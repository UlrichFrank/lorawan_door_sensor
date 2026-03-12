/**
 * 
 * FOR THIS EXAMPLE TO WORK, YOU MUST INSTALL THE "LoRaWAN_ESP32" LIBRARY USING
 * THE LIBRARY MANAGER IN THE ARDUINO IDE.
 * 
 * This code continuously monitors a reed switch contact on GPIO4 and sends
 * LoRaWAN messages when the contact state changes OR every 15 minutes,
 * whichever comes first. The device runs without deep sleep in continuous mode.
 *
 * If your NVS partition does not have stored TTN / LoRaWAN provisioning
 * information in it yet, you will be prompted for them on the serial port and
 * they will be stored for subsequent use.
 *
 * See https://github.com/ropg/LoRaWAN_ESP32
*/


// Send interval: 15 minutes in milliseconds (without deep sleep)
#define SEND_INTERVAL 900000

// do not use oled
#define NO_DISPLAY
#define NO_DISPLAY_INSTANCE     // Verhindert Erstellung der Display-Instanz
#define HELTEC_NO_DISPLAY      // Deaktiviert Display in der Heltec-Bibliothek

//button press will wake it up and a long press will turn it off.
#define HELTEC_POWER_BUTTON

#include <heltec_unofficial.h>
#include <LoRaWAN_ESP32.h>
#include "driver/rtc_io.h"

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
  Serial.println("\n\n=== LoRaWAN Door Sensor (Continuous Mode) ===");

  // default is 10 due to backward compatibility
  analogReadResolution(12);

  // Reed-Kontakt on GPIO4 as input
  pinMode(REED_GPIO, INPUT_PULLUP);
  
  // Read initial reed state and send immediately
  int reedState = digitalRead(REED_GPIO);
  lastReedState = reedState;
  Serial.printf("Initial reed state: %d (%s)\n", reedState, reedState == OPENED ? "OPENED" : "CLOSED");
  
  // Check if provisioning is available
  Serial.println("Checking LoRaWAN provisioning...");
  if (!persist.isProvisioned()) {
    Serial.println("ERROR: No provisioning data found. Please use Arduino IDE serial monitor to provision.");
    Serial.println("You need to restart the device and enter provisioning info via serial port.");
    Serial.println("The device will remain in this state until provisioning is done.");
    while (true) {
      heltec_delay(1000);  // Block here until user provisions
    }
  }
  
  Serial.println("Provisioning OK. Attempting to join network...");
  
  // Try initial send on boot
  sendData(reedState);
  lastSendTime = millis();
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
  
  // Poll every 2 seconds
  heltec_delay(2000);
}
