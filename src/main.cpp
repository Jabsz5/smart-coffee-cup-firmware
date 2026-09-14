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

 // =======================================================
// Hardware initialization
// =======================================================

// The display cannot report its own initialization failure,
// so this particular error is printed only to Serial.
if (OLEDinit() != EXT_CODE_SUCCESS) {
    Serial.println("OLED init failed!");
    Serial.printf("Error code: 0x%04X\n", ERR_CODE_OLED_INIT_FAILED);
    Serial.println("Restarting ESP32...");
    Serial.flush();

    // ESP.restart();
}

if (bootupScreen() != EXT_CODE_SUCCESS) {
    printError("Bootup screen failed!", ERR_CODE_BOOTUP_SCREEN_FAILED);
    // ESP.restart();
}

if (temperatureSensorInit() != EXT_CODE_SUCCESS) {
    printError("Temperature sensors failed to initialize!", ERR_CODE_TEMP_SENSOR_INIT_FAILED);
    // ESP.restart();
}

if (buzzerGPIOinit() != EXT_CODE_SUCCESS) {
    printError("Buzzer GPIO initialization failed!", ERR_CODE_BUZZER_INIT_FAIL);
    // ESP.restart();
}

if (bluetoothinit() != EXT_CODE_SUCCESS) {
    printError("Bluetooth initialization failed!", ERR_CODE_BLUETOOTH_INIT_FAILED);
    // ESP.restart();
}


// =======================================================
// Software initialization
// =======================================================

oledTextQueue = xQueueCreate(5,sizeof(OledTextMessage));

if (oledTextQueue == nullptr) {
    printError("Failed to create OLED text queue.", ERR_CODE_OLED_QUEUE_CREATION_FAILED);
    // ESP.restart();
}


temperatureCommandQueue = xQueueCreate(1, sizeof(uint8_t));

if (temperatureCommandQueue == nullptr) {
    printError("Failed to create temperature command queue.", ERR_CODE_TEMPERATURE_COMMAND_QUEUE_CREATION_FAILED);
    // ESP.restart();
}


capacityCommandQueue = xQueueCreate(1, sizeof(uint8_t));

if (capacityCommandQueue == nullptr) {
    printError("Failed to create capacity command queue.", ERR_CODE_CAPACITY_QUEUE_CREATION_FAILED);
    // ESP.restart();
}


// =======================================================
// FreeRTOS task creation
// =======================================================

BaseType_t OLEDtaskResult = xTaskCreatePinnedToCore(
    oledTextTask,
    "OLED Text Task",
    4096,
    nullptr,
    1,
    nullptr,
    CORE_0
);

if (OLEDtaskResult != pdPASS) {
    printError("Failed to create OLED task.", ERR_CODE_OLED_TASK_CREATION_FAILED);
    // ESP.restart();
}


BaseType_t TemperaturetaskResult = xTaskCreatePinnedToCore(
    temperatureTask,
    "Temperature Task",
    16384,
    nullptr,
    1,
    nullptr,
    CORE_0
);

if (TemperaturetaskResult != pdPASS) {
    printError("Failed to create temperature task.", ERR_CODE_TEMPERATURE_TASK_CREATION_FAILED);
    // ESP.restart();
}


BaseType_t CapacityTaskResult = xTaskCreatePinnedToCore(
    capacityTask,
    "Capacity Task",
    4096,
    nullptr,
    1,
    nullptr,
    CORE_0
);

if (CapacityTaskResult != pdPASS) {
    printError("Failed to create capacity task.", ERR_CODE_CAPACITY_TASK_CREATION_FAILED);
    // ESP.restart();
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
    printError("Failed to create alarm task.", ERR_CODE_ALARM_TASK_CREATION_FAILED);
    // ESP.restart();
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
