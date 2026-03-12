# Copilot Instructions for LoRaWAN Door Sensor

## Project Overview

This is an Arduino sketch for a **Heltec ESP32-based LoRaWAN door sensor**. It monitors a reed switch contact connected to GPIO4 and sends periodic status updates via LoRaWAN to The Things Network (TTN).

### Key Purpose

- Monitors a magnetic reed switch (door open/closed state)
- Measures temperature and battery voltage
- Sends LoRaWAN messages with sensor data
- Operates in deep sleep mode for extended battery life
- Wakes on timer (periodic check) or reed switch state change

## Build & Upload

**IDE**: Arduino IDE with Heltec-specific boards and libraries

**Required Libraries** (install via Library Manager):
- `LoRaWAN_ESP32` - Core LoRaWAN functionality
- `Heltec ESP32 Dev-Boards` (board support package)
- `heltec_unofficial` - Heltec hardware utilities (display, battery, temperature)

**Board Configuration**:
- Board: Heltec WiFi LoRa 32(V3)
- Additional URLs in Arduino IDE: `https://github.com/Heltec-Aaron-Lee/WiFi_Kit_series/releases/download/0.0.9/package_heltec_esp32_index.json`

**To Build/Upload**:
1. Open `lorawan_doorsensor.ino` in Arduino IDE
2. Select correct board and COM port
3. Click Upload (or `arduino-cli upload` via command line)

**Debug Mode**:
- Uncomment `#define DEBUG` near top of sketch to enable serial loop output instead of sleep mode
- Useful for testing without 15-minute delays between messages

## Architecture & Key Concepts

### Sleep & Wake Strategy

The device uses **deep sleep** to minimize power consumption:

- **Closed state**: Sleeps for 86400 seconds (24 hours), wakes on `EXT1_WAKEUP_ANY_HIGH` (door opens)
- **Open state**: Sleeps for 300 seconds (5 minutes), wakes on `EXT1_WAKEUP_ALL_LOW` (door closes)

These intervals are defined by `MINIMUM_DELAY_CLOSED` and `MINIMUM_DELAY_OPENED` macros.

### Data Payload Format

Each LoRaWAN message is 7 bytes:

```
Byte 0:    Counter (8-bit, incremented per message, persists across sleep via RTC_DATA_ATTR)
Byte 1:    Temperature (°C + 100 offset)
Bytes 2-3: Battery voltage (millivolts, big-endian 16-bit)
Byte 4:    Reed state (1=OPENED/HIGH, 0=CLOSED/LOW)
Bytes 5-6: Battery percentage (big-endian 16-bit)
```

### Power Management

- **Reed GPIO**: Uses RTC peripheral to maintain GPIO state/pullup across deep sleep (lines 148-150)
- **Display**: Disabled with `#define NO_DISPLAY` to reduce power consumption
- **Battery Reading** (`heltec_vbat_v3_2()`): Custom calibration function for Heltec V3.2 board with hardcoded voltage divider and calibration constants

### TTN Integration

- Uses `LoRaWAN_ESP32` library with persistent session storage
- Credentials stored in NVS (Non-Volatile Storage) flash
- Respects TTN Fair Use Policy via `node->setDutyCycle(true, 1250)` (max 30 messages/day)
- `node->timeUntilUplink()` enforces minimum intervals between transmissions

### Hardware Specifics

- **GPIO4**: Reed switch input with RTC pullup
- **VBAT_ADC**: Battery voltage ADC pin
- **VBAT_CTRL**: Battery voltage sensing enable pin
- **Power Button**: Enabled with `#define HELTEC_POWER_BUTTON` for manual wakeup/shutdown

## Code Conventions

### Constants & Naming

- Reed state constants: `OPENED = HIGH (1)`, `CLOSED = LOW (0)`
- Wake mode macros: `WAKEUP_ON_CLOSED`, `WAKEUP_ON_OPENED` (set at top of file)
- RTC-persistent data: Use `RTC_DATA_ATTR` attribute to preserve across sleep (e.g., `count` variable)

### Macros for Configuration

All key behavior is controlled via `#define` macros at the top:
- `MINIMUM_DELAY_CLOSED` / `MINIMUM_DELAY_OPENED` - Sleep intervals in seconds
- `DEBUG` - Enable/disable test loop mode
- `NO_DISPLAY` - Disable OLED to save power
- `HELTEC_POWER_BUTTON` - Enable manual power button

### Flow Comments

Code includes German and English comments for wake reasons and reed contact checks. Maintain bilingual style for consistency.

### Serial Output

Uses `Serial.printf()` for debug logging. Format:
```
Temperature: T°C, Battery: VmV, Percentage: P%, Reed: R=STATE
Wake up reason: [External trigger / Timer / Undefined / Other]
Next TX in X s. Wakeup On: Y
```

## Common Tasks

**Adding a new sensor**:
1. Read value in `sendData()` function before radio init
2. Pack into `uplinkData[]` array (max 51 bytes for LoRaWAN)
3. Update payload format documentation

**Adjusting sleep intervals**:
- Modify `MINIMUM_DELAY_CLOSED` and `MINIMUM_DELAY_OPENED` macros
- Note: Actual delay may be longer due to TTN Fair Use Policy enforcement

**Testing without waiting for sleep**:
- Uncomment `#define DEBUG` at top
- Upload and open Serial Monitor
- Device will loop in `loop()` function, printing reed state every 1 second

**Debugging transmission issues**:
- Check Serial Monitor output for radio init errors
- Verify TTN provisioning is stored (first run will prompt for DevEUI, AppKey, etc.)
- Ensure antenna is properly connected
- TTN Fair Use Policy may delay transmission: check `timeUntilUplink()` messages
