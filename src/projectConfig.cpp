#include "smartCupConfig.h"

bool deviceConnected = false;
bool HEATER_ENABLED = false;
uint8_t temperatureControlValue = 0xFF;
uint8_t capacityControlValue = 0xFF;

float currentWeight = NAN;
portMUX_TYPE loadCellMux = portMUX_INITIALIZER_UNLOCKED;


BLECharacteristic* displayCharacteristic = nullptr;
BLECharacteristic* heatingPadCharacteristic = nullptr;
BLECharacteristic* temperatureCharacteristic = nullptr;
BLECharacteristic* plateTemperatureCharacteristic = nullptr;
BLECharacteristic* capacityCharacteristic = nullptr;
BLECharacteristic* photoCharacteristic = nullptr;
BLECharacteristic* displayControlCharacteristic = nullptr;

QueueHandle_t displayQueue = nullptr;
QueueHandle_t temperatureCommandQueue = nullptr;
QueueHandle_t plateTemperatureCommandQueue = nullptr;
QueueHandle_t capacityCommandQueue = nullptr;
QueueHandle_t uploadPhotoCommandQueue = nullptr;
QueueHandle_t photoPacketQueue = nullptr;

TaskHandle_t alarmTaskHandle = nullptr;