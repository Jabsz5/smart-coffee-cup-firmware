#pragma once

#include <Arduino.h>
#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

struct SensorData {
    float activeTempF;
    float ambientTempF;
    float plateTempF;
    float weight;
};

// Retrieve a complete snapshot of sensor data
SensorData getSensorData();

// Update individual sensor measurements
void updateTemperatureData(float activeF, float ambientF);
void updatePlateTemperatureData(float plateF);
void updateLoadCellData(float weight);