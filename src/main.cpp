#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include <cstring>
#include <string>


#include "smartCupConfig.h"
#include "drivers.h"


#define MONITOR_SPEED 115200
#define DEFAULT_DELAY 500


void setup() {
 Serial.begin(MONITOR_SPEED);
 delay(DEFAULT_DELAY);

 Serial.println("Hello World! Starting ESP32 BLE + OLED test...");

 // Initialization
 if (OLEDinit() != EXT_CODE_SUCCESS){
 showOLEDMessage("OLED Init failed! err code: ERR_CODE_OLED_INIT_FAILED");
 Serial.println("Restarting ESP32...");
 Serial.flush();
 ESP.restart();
 }

  if (bootupScreen() != EXT_CODE_SUCCESS){
 showOLEDMessage("Bootup screen failed! err code: ERR_CODE_BOOTUP_SCREEN_FAIL");
 Serial.println("Restarting ESP32...");
 Serial.flush();
 ESP.restart();
 }

 if (temperatureSensorInit() != EXT_CODE_SUCCESS){
  showOLEDMessage("Temperature sensors failed to init! err code: ERR_CODE_TEMP_SENSOR_INIT_FAIL");
  Serial.println("Restarting ESP32...");
  Serial.flush();
  ESP.restart();
 }

 if (buzzerinit() != EXT_CODE_SUCCESS){
  showOLEDMessage("Buzzer init failed! err code: ERR_CODE_BUZZER_INIT_FAIL");
  Serial.println("Restarting ESP32...");
  Serial.flush();
  ESP.restart();
 }
 
 if (bluetoothinit() != EXT_CODE_SUCCESS){
  showOLEDMessage("Bluetooth init failed! err code: ERR_CODE_BLUETOOTH_INIT_FAILED");
  Serial.println("Restarting ESP32...");
  Serial.flush();
  ESP.restart();
 }

 // hardare init done... software init time
  oledTextQueue = xQueueCreate(5, sizeof(OledTextMessage));

 if (oledTextQueue == NULL){
  Serial.println("Failed to create OLED text queue. err code: ERR_CODE_OLED_QUEUE_CREATION_FAILED");
  Serial.println("Restarting ESP32...");
  Serial.flush();
  ESP.restart();
 }

 temperatureCommandQueue = xQueueCreate(1, sizeof(uint8_t));

 if (temperatureCommandQueue == nullptr) {
   Serial.println("Failed to create temperature command queue. err code: ERR_CODE_TEMP_COMMAND_QUEUE_CREATION_FAILED");
   Serial.println("Restarting ESP32...");
   Serial.flush();
   ESP.restart();
 }

 capacityCommandQueue = xQueueCreate(1, sizeof(uint8_t));

 if (capacityCommandQueue == nullptr){
   Serial.println("Failed to create capacity commmand queue. err code: ERR_CODE_CAPACITY_COMMAND_QUEUE_CREATION_FAILED");
   Serial.println("Restarting ESP32...");
   Serial.flush();
   ESP.restart();
 }


 BaseType_t OLEDtaskResult = xTaskCreatePinnedToCore(
                           oledTextTask,
                           "OLED Text Task",
                           4096,
                           nullptr,
                           1, // priority
                           nullptr,
                           CORE_0
                           );


 if (OLEDtaskResult != pdPASS) {
   Serial.println("Failed to create OLED task. err code: ERR_CODE_OLED_TASK_CREATION_FAILED");
   Serial.println("Restarting ESP32...");
   Serial.flush();
   ESP.restart();
   }


 BaseType_t TemperaturetaskResult = xTaskCreatePinnedToCore(
                             temperatureTask,
                             "Temperature Task",
                             16384,
                             nullptr,
                             1, // priorityj
                             nullptr,
                             CORE_0
                             );


 if (TemperaturetaskResult != pdPASS) {
      Serial.println("Failed to create Temperature task. err code: ERR_CODE_TEMP_TASK_CREATION_FAILED");
      Serial.println("Restarting ESP32...");
      Serial.flush();
      ESP.restart();
   }


 BaseType_t CapacityTaskResult = xTaskCreatePinnedToCore(
                             capacityTask,
                             "Capacity task",
                             4096,
                             nullptr,
                             1,
                             nullptr,
                             CORE_0
 );


 if (CapacityTaskResult != pdPASS){
   Serial.println("Failed to create capacity task. err code: ERR_CODE_CAPACITY_TASK_CREATION_FAILED");
   Serial.println("Restarting ESP32...");
   Serial.flush();
   ESP.restart();
 }

 BaseType_t alarmTaskResult = xTaskCreatePinnedToCore(
                              alarmTask,
                              "Alarm Task",
                              2048,
                              nullptr,
                              1,
                              &alarmTaskHandle,
                              CORE_0
);

if (alarmTaskResult != pdPASS) {
    Serial.println("Failed to create alarm task. err code: ERR_CODE_ALARM_TASK_CREATION_FAILED");
    Serial.println("Restarting ESP32...");
    Serial.flush();
    ESP.restart();
}

  // TO-DO: Also initialize capacity sensors here
 // Will also need to set a GPIO pin for the heating pad control
}


void loop()
{

}
/*
1. OLED display inits
2. Bluetooth init
Once those are initialized we can then proceed with the main logic
*/
