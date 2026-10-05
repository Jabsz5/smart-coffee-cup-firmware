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
#include "photoProtocol.h"


#define MONITOR_SPEED 115200
#define DEFAULT_DELAY 500


void setup() {
 Serial.begin(MONITOR_SPEED);
 delay(DEFAULT_DELAY);
 Serial.println("Hello World! Starting ESP32 BLE + OLED test...");

 // =======================================================
// Hardware initialization
// =======================================================

// We can only check for SPI errors
// Hardware validation can only be done with exposed MISO pin
if (OLEDinit() != EXT_CODE_SUCCESS) {
    Serial.println("OLED init failed!");
    Serial.printf("Error code: 0x%04X\n", ERR_CODE_SPI_INIT_FAILED);
    Serial.flush();

}

// Again, can only test ESP32 side
if (bootupScreen() != EXT_CODE_SUCCESS) {
    printError("Bootup screen failed!", ERR_CODE_BOOTUP_SCREEN_FAILED);
}

// Disables heater pin after 5 failed attempts
if (temperatureSensorInit() != EXT_CODE_SUCCESS) {
    printError("Temperature sensors failed to initialize!", ERR_CODE_TEMP_SENSOR_INIT_FAILED);
}

// I'm not sure what this actuallly is... (???)
plateTemperatureSensorInit();

if (buzzerGPIOinit() != EXT_CODE_SUCCESS) {
    printError("Buzzer GPIO initialization failed!", ERR_CODE_BUZZER_INIT_FAIL);
}

// BLE stack is disabled after 5 failed attempts
if (bluetoothinit() != EXT_CODE_SUCCESS) {
    printError("Bluetooth initialization failed!", ERR_CODE_BLUETOOTH_INIT_FAILED);
}


// =======================================================
// Software initialization
// =======================================================

displayQueue = xQueueCreate(5, sizeof(displayMessage_t));

if (displayQueue == nullptr) {
    printError("Failed to create display queue.", ERR_CODE_DISPLAY_QUEUE_CREATION_FAILED);
}


temperatureCommandQueue = xQueueCreate(1, sizeof(uint8_t));

if (temperatureCommandQueue == nullptr) {
    printError("Failed to create temperature command queue.", ERR_CODE_TEMPERATURE_COMMAND_QUEUE_CREATION_FAILED);
}


capacityCommandQueue = xQueueCreate(1, sizeof(uint8_t));

if (capacityCommandQueue == nullptr) {
    printError("Failed to create capacity command queue.", ERR_CODE_CAPACITY_QUEUE_CREATION_FAILED);
}

uploadPhotoCommandQueue = xQueueCreate(1, sizeof(uint8_t));

if (uploadPhotoCommandQueue == nullptr) {
    printError("Failed to create photo command queue.", ERR_CODE_PHOTO_QUEUE_CREATION_FAILED);
}

photoPacketQueue = xQueueCreate(10, sizeof(PhotoPacketMessage));

if (photoPacketQueue == nullptr) {
    printError("Failed to create photo packet queue.", ERR_CODE_PHOTO_PACKET_QUEUE_CREATION_FAILED);
}
// =======================================================
// FreeRTOS task creation
// =======================================================

BaseType_t displayTaskResult = xTaskCreatePinnedToCore(
    displayTask,
    "Display Task",
    4096,
    nullptr,
    1,
    nullptr,
    CORE_0
);

if (displayTaskResult != pdPASS) {
    printError("Failed to create display task.", ERR_CODE_DISPLAY_TASK_CREATION_FAILED);
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

BaseType_t plateTemperatureTaskResult =
    xTaskCreatePinnedToCore(
        plateTemperatureTask,
        "Plate Temperature Task",
        3072,
        nullptr,
        1,
        nullptr,
        CORE_0
    );

if (plateTemperatureTaskResult != pdPASS) {
    printError("Failed to create plate temperature task.", ERR_CODE_TEMPERATURE_TASK_CREATION_FAILED);
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

BaseType_t uploadPhotoTaskResult =
    xTaskCreatePinnedToCore(
        uploadPhotoTask,
        "Upload Photo Task",
        8192,
        nullptr,
        1,
        nullptr,
        CORE_0
    );

if (uploadPhotoTaskResult != pdPASS) {
    printError(
        "Failed to create upload photo task.",
        ERR_CODE_PHOTO_TASK_CREATION_FAILED
    );
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
