#pragma once

#include <Arduino.h>
#include <Adafruit_ST7789.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "smartCupConfig.h"

// CODE FOR TEMPERATURE SENSORS
#include <OneWire.h>
#include <DallasTemperature.h>



struct displayMessage_t {
    char text[OLED_TEXT_MAX_LENGTH];
};

enum class DisplayMode : uint8_t {
    Initialization,
    SensorDashboard,
    Text,
    Photo,
    Drawing
};

extern volatile DisplayMode currentDisplayMode;

// Driver objects defined in drivers.cpp
extern Adafruit_ST7789 display;

extern OneWire activeOneWire;
extern OneWire ambientOneWire;
extern DallasTemperature activeSensor;
extern DallasTemperature ambientSensor;

// FreeRTOS queues defined in projectConfig.cpp
extern QueueHandle_t displayQueue;
extern QueueHandle_t temperatureCommandQueue;
extern QueueHandle_t capacityCommandQueue;

// BLE characteristics defined in projectConfig.cpp
extern BLECharacteristic* oledTextCharacteristic;
extern BLECharacteristic* heatingPadCharacteristic;
extern BLECharacteristic* temperatureCharacteristic;
extern BLECharacteristic* capacityCharacteristic;
extern BLECharacteristic* photoCharacteristic;

// Shared BLE and temperature state
extern bool deviceConnected;
extern uint8_t temperatureControlValue;

// OLED driver functions
int OLEDinit();
int bootupScreen();
void displaySettingStartup();
void displayMessage(const char* message);
void printError(const char* errorMessage, uint16_t errorCode);

// Bluetooth driver function
int bluetoothinit();

// DS18B20 driver function
int temperatureSensorInit();

// buzzer driver function
int buzzerGPIOinit();
void soundAlarm();

// temperature plates driver functions
void plateTemperatureSensorInit();

float readPlateTemperature();
float getPlateTemperatureF();

void plateTemperatureTask(void* parameters);

// FreeRTOS task functions
void displayTask(void* parameter);
void temperatureTask(void* parameter);
void capacityTask(void* parameter);
void alarmTask(void* parameter);
void uploadPhotoTask(void* parameter);