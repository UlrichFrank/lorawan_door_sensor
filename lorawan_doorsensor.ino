/**
 * 
 * FOR THIS EXAMPLE TO WORK, YOU MUST INSTALL THE "LoRaWAN_ESP32" LIBRARY USING
 * THE LIBRARY MANAGER IN THE ARDUINO IDE.
 * 
 * This code will send a two-byte LoRaWAN message every 15 minutes. The first
 * byte is a simple 8-bit counter, the second is the ESP32 chip temperature
 * directly after waking up from its 15 minute sleep in degrees celsius + 100.
 *
 * If your NVS partition does not have stored TTN / LoRaWAN provisioning
 * information in it yet, you will be prompted for them on the serial port and
 * they will be stored for subsequent use.
 *
 * See https://github.com/ropg/LoRaWAN_ESP32
*/


// Pause between sends in seconds, so this is every 15 minutes. (Delay will be
// longer if regulatory or TTN Fair Use Policy requires it.)
#define MINIMUM_DELAY_CLOSED 86400 
#define MINIMUM_DELAY_OPENED 300
#define WAKEUP_ON_CLOSED ESP_EXT1_WAKEUP_ALL_LOW
#define WAKEUP_ON_OPENED ESP_EXT1_WAKEUP_ANY_HIGH

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

  uint8_t uplinkData[5];
  uplinkData[0] = count++;
  uplinkData[1] = temp + 100; 
  uplinkData[2] = vbat_milivolt >> 8;
  uplinkData[3] = vbat_milivolt & 0xFF;
  uplinkData[4] = reedState;  // HIGH (1) = offen, LOW (0) = geschlossen

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
}

void setup() {
  heltec_setup();

  // Sofort nach dem Aufwachen Wake-up Grund prüfen
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  Serial.print("Wake up reason: ");
  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT1: 
      Serial.println("External trigger (Reed contact)"); 
      break;
    case ESP_SLEEP_WAKEUP_TIMER: 
      Serial.println("Timer"); 
      break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      Serial.println("Undefined (normal boot)");
      break;
    default: 
      Serial.printf("Other: %d\n", wakeup_reason); 
      break;
  }
  
  // default is 10 due to backward compatibility
  analogReadResolution(12);

  // Reed-Kontakt on GPIO4 as Wakeup-Source
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  rtc_gpio_pullup_en(REED_GPIO); // Verwenden statt pinMode(REED_GPIO, INPUT_PULLUP), da pinMode deepsleep nicht überlebt
  rtc_gpio_pulldown_dis(REED_GPIO);
  //pinMode(REED_GPIO, INPUT_PULLUP);

#ifdef DEBUG
#else
  int reedState = digitalRead(REED_GPIO);  // Liest den Zustand des Reed-Kontakts
  sendData(reedState);

  // Does not return, program starts over next round
  if (reedState == OPENED) {
    goToSleep(MINIMUM_DELAY_OPENED, WAKEUP_ON_CLOSED);
  } else {
    goToSleep(MINIMUM_DELAY_CLOSED, WAKEUP_ON_OPENED);
  }
#endif
}

void loop() {
  #ifdef DEBUG
    heltec_loop();
    heltec_delay(1000);
    int reedState = digitalRead(REED_GPIO);  // Liest den Zustand des Reed-Kontakts
    Serial.printf("reed: %d, %s\n", reedState, reedState == OPENED ? "OPEN" : "CLOSED");
  #endif
}

void goToSleep(int delay, esp_sleep_ext1_wakeup_mode_t wakeupMode) {
  Serial.println("Going to deep sleep now");
  // allows recall of the session after deepsleep
  persist.saveSession(node);
  // Calculate minimum duty cycle delay (per FUP & law!)
  uint32_t interval = node->timeUntilUplink();
  // And then pick it or our MINIMUM_DELAY, whichever is greater
  uint32_t delayMs = max(interval, (uint32_t)delay * 1000);

  esp_sleep_enable_ext1_wakeup(1ULL << REED_GPIO, wakeupMode);
 
  Serial.printf("Next TX in %i s. Wakeup On: %d\n", delayMs/1000, (int) wakeupMode);
  heltec_delay(100);  // So message prints

  // and off to bed we go
  heltec_deep_sleep(delayMs/1000);
}