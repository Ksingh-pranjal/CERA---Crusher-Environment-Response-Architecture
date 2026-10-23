# Firmware integration (read before using dashboard controls)

## Fastest path
1. Make a backup of your current sketch.
2. Open `CERAController.ino` in Arduino IDE.
3. Install the `DHT sensor library` (Adafruit) and its required Adafruit Unified Sensor dependency if prompted.
4. Confirm your board and pins match your original wiring. This sketch assumes the same pin layout as the code supplied in chat (`A0`, pins 2/3/6/11/12), typical of an Arduino Uno/Nano-style board.
5. Upload it. Close Arduino Serial Monitor before starting the Node backend because only one application can normally own a serial port at a time.
6. Copy `server/.env.example` to `server/.env`, set `SERIAL_PORT=COM3` (replace with your actual COM port), and leave `SERIAL_BAUD=9600`.
7. Restart the backend and check that the dashboard says `Hardware connected`.

## Serial protocol
The controller sends the original data row:
`DATA,temp,humidity,dust,trend,blower,pump,status,error`

Commands accepted by this adaptation:
- `MODE,AUTO`
- `MODE,STANDBY`
- `MODE,MANUAL`
- `BLOWER,ON` / `BLOWER,OFF` (manual mode only)
- `PUMP,ON` / `PUMP,OFF` (manual mode only)

The firmware returns `ACK,...` lines. The backend currently logs that a command was sent but does not wait for the ACK to mark it confirmed; check the controller's Serial Monitor during bench testing if needed.

## Important safety notes
- Start in STANDBY and test with low-voltage prototype loads.
- This is a student prototype, not a safety-rated industrial controller.
- Do not connect an ESP/Arduino relay directly to a real crusher motor. Real equipment requires a qualified electrical design with appropriately rated contactors, overload protection, interlocks and an independent emergency-stop circuit.
- Manual mode is not an emergency stop. Do not rely on browser connectivity for safety.
- The dust threshold values (20/70) are raw ADC values and must be calibrated before operational use.
- `blowerOn` and `pumpOn` report commanded relay state; add feedback sensors if actual operation must be verified.
