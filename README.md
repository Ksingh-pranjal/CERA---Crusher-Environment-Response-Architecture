# CERA — Crushing Environment Response Architecture

A fast-start React + Express dashboard inspired by modern industrial websites. It includes a landing-page video hero, monitoring UI, serial backend, event history, and a firmware integration guide.

## Requirements
- Node.js 20+ (LTS recommended)
- Arduino-compatible board connected over USB for live hardware data
- On Windows, identify the board's COM port in Device Manager

## Run it
1. Extract this folder.
2. Open a terminal in the extracted `CERA` project folder.
3. Run `npm install` and `npm run install:all`.
4. Set the serial port in `server/.env` (copy `.env.example` to `.env`, then edit `SERIAL_PORT=COM3`, for example). If `SERIAL_PORT` is omitted or the controller is disconnected, the dashboard shows waiting/offline status and blank sensor values. It never generates fake sensor readings.
5. Run `npm run dev`.
6. Open the Vite URL shown in the terminal, usually http://localhost:5173.

## Hardware integration
- The server reads only CSV lines beginning with `DATA,` and ignores the human-readable lines printed by the current sketch.
- Use `firmware/CERAController.ino` for the command-enabled prototype firmware. Follow `firmware/INTEGRATION.md` and verify the pin mapping before connecting hardware.
- Serial control is a prototype interface only. Do not connect the prototype relay directly to a real crusher motor. Real plant controls need professionally engineered interlocks, contactors, emergency-stop circuits and qualified installation.
- The `blowerOn` / `pumpOn` fields in the existing sketch report requested relay state, not verified physical operation.

## Notes
- The Propel video is loaded from the URL supplied by the user. If the source blocks embedding or is unavailable, replace it with a locally licensed MP4 in `client/public/` and update the `<video>` source.
- This is a local prototype. It does not include production-grade authentication or hardened industrial control security.


## Real sensor data only
CERA's backend intentionally does not generate fake sensor readings. Before the Arduino is connected, dashboard sensor values remain blank and hardware controls are disabled. Set `SERIAL_PORT=COMx` in `server/.env`, match `SERIAL_BAUD` to `Serial.begin(...)`, upload `firmware/CERAController.ino`, close Arduino Serial Monitor, and then run `npm run dev`.

## Photos
The `client/public/images/` directory contains the supplied real plant photos and the tabletop prototype photo. The home page uses the plant photos in an auto-scrolling strip and explains prototype components in a gallery section.
