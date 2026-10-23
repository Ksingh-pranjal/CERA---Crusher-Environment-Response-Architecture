import 'dotenv/config';
import express from 'express';
import cors from 'cors';
import { createServer } from 'node:http';
import { Server } from 'socket.io';
import { SerialPort } from 'serialport';
import { ReadlineParser } from '@serialport/parser-readline';

const app = express();
const httpServer = createServer(app);
const io = new Server(httpServer, { cors: { origin: '*' } });
app.use(cors());
app.use(express.json());

const PORT = Number(process.env.PORT || 4000);
const SERIAL_PORT = process.env.SERIAL_PORT?.trim();
const SERIAL_BAUD = Number(process.env.SERIAL_BAUD || 9600);
let serial = null;
let serialConnected = false;
let mode = 'WAITING_FOR_HARDWARE';
let latest = {
  temperature: null, humidity: null, dustRaw: null, dustTrend: 'UNKNOWN',
  blowerOn: false, pumpOn: false, statusText: 'WAITING_FOR_HARDWARE', sensorError: false,
  timestamp: null, connected: false, demo: false
};
const readings = [];
const events = [];
const commands = [];
let demoTimer;

function recordEvent(type, severity, message) {
  const event = { id: `${Date.now()}-${Math.random().toString(16).slice(2, 7)}`, type, severity, message, timestamp: new Date().toISOString(), resolved: false };
  events.unshift(event);
  if (events.length > 300) events.pop();
  io.emit('event:new', event);
}

function parseData(line) {
  const p = line.trim().split(',');
  if (p.length < 9 || p[0] !== 'DATA') return null;
  const num = (v) => v === 'NA' || v === '' ? null : Number(v);
  const temp = num(p[1]); const humidity = num(p[2]); const dust = num(p[3]);
  if (dust === null || !Number.isFinite(dust)) return null;
  return {
    temperature: temp !== null && Number.isFinite(temp) ? temp : null,
    humidity: humidity !== null && Number.isFinite(humidity) ? humidity : null,
    dustRaw: dust,
    dustTrend: p[4] || 'UNKNOWN',
    blowerOn: p[5] === '1',
    pumpOn: p[6] === '1',
    statusText: p[7] || 'UNKNOWN',
    sensorError: p[8] === '1',
    timestamp: new Date().toISOString(), connected: true, demo: false
  };
}

function acceptReading(next) {
  const old = latest;
  latest = next;
  readings.push(next);
  if (readings.length > 1200) readings.shift();
  io.emit('sensor:update', latest);
  if (next.sensorError && !old.sensorError) recordEvent('SENSOR_ERROR', 'critical', 'A sensor error was reported by the controller.');
  if (next.dustRaw >= 70 && old.dustRaw < 70) recordEvent('HIGH_DUST', 'warning', `Raw dust reading reached ${next.dustRaw} (configured high threshold: 70).`);
  if (next.statusText !== old.statusText) recordEvent('SYSTEM_STATUS', next.sensorError ? 'critical' : 'info', next.statusText.replaceAll('_', ' '));
}

function startDemo() {
  // Intentionally disabled: CERA must never present simulated values as sensor readings.
  mode = 'WAITING_FOR_HARDWARE';
  clearInterval(demoTimer);
}


