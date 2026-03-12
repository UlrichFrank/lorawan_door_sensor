/**
 * 
 * FOR THIS EXAMPLE TO WORK, YOU MUST INSTALL:
 * 1. "heltec_esp32" library using LIBRARY MANAGER (search "heltec_esp32")
 * 2. "LoRaWAN_ESP32" library using LIBRARY MANAGER
 * 3. ESP32 board support: Add https://espressif.github.io/arduino-esp32/package_esp32_index.json to settings
 * 4. Install "esp32 by Espressif Systems" from board manager
 * 5. Select board: "Heltec WiFi LoRa 32(V3) / Wireless shell(V3)"
 * 
 * This code continuously monitors a reed switch contact on GPIO4 and sends
 * LoRaWAN messages when the contact state changes OR every 15 minutes,
 * whichever comes first. The device runs without deep sleep in continuous mode.
 *
 * The OLED display shows:
 * - Door state (OPEN/CLOSED)
 * - Time until next periodic LoRaWAN transmission (in minutes or seconds)
 *
 * If your NVS partition does not have stored TTN / LoRaWAN provisioning
 * information in it yet, you will be prompted for them on the serial port and
 * they will be stored for subsequent use.
 *
 * See https://github.com/ropg/heltec_esp32_lora_v3
 * See https://github.com/ropg/LoRaWAN_ESP32
 */



// Send interval: 15 minutes in milliseconds (without deep sleep)
#define SEND_INTERVAL 900000

// button press will wake it up and a long press will turn it off.
#define HELTEC_POWER_BUTTON

#include <heltec_unofficial.h>
#include <LoRaWAN_ESP32.h>

//#define REED_GPIO 4
#define REED_GPIO GPIO_NUM_4
#define OPENED HIGH
#define CLOSED LOW

//#define DEBUG

LoRaWANNode* node;

RTC_DATA_ATTR uint8_t count = 0;

// State tracking for continuous monitoring
int lastReedState = -1;
unsigned long lastSendTime = 0;
unsigned long lastDisplayUpdate = 0;

void updateDisplay() {
  // Only update display every 1 second to reduce flicker
  unsigned long now = millis();
  if (now - lastDisplayUpdate < 1000) {
    return;
  }
  lastDisplayUpdate = now;
  
  int reedState = digitalRead(REED_GPIO);
  unsigned long timeSinceLastSend = (now - lastSendTime);
  unsigned long timeUntilNextSend = SEND_INTERVAL - timeSinceLastSend;
  
  // Calculate display string for time remaining
  String timeStr;
  if (timeUntilNextSend > 60000) {
    // More than 1 minute: show in minutes
    unsigned long minutes = timeUntilNextSend / 60000;
    timeStr = String(minutes) + "m";
  } else {
    // Less than 1 minute: show in seconds
    unsigned long seconds = timeUntilNextSend / 1000;
    timeStr = String(seconds) + "s";
  }
  
  // Only try to update display if it exists
  if (!display.isLayoutFitted()) {
    return;
  }
  
  try {
    // Clear and update display
    display.clear();
    display.setFont(ArialMT_Plain_16);
    
    // Line 1: Status
    display.drawString(0, 0, "LoRaWAN Door Sensor");
    
    // Line 2: Reed state
    display.setFont(ArialMT_Plain_24);
    String reedStr = (reedState == OPENED) ? "OPEN" : "CLOSED";
    display.drawString(0, 20, "Door: " + reedStr);
    
    // Line 3: Time until next send
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 48, "Next TX: " + timeStr);
    
    display.display();
  } catch (...) {
    // If display fails, just continue
    Serial.println("WARNING: Display update failed");
  }
}

float heltec_vbat_v3_2() {
  // ADC resolution
  const int resolution = 12;
  const int adcMax = pow(2,resolution) - 1;
  //const float adcMaxVoltage = 3.3; // https://digitalconcepts.net.au/arduino/index.php?op=Battery
  const float adcMaxVoltage = 3.3; // 3,9V https://my-esp-idf.readthedocs.io/en/latest/api-reference/peripherals/adc.html At 11dB attenuation the maximum voltage is limited by VDD_A, not the full scale voltage.
  // On-board voltage divider
  const int R1 = 390;
  const int R2 = 100;
  // Calibration measurements
  const float reportedVoltage = 3.893;
  const float measuredVoltage = 4.176;
  // Calibration factor
  const float factor = (adcMaxVoltage / adcMax) * ((R1 + R2)/(float)R2) * (measuredVoltage / reportedVoltage);

  //analogSetPinAttenuation(VBAT_ADC, ADC_11db);
  pinMode(VBAT_CTRL, OUTPUT);
  digitalWrite(VBAT_CTRL, HIGH);
  delay(5);
  float vbat = analogRead(VBAT_ADC) * factor;
  // pulled up, no need to drive it
  pinMode(VBAT_CTRL, INPUT);
  return vbat;
}

