#include "smartCupConfig.h"

bool deviceConnected = false;

uint8_t temperatureControlValue = 0xFF;
uint8_t capacityControlValue = 0xFF;

BLECharacteristic* oledTextCharacteristic = nullptr;
BLECharacteristic* heatingPadCharacteristic = nullptr;
BLECharacteristic* temperatureCharacteristic = nullptr;
BLECharacteristic* capacityCharacteristic = nullptr;
BLECharacteristic* photoCharacteristic = nullptr;

QueueHandle_t oledTextQueue = nullptr;
QueueHandle_t temperatureCommandQueue = nullptr;
QueueHandle_t capacityCommandQueue = nullptr;

TaskHandle_t alarmTaskHandle = nullptr;