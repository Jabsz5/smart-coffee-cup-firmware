#pragma once

// FreeRTOS task entry points implemented in smartcoffeecupTasks.cpp.
void displayTask(void* parameter);

void temperatureSensorTask(void* parameter);
void temperatureCommandTask(void* parameter);

void loadCellSensorTask(void* parameter);

void plateTemperatureTask(void* parameter);
void plateTemperatureCommandTask(void* parameter);

void capacityTask(void* parameter);
void alarmTask(void* parameter);
void uploadPhotoTask(void* parameter);