void sendData(int reedState) {
  // Check if we have provisioning info and radio is available
  if (!persist.isProvisioned()) {
    Serial.println("ERROR: No provisioning data. Cannot send.");
    return;
  }
  
  float temp = heltec_temperature();
  float vbat = heltec_vbat_v3_2();
  uint16_t vbat_milivolt = (uint16_t)(vbat * 1000.0f);
  int vbat_percentage = heltec_battery_percent(vbat);
  
  Serial.printf("Temperature: %.1f°C, Battery: %umV, Percentage: %d%%, Reed: %d=%s\n", 
                temp, vbat_milivolt, vbat_percentage, reedState, reedState == OPENED ? "OPENED" : "CLOSED");
  
  // initialize radio
  Serial.println("Radio init");
  int16_t state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.println("Radio did not initialize. We'll try again later.");
    return;
  }

  node = persist.manage(&radio);

  if (!node->isActivated()) {
    Serial.println("Could not join network. We'll try again later.");
    return;
  }

  // Manages uplink intervals to the TTN Fair Use Policy
  node->setDutyCycle(true, 1250);

  uint8_t uplinkData[7];
  uplinkData[0] = count++;
  uplinkData[1] = temp + 100; 
  uplinkData[2] = vbat_milivolt >> 8;
  uplinkData[3] = vbat_milivolt & 0xFF;
  uplinkData[4] = reedState;  // HIGH (1) = offen, LOW (0) = geschlossen
  uplinkData[5] = vbat_percentage >> 8;
  uplinkData[6] = vbat_percentage & 0xFF;

  uint8_t downlinkData[256];
  size_t lenDown = sizeof(downlinkData);

  state = node->sendReceive(uplinkData, sizeof(uplinkData), 1, downlinkData, &lenDown);

  if(state == RADIOLIB_ERR_NONE) {
    Serial.println("Message sent, no downlink received.");
  } else if (state > 0) {
    Serial.println("Message sent, downlink received.");
  } else {
    Serial.printf("sendReceive returned error %d, we'll try again later.\n", state);
  }
  
  // Save session for next time
  persist.saveSession(node);
  
  // Calculate time until next allowed transmission
  uint32_t nextTX = node->timeUntilUplink();
  Serial.printf("Next TX possible in %u seconds\n", nextTX);
}

void setup() {
  heltec_setup();
  
  // Force flush of serial
  delay(100);
  
  Serial.println("\n\n");
  Serial.println("=== LoRaWAN Door Sensor (Continuous Mode) ===");
  Serial.flush();

  // default is 10 due to backward compatibility
  analogReadResolution(12);

  // Reed-Kontakt on GPIO4 as input
  pinMode(REED_GPIO, INPUT_PULLUP);
  
  // Read initial reed state and send immediately
  int reedState = digitalRead(REED_GPIO);
  lastReedState = reedState;
  Serial.printf("Initial reed state: %d (%s)\n", reedState, reedState == OPENED ? "OPENED" : "CLOSED");
  Serial.flush();
  
  // Try to initialize and update display - with error handling
  Serial.println("Initializing display...");
  Serial.flush();
  
  if (display.isLayoutFitted()) {
    Serial.println("Display is available");
    display.clear();
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0, "LoRaWAN Door Sensor");
    display.drawString(0, 20, "Initializing...");
    display.display();
    Serial.println("Display updated");
  } else {
    Serial.println("WARNING: Display not available");
  }
  Serial.flush();
  
  // Check if provisioning is available
  Serial.println("Checking LoRaWAN provisioning...");
  Serial.flush();
  
  if (!persist.isProvisioned()) {
    Serial.println("ERROR: No provisioning data found. Please use Arduino IDE serial monitor to provision.");
    Serial.println("You need to restart the device and enter provisioning info via serial port.");
    Serial.println("The device will remain in this state until provisioning is done.");
    Serial.flush();
    
    display.clear();
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 10, "ERROR: No provisioning");
    display.drawString(0, 25, "Use Serial Monitor");
    display.drawString(0, 40, "to enter credentials");
    display.drawString(0, 55, "Then restart device");
    display.display();
    
    while (true) {
      heltec_delay(1000);
    }
  }
  
  Serial.println("Provisioning OK. Attempting to join network...");
  Serial.flush();
  
  // Try initial send on boot
  sendData(reedState);
  lastSendTime = millis();
  
  // Show initial state on display
  updateDisplay();
  
  Serial.println("Setup complete");
  Serial.flush();
}

void loop() {
  heltec_loop();
  
  int reedState = digitalRead(REED_GPIO);
  unsigned long currentTime = millis();
  
  // Check if state changed OR 15 minutes have passed since last send
  bool stateChanged = (reedState != lastReedState);
  bool intervalExceeded = (currentTime - lastSendTime > SEND_INTERVAL);
  
  if (stateChanged || intervalExceeded) {
    if (!persist.isProvisioned()) {
      Serial.println("WARNING: Provisioning data missing. Cannot send.");
      heltec_delay(2000);
      return;
    }
    
    Serial.printf("[%lu ms] Trigger: %s, Reed: %d\n", 
                  currentTime, stateChanged ? "STATE_CHANGED" : "TIMER_15MIN", reedState);
    sendData(reedState);
    lastSendTime = currentTime;
    lastReedState = reedState;
  }
  
  // Update display (throttled to 1 second)
  updateDisplay();
  
  // Poll every 2 seconds
  heltec_delay(2000);
}