if (SERIAL_PORT) {
  try {
    serial = new SerialPort({ path: SERIAL_PORT, baudRate: SERIAL_BAUD, autoOpen: false });
    const parser = serial.pipe(new ReadlineParser({ delimiter: '\n' }));
    parser.on('data', (line) => {
      const clean = line.trim();
      if (clean.startsWith('ACK,')) {
        const parts = clean.split(',');
        const ack = { command: parts[1] || 'UNKNOWN', result: parts[2] || 'UNKNOWN', timestamp: new Date().toISOString() };
        const matching = commands.find(c => c.action === ack.command && c.result === 'SENT_NOT_CONFIRMED');
        if (matching) matching.result = ack.result;
        io.emit('command:ack', ack);
        recordEvent('COMMAND_ACK', ack.result === 'ACCEPTED' ? 'success' : 'warning', `Controller acknowledgement: ${ack.command} — ${ack.result}.`);
        return;
      }
      const next = parseData(clean);
      if (next) acceptReading(next);
    });
    serial.on('open', () => {
      serialConnected = true; clearInterval(demoTimer);
      latest = { ...latest, connected: true, demo: false };
      recordEvent('HARDWARE_CONNECTED', 'success', `Serial controller connected on ${SERIAL_PORT}.`);
      io.emit('connection', { connected: true, demo: false, port: SERIAL_PORT });
    });
    serial.on('close', () => {
      serialConnected = false;
      latest = { ...latest, connected: false };
      recordEvent('HARDWARE_DISCONNECTED', 'critical', 'Serial connection closed. Displayed readings may be stale.');
      io.emit('connection', { connected: false, demo: false, port: SERIAL_PORT });
    });
    serial.on('error', (err) => {
      console.error('Serial error:', err.message);
      recordEvent('SERIAL_ERROR', 'critical', `Serial error: ${err.message}`);
    });
    serial.open((err) => {
      if (err) {
        console.error(`Could not open ${SERIAL_PORT}: ${err.message}`);
        recordEvent('SERIAL_OPEN_FAILED', 'warning', `Could not open ${SERIAL_PORT}; check the port and close Arduino Serial Monitor.`);
        startDemo();
      }
    });
  } catch (err) {
    console.error(err.message); startDemo();
  }
} else {
  startDemo();
}

app.get('/api/health', (_req, res) => res.json({ ok: true, connected: serialConnected, demo: false, port: SERIAL_PORT || null }));
app.get('/api/status', (_req, res) => res.json({ ...latest, connected: serialConnected, demo: false }));
app.get('/api/history', (req, res) => {
  const limit = Math.min(300, Math.max(1, Number(req.query.limit || 80)));
  res.json(readings.slice(-limit));
});
app.get('/api/events', (_req, res) => res.json(events.slice(0, 100)));
app.get('/api/commands', (_req, res) => res.json(commands.slice(0, 100)));

app.post('/api/control', (req, res) => {
  const { action, value } = req.body || {};
  const allowed = new Set(['MODE_AUTO', 'MODE_STANDBY', 'MODE_MANUAL', 'BLOWER_ON', 'BLOWER_OFF', 'PUMP_ON', 'PUMP_OFF']);
  if (!allowed.has(action)) return res.status(400).json({ ok: false, error: 'Unsupported command.' });
  if (!serial || !serialConnected || !serial.isOpen) {
    return res.status(409).json({ ok: false, error: 'No live hardware serial connection. Demo mode does not control physical equipment.' });
  }
  const wire = ({ MODE_AUTO: 'MODE,AUTO', MODE_STANDBY: 'MODE,STANDBY', MODE_MANUAL: 'MODE,MANUAL', BLOWER_ON: 'BLOWER,ON', BLOWER_OFF: 'BLOWER,OFF', PUMP_ON: 'PUMP,ON', PUMP_OFF: 'PUMP,OFF' })[action];
  const entry = { id: `${Date.now()}`, action, value: value ?? null, timestamp: new Date().toISOString(), result: 'SENT_NOT_CONFIRMED' };
  commands.unshift(entry);
  serial.write(`${wire}\n`, (err) => {
    if (err) { entry.result = 'WRITE_FAILED'; return; }
  });
  recordEvent('COMMAND_SENT', 'info', `Command sent: ${action}. Hardware acknowledgement is required to confirm execution.`);
  res.json({ ok: true, command: entry, message: 'Command sent; physical execution is not confirmed until the controller acknowledges it.' });
});

io.on('connection', (socket) => {
  socket.emit('sensor:update', { ...latest, connected: serialConnected, demo: false });
  socket.emit('events:initial', events.slice(0, 30));
});

httpServer.listen(PORT, () => console.log(`CERA API running at http://localhost:${PORT} | ${SERIAL_PORT ? `serial=${SERIAL_PORT}` : 'waiting for hardware (set SERIAL_PORT in server/.env)'}`));
