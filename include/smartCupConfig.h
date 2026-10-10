#pragma once

#include <Arduino.h>
#include <BLEServer.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// ============================================================
// Heater configuration
// ============================================================

constexpr int HEATER_PIN = 32;
extern bool HEATER_ENABLED;
// ============================================================
// OLED configuration
// ============================================================

constexpr int SCREEN_WIDTH = 128;
constexpr int SCREEN_HEIGHT = 128;
constexpr int OLED_RESET = -1;

constexpr int I2C_SDA = 6;
constexpr int I2C_SCL = 7;

constexpr size_t OLED_TEXT_MAX_LENGTH = 128;

// ============================================================
// Temperature sensor configuration
// ============================================================
// DS18B20 data pins
constexpr uint8_t ACTIVE_SENSOR_PIN  = 18;
constexpr uint8_t AMBIENT_SENSOR_PIN = 17;

constexpr unsigned long READ_INTERVAL_MS = 2000;
constexpr unsigned long CONVERSION_TIME_MS = 750;

// ============================================================
// buzzer configuration
// ============================================================
#define BUZZER_PIN  16
#define ALARM_TEMP_F       60.0
#define REARM_TEMP_F       70.0
#define BUZZER_FREQUENCY   200
#define BUZZER_DURATION_MS 20000

constexpr float LOW_TEMP_ALARM_F = 60.0f;
constexpr float LOW_TEMP_REARM_F = 65.0f;

constexpr uint32_t TEMPERATURE_CHECK_INTERVAL_MS = 1000;
constexpr uint32_t ALARM_DURATION_MS = 20000;
extern TaskHandle_t alarmTaskHandle;
extern bool alarmArmed;

// --------------------------------------------------
// Plate thermistor
// --------------------------------------------------
constexpr uint8_t THERMISTOR_PIN = 34;

constexpr float SERIES_RESISTOR = 10000.0f;
constexpr float THERMISTOR_NOMINAL = 10000.0f;
constexpr float TEMP_NOMINAL_C = 25.0f;
constexpr float THERMISTOR_BETA = 3950.0f;
constexpr float ADC_MAX_VALUE = 4095.0f;

// Carried over from the test program.
// This should eventually be recalibrated against a trusted thermometer.
constexpr float PLATE_TEMP_CORRECTION_F = 138.5f;

constexpr uint32_t PLATE_UPDATE_INTERVAL_MS = 50;

// ============================================================
// BLE UUIDs
// ============================================================

constexpr const char SERVICE_UUID[] =
    "12345678-1234-1234-1234-1234567890ab";

constexpr const char OLED_TEXT_CHAR_UUID[] =
    "abcd1234-5678-90ab-cdef-1234567890ab";

constexpr const char HEATING_PAD_CHAR_UUID[] =
    "abcd1234-5678-90ab-cdef-1234567890ac";

constexpr const char TEMPERATURE_CHAR_UUID[] =
    "abcd1234-5678-90ab-cdef-1234567890ad";

constexpr const char CAPACITY_CHAR_UUID[] =
    "abcd1234-5678-90ab-cdef-1234567890ae";

constexpr const char PHOTO_UPLOAD_UUID[] = 
    "abcd1234-5678-90ab-cdef-1234567890af";

constexpr const char DISPLAY_CONTROL_UUID[] =
    "abcd1234-5678-90ab-cdef-1234567890b0";

// ============================================================
// Status codes
// ============================================================

constexpr int EXT_CODE_SUCCESS = 0;
constexpr int ERR_CODE_OLED_INIT_FAILED = 1;

constexpr int ERR_CODE_BOOTUP_SCREEN_FAILED = 2;
constexpr int ERR_CODE_BLUETOOTH_INIT_FAILED = 3;
constexpr int ERR_CODE_DISPLAY_QUEUE_CREATION_FAILED = 4;
constexpr int ERR_CODE_TEMPERATURE_COMMAND_QUEUE_CREATION_FAILED = 5;
constexpr int ERR_CODE_DISPLAY_TASK_CREATION_FAILED = 6;
constexpr int ERR_CODE_TEMPERATURE_TASK_CREATION_FAILED = 7;
constexpr int ERR_CODE_TEMPERATURE_COMMAND_RECEIVE_FAILED = 8;
constexpr int ERR_CODE_CAPACITY_COMMAND_RECEIVE_FAILED = 9;
constexpr int ERR_CODE_CAPACITY_QUEUE_CREATION_FAILED = 10;
constexpr int ERR_CODE_CAPACITY_TASK_CREATION_FAILED = 11;
constexpr int ERR_CODE_PHOTO_QUEUE_CREATION_FAILED = 15;
constexpr int ERR_CODE_TEMP_SENSOR_INIT_FAILED = 12;
constexpr int ERR_CODE_BUZZER_INIT_FAIL = 13;
constexpr int ERR_CODE_ALARM_TASK_CREATION_FAILED = 14;
constexpr int ERR_CODE_PHOTO_TASK_CREATION_FAILED = 16;
constexpr int ERR_CODE_PHOTO_PACKET_QUEUE_CREATION_FAILED = 17;
constexpr int ERR_CODE_SPI_INIT_FAILED = 18;
constexpr int ERR_CODE_FAILED_WRITE_TO_DISPLAY = 19;
constexpr int ERR_CODE_PLATE_TEMP_SENSOR_INIT_FAILED = 20;
// ============================================================
// ESP32 core assignments
// ============================================================

constexpr BaseType_t CORE_0 = 0;
constexpr BaseType_t CORE_1 = 1;

// ============================================================
// Mobile application commands
// ============================================================

constexpr uint8_t HEATING_PAD_OFF = 0;
constexpr uint8_t HEATING_PAD_ON = 1;
constexpr uint8_t CHECK_TEMPERATURE_COMMAND = 5;
constexpr uint8_t CHECK_CAPACITY_COMMAND = 6;
constexpr uint8_t VIEW_SENSOR_DASHBOARD_COMMAND = 42;


// ============================================================
// Shared runtime state
// ============================================================

extern bool deviceConnected;
extern uint8_t temperatureControlValue;
extern uint8_t capacityControlValue;
// ============================================================
// BLE characteristic pointers
// ============================================================

extern BLECharacteristic* displayCharacteristic;
extern BLECharacteristic* heatingPadCharacteristic;
extern BLECharacteristic* temperatureCharacteristic;
extern BLECharacteristic* capacityCharacteristic;
extern BLECharacteristic* displayControlCharacteristic;

// ============================================================
// BLE callback handlers
// ============================================================


// ============================================================
// FreeRTOS queues
// ============================================================

extern QueueHandle_t oledTextQueue;
extern QueueHandle_t temperatureCommandQueue;
extern QueueHandle_t uploadPhotoCommandQueue;
extern QueueHandle_t photoPacketQueue;