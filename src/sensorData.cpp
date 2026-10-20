#include "sensorData.h"

// Shared sensor data
static SensorData sensorData = {
    NAN,    // Active temperature
    NAN,    // Ambient temperature
    NAN,    // Plate temperature
    NAN     // Weight
};

// FreeRTOS spinlock for shared sensor data
static portMUX_TYPE sensorDataMux = portMUX_INITIALIZER_UNLOCKED;

/*
 * ==========================================
 * GET SENSOR DATA
 * ==========================================
 */

SensorData getSensorData() {
    SensorData snapshot;

    portENTER_CRITICAL(&sensorDataMux);
    snapshot = sensorData;
    portEXIT_CRITICAL(&sensorDataMux);

    return snapshot;
}

/*
 * ==========================================
 * UPDATE DS18B20 TEMPERATURES
 * ==========================================
 */

void updateTemperatureData(float activeF, float ambientF) {
    portENTER_CRITICAL(&sensorDataMux);

    sensorData.activeTempF = activeF;
    sensorData.ambientTempF = ambientF;

    portEXIT_CRITICAL(&sensorDataMux);
}

/*
 * ==========================================
 * UPDATE PLATE TEMPERATURE
 * ==========================================
 */

void updatePlateTemperatureData(float plateF) {
    portENTER_CRITICAL(&sensorDataMux);

    sensorData.plateTempF = plateF;

    portEXIT_CRITICAL(&sensorDataMux);
}

/*
 * ==========================================
 * UPDATE LOAD CELL
 * ==========================================
 */

void updateLoadCellData(float weight) {
    portENTER_CRITICAL(&sensorDataMux);

    sensorData.weight = weight;

    portEXIT_CRITICAL(&sensorDataMux);
}