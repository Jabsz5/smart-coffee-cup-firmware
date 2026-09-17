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


struct OledTextMessage {
    char text[OLED_TEXT_MAX_LENGTH];
};

// BLE server connection callbacks
class MyServerCallbacks : public BLEServerCallbacks {
public:
    void onConnect(BLEServer* server) override;
    void onDisconnect(BLEServer* server) override;
};

// BLE OLED characteristic callback
class OledTextCallbacks : public BLECharacteristicCallbacks {
public:
    void onWrite(BLECharacteristic* characteristic) override;
};

// BLE temperature characteristic callback
class TemperatureCallbacks : public BLECharacteristicCallbacks {
public:
    void onWrite(BLECharacteristic* characteristic) override;
};

// BLE capacity characteristic callback
class CapacityCallbacks : public BLECharacteristicCallbacks {
public:
    void onWrite(BLECharacteristic* characteristic) override;
};

class PhotoCallbacks : public BLECharacteristicCallbacks {
public:
    void onWrite(BLECharacteristic* characteristic) override;
};

// Driver objects defined in drivers.cpp
extern Adafruit_ST7789 display;
extern OledTextCallbacks oledTextCallbacks;
extern TemperatureCallbacks temperatureCallbackHandler;
extern CapacityCallbacks capacityCallbackHandler;
extern OneWire activeOneWire;
extern OneWire ambientOneWire;
extern DallasTemperature activeSensor;
extern DallasTemperature ambientSensor;

// FreeRTOS queues defined in projectConfig.cpp
extern QueueHandle_t oledTextQueue;
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
void showOLEDMessage(const char* message);
void printError(const char* errorMessage, uint16_t errorCode);

// Bluetooth driver function
int bluetoothinit();

// DS18B20 driver function
int temperatureSensorInit();

// buzzer driver function
int buzzerGPIOinit();
void soundAlarm();

// FreeRTOS task functions
void oledTextTask(void* parameter);
void temperatureTask(void* parameter);
void capacityTask(void* parameter);
void alarmTask(void* parameter);