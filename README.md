# BMini4 – ESP8266 RC Car


## Note about the : 
- This readme is outdated, I changed a lot in the code but didn't update the readme.
I'm currently busy studying, so there will be no updates for now.
i will update the readme and add some documentation comments to the code. 

## OVERVIEW
- WiFi-controlled RC car using ESP8266 with the following features:
- Steering servo (45-135° range
- 5-level throttle with forward/reverse
- Realistic braking: hold 1.2s to engage reverse gear
- Headlights, backlighys, turn indicators (blinking)
- Auxiliary LED withmultiple animation modes

## HARDWARE & CONNECTIONS

- Component (used model) : role -> pin

- SEVO (SG90) : Steering -> D1 (GPIO5)
- Driver (Mini L298) : Drives the motor
  - Motor forward -> D2 (GPIO4)
  - Motor reverse -> D6 (GPIO12)
- DC Motor (JQ24-25H440) : motor -> to the driver's output
- Active Buzze (regular) : Beep (Horn) -> D5 (GPIO15)
- x2 White LEDs (5mm) : Headlights - D4 (GPIO2)
- x4 Yellow LEDs (5nm) : Turn indicator -> D0 (GPIO16), D3 (GPIO0)
- x2 Red LEDs (5nm) : Brake/reverse lights - D7 (GPIO13)
- RGB LED (5nm) : Aux LEDs -> D8 (GPIO1), RX, TX

## PROTOCOL & DETAILS

### Commands received (plain text, newline-terminated)

- servo \<angle> - Set steering (45-135, threshold >=8 to reduce spam)
- gas \<level> - Set throttle (0-5)
- brake on/off - Brake control (hold 1.2s → reverse gear)
- beep on/off - Horn
- head on/off - Headlights
- ind left/right/off - Turn indicators
- led off/blink/on - Simple LED modes
- led linear/breathe/heartbeat/strobe/glitch/candle - Fade animations

### Telemetry sent

- TELEM:,,100\n - Keep-alive every 8s (battery % placeholder)

### NETWORK

- Access Point: SSID "BMini4", password "26032009"
- IP: 192.168.4.1:80
- Single client TCP server
- Proper client cleanup on disconnect for reliable reconnection

### TIMINGS

- Servo threshold: 8° minimum change to update
- Brake pulse: 200ms reverse burst when braking while moving
- Brake long: 1200ms hold to enter reverse gear
- Blink interval: 500ms (indicators, blink mode, reverse lights)
- Fade interval: 50ms (smooth animations)
- Telemetry: 8000ms keep-alive

## DASHBOARD & TODO (not really in order though)

- [x] AP init & Connection handler & CMD handler
- [x] motor & gas states
- [x] steering servo
- [x] Headlights / indicators / backlights
- [x] Brake & reverse logic
- [x] LED animations
- [x] DEBUG macros (i needed them)
- [x] Docmentation
- [x] Some Logic enhancements

- [ ] Add battery level
- [x] Aux LED and add animatons
