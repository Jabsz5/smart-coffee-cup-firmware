#include "smartCupConfig.h"

bool deviceConnected = false;

uint8_t temperatureControlValue = 0xFF;
uint8_t capacityControlValue = 0xFF;

BLECharacteristic* displayCharacteristic = nullptr;
BLECharacteristic* heatingPadCharacteristic = nullptr;
BLECharacteristic* temperatureCharacteristic = nullptr;
BLECharacteristic* capacityCharacteristic = nullptr;
BLECharacteristic* photoCharacteristic = nullptr;

QueueHandle_t displayQueue = nullptr;
QueueHandle_t temperatureCommandQueue = nullptr;
QueueHandle_t capacityCommandQueue = nullptr;
QueueHandle_t uploadPhotoCommandQueue = nullptr;
QueueHandle_t photoPacketQueue = nullptr;

TaskHandle_t alarmTaskHandle = nullptr;