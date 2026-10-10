#pragma once

#include <BLEServer.h>
#include <BLECharacteristic.h>

class MyServerCallbacks : public BLEServerCallbacks {
public:
    void onConnect(BLEServer* server) override;
    void onDisconnect(BLEServer* server) override;
};

class DisplayCallbacks : public BLECharacteristicCallbacks {
public:
    void onWrite(BLECharacteristic* characteristic) override;
};

class DisplayControlCallbacks : public BLECharacteristicCallbacks {
public:
    void onWrite(BLECharacteristic* characteristic) override;
};

class TemperatureCallbacks : public BLECharacteristicCallbacks {
public:
    void onWrite(BLECharacteristic* characteristic) override;
};

class CapacityCallbacks : public BLECharacteristicCallbacks {
public:
    void onWrite(BLECharacteristic* characteristic) override;
};

class PhotoCallbacks : public BLECharacteristicCallbacks {
public:
    void onWrite(BLECharacteristic* characteristic) override;
};

/*
 * Callback objects are defined in bluetoothCallbacks.cpp.
 */
extern MyServerCallbacks serverCallbacks;
extern DisplayCallbacks displayCallbacks;
extern DisplayControlCallbacks displayControlCallbackHandler;
extern TemperatureCallbacks temperatureCallbackHandler;
extern CapacityCallbacks capacityCallbackHandler;
extern PhotoCallbacks photoCallbackHandler;