#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_ST7789.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "smartCupConfig.h"

// Temperature sensor libraries
#include <OneWire.h>
#include <DallasTemperature.h>

#include <atomic>
#include <stdint.h>

/*
 * ==========================================
 * DISPLAY CONFIGURATION — CLASSIC ESP32
 * ==========================================
 */

#define TFT_CS    14
#define TFT_DC    5
#define TFT_RST   4
#define TFT_MOSI  13
#define TFT_SCLK  26


/*
 * ==========================================
 * DISPLAY DRIVER OBJECTS
 * ==========================================
 */

// SPI instance defined in drivers.cpp
extern SPIClass displaySPI;

// ST7789 display defined in drivers.cpp
extern Adafruit_ST7789 display;

/*
 * ==========================================
 * TEMPERATURE SENSOR DRIVER OBJECTS
 * ==========================================
 */

extern OneWire activeOneWire;
extern OneWire ambientOneWire;

extern DallasTemperature activeSensor;
extern DallasTemperature ambientSensor;


/*
 * ==========================================
 * FREERTOS QUEUES
 * Defined in projectConfig.cpp
 * ==========================================
 */

struct displayMessage_t {
    char text[OLED_TEXT_MAX_LENGTH];
};

extern QueueHandle_t displayQueue;
extern QueueHandle_t temperatureCommandQueue;
extern QueueHandle_t plateTemperatureCommandQueue;
extern QueueHandle_t capacityCommandQueue;

/*
 * ==========================================
 * BLE CHARACTERISTICS
 * Defined in projectConfig.cpp
 * ==========================================
 */

extern BLECharacteristic* oledTextCharacteristic;
extern BLECharacteristic* heatingPadCharacteristic;
extern BLECharacteristic* temperatureCharacteristic;
extern BLECharacteristic* capacityCharacteristic;
extern BLECharacteristic* photoCharacteristic;

/*
 * ==========================================
 * SHARED BLE AND TEMPERATURE STATE
 * ==========================================
 */

extern bool deviceConnected;
extern uint8_t temperatureControlValue;

/*
 * ==========================================
 * DISPLAY DRIVER FUNCTIONS
 * ==========================================
 */

int OLEDinit();
int bootupScreen();

void displaySettingStartup();
void displayMessage(const char* message);
void printError(const char* errorMessage, uint16_t errorCode);

/*
 * ==========================================
 * BLUETOOTH DRIVER FUNCTIONS
 * ==========================================
 */

int bluetoothinit();

/*
 * ==========================================
 * TEMPERATURE SENSOR DRIVER FUNCTIONS
 * ==========================================
 */

int temperatureSensorInit();

int plateTemperatureSensorInit();

float readPlateTemperature();
float getPlateTemperatureF();

/*
 * ==========================================
 * BUZZER DRIVER FUNCTIONS
 * ==========================================
 */

int buzzerGPIOinit();
void soundAlarm();

/*
 * ==========================================
 * LOAD CELL DRIVER
 * ==========================================
 */
int loadcellInit();

/*
 * ==========================================
 * FREERTOS TASK FUNCTIONS
 * ==========================================
 */

void displayTask(void* parameter);
void temperatureTask(void* parameter);
void plateTemperatureTask(void* parameters);
void capacityTask(void* parameter);
void alarmTask(void* parameter);
void uploadPhotoTask(void* parameter);