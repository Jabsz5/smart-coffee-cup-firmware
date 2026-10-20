#include "smartcoffeecupTasks.h"
#include "drivers.h"
#include "smartCupConfig.h"
#include "photoProtocol.h"
#include "displayController.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <cmath>
#include "sensorData.h"


void plateTemperatureTask(void* parameters) {

    bool tempPlateErrorReported = false;
    TickType_t lastWakeTime = xTaskGetTickCount();

    Serial.println("Plate temperature task started");

    while (true) {
        const float newPlateTempF = readPlateTemperature();

        // Validate temperature reading
        const bool validTemperature = std::isfinite(newPlateTempF) && newPlateTempF <= 400.0f;

        updatePlateTemperatureData(validTemperature ? newPlateTempF : NAN);

        if (validTemperature) {
            tempPlateErrorReported = false;

            Serial.printf(
                "Plate temperature: %.1f F\n",
                newPlateTempF
            );
        }
        else if (!tempPlateErrorReported) {
            Serial.printf(
                "ERROR: Invalid plate temperature reading: %.1f F\n",
                newPlateTempF
            );

            tempPlateErrorReported = true;
        }

        vTaskDelayUntil(
            &lastWakeTime,
            pdMS_TO_TICKS(PLATE_UPDATE_INTERVAL_MS)
        );
    }
}

/*
 * ============================================================
 * PLATE TEMPERATURE COMMAND TASK
 * ============================================================
 */

void plateTemperatureCommandTask(void* parameter) {
    uint8_t command = 0;
    uint8_t temperatureBytes[sizeof(float)];

    Serial.printf("Plate temperature command task started on Core %d\n", xPortGetCoreID());

    while (true) {
        // Wait for BLE command
        if (xQueueReceive(plateTemperatureCommandQueue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        Serial.printf("Processing plate temperature command: %u\n", command);

        if (command != CHECK_PLATE_TEMPERATURE_COMMAND) {
            continue;
        }

        // Retrieve latest plate temperature
        const SensorData sensorSnapshot = getSensorData();
        const float plateTempF = sensorSnapshot.plateTempF;

        // Validate measurement
        if (!std::isfinite(plateTempF)) {
            Serial.println("ERROR: Plate temperature sensor unavailable.");
            continue;
        }

        // Check BLE connection
        if (!deviceConnected || plateTemperatureCharacteristic == nullptr) {
            Serial.println("ERROR: Plate temperature BLE characteristic unavailable.");
            continue;
        }

        // Convert float to 4-byte buffer
        memcpy(temperatureBytes, &plateTempF, sizeof(float));

        // Send plate temperature to mobile app
        plateTemperatureCharacteristic->setValue(
            temperatureBytes,
            sizeof(temperatureBytes)
        );

        plateTemperatureCharacteristic->notify();

        Serial.printf("Sent plate temperature to mobile app: %.2f F\n", plateTempF);
    }
}

// ===== displayTask =====
void drawSensorDisplayLayout() {
    display.fillScreen(ST77XX_BLACK);
    display.setTextWrap(false);
    display.setTextSize(3);
    display.setTextColor(ST77XX_CYAN);
    display.setCursor(25, 15);
    display.print("SMART CUP");
    display.drawFastHLine(
        10,
        50,
        display.width() - 20,
        ST77XX_WHITE
    );
    display.setTextSize(2);
    display.setTextColor(ST77XX_YELLOW);
    display.setCursor(10, 75);
    display.print("Plate:");
    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 120);
    display.print("Liquid:");
    display.setTextColor(ST77XX_CYAN);
    display.setCursor(10, 165);
    display.print("Ambient:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(10, 210);
    display.print("Weight:");
}

void drawSensorValue(int16_t y, uint16_t color, float value, const char* unit){
    /*
     * Clear only the region containing the old number.
     * Do not clear the entire display.
     */
    display.fillRect(115, y, 120, 24, ST77XX_BLACK);
    display.setTextSize(2);
    display.setTextColor(color);
    display.setCursor(115, y);

    if (isfinite(value)) {
        display.print(value, 1);
        display.print(" ");
        display.print(unit);
    } else {
        display.print("---");
    }
}

void updateSensorDisplay() {
    // Retrieve latest sensor readings
    const SensorData sensorSnapshot = getSensorData();

    // Update TFT dashboard
    drawSensorValue(75, ST77XX_YELLOW, sensorSnapshot.plateTempF, "F");
    drawSensorValue(120, ST77XX_GREEN, sensorSnapshot.activeTempF, "F");
    drawSensorValue(165, ST77XX_CYAN, sensorSnapshot.ambientTempF, "F");
    drawSensorValue(210, ST77XX_WHITE, sensorSnapshot.weight, "g");
}
/*
Two different behaviors occuring;
1. State transition
2. State update while continuously in said state.
*/
void displayTask(void* parameter) {

    constexpr TickType_t DISPLAY_INTERVAL = pdMS_TO_TICKS(100);
    TickType_t lastWakeTime = xTaskGetTickCount();
    DisplayMode previousDisplayMode = currentDisplayMode;
    Serial.printf("Display task started on Core %d\n", xPortGetCoreID());

    while (true) {
        const DisplayMode mode = currentDisplayMode;
        if (mode != previousDisplayMode) {
            switch (mode) {
                case DisplayMode::Initialization:
                    Serial.println("Display mode default is Initialization mode");
                    break;
                case DisplayMode::SensorDashboard:
                    Serial.println("Switching display to Sensor Dashboard mode");
                    drawSensorDisplayLayout();
                    break;
                case DisplayMode::Text:
                    // Draw text screen later.
                    break;
                case DisplayMode::Photo:
                    // The upload task draws rows as DATA packets arrive.
                    Serial.println("Switching display to Photo mode");
                    break;
                case DisplayMode::Drawing:
                    // Draw drawing screen later.
                    break;
            }
            previousDisplayMode = mode;
        }
        // Continuously refresh the sensor dashboard.
        if (mode == DisplayMode::SensorDashboard) {
            updateSensorDisplay();
        }
        vTaskDelayUntil(&lastWakeTime, DISPLAY_INTERVAL);
    }
}

// ===== temperatureTask =====
#define DO_NOT_STOP_HERE 0
// Read sensors, validate readings, update shared temp variables,
// detect temperature, trigger buzzer, notify phone.
void temperatureSensorTask(void* parameter) {
    bool highTempAlarmArmed = true;

    const float HIGH_TEMP_ALARM_F = 150.0f;
    const float HIGH_TEMP_REARM_F = 145.0f;

    TickType_t lastWakeTime = xTaskGetTickCount();

    Serial.printf("Temperature sensor task started on Core %d\n", xPortGetCoreID());

    while (true) {
        // Request temperature readings
        ambientSensor.requestTemperatures();
        activeSensor.requestTemperatures();

        const float newAmbientF = ambientSensor.getTempFByIndex(0);
        const float newProbeF = activeSensor.getTempFByIndex(0);

        // Validate against DS18B20 operating range
        const bool ambientValid =
            std::isfinite(newAmbientF) &&
            newAmbientF >= -67.0f &&
            newAmbientF <= 257.0f;

        const bool probeValid =
            std::isfinite(newProbeF) &&
            newProbeF >= -67.0f &&
            newProbeF <= 257.0f;

        // Update shared temperature values
        updateTemperatureData(probeValid ? newProbeF : NAN, ambientValid ? newAmbientF : NAN);

        // Print valid temperature readings
        if (probeValid) {
            Serial.printf("Active Temperature: %.2f F\n", newProbeF);
        }

        if (ambientValid) {
            Serial.printf("Ambient Temperature: %.2f F\n", newAmbientF);
        }

        // Ignore invalid probe readings
        if (probeValid) {

            /*
             * ==========================================
             * HIGH-TEMPERATURE ALARM
             * ==========================================
             */

            if (highTempAlarmArmed && newProbeF > HIGH_TEMP_ALARM_F) {
                Serial.printf("HIGH TEMP ALERT: %.2f F is above %.2f F\n",
                              newProbeF, HIGH_TEMP_ALARM_F);

                // Notify mobile app
                if (deviceConnected && temperatureCharacteristic != nullptr) {
                    uint8_t temperatureBytes[sizeof(float)];

                    memcpy(temperatureBytes, &newProbeF, sizeof(temperatureBytes));
                    temperatureCharacteristic->setValue(temperatureBytes, sizeof(temperatureBytes));
                    temperatureCharacteristic->notify();
                }

                // Activate buzzer
                if (alarmTaskHandle != nullptr) {
                    xTaskNotifyGive(alarmTaskHandle);
                }

                highTempAlarmArmed = false;
            }
            else if (!highTempAlarmArmed && newProbeF <= HIGH_TEMP_REARM_F) {
                Serial.printf("High-temperature alarm rearmed at %.2f F\n", newProbeF);
                highTempAlarmArmed = true;
            }
        }

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TEMPERATURE_CHECK_INTERVAL_MS));
    }
}

void temperatureCommandTask(void* parameter) {
    uint8_t command = 0;
    uint8_t temperatureBytes[sizeof(float) * 2];

    Serial.printf("Temperature command task started on Core %d\n", xPortGetCoreID());

    while (true) {
        // Wait until a command is received
        if (xQueueReceive(temperatureCommandQueue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        Serial.printf("Processing temperature command: %u\n", command);

        if (command != CHECK_TEMPERATURE_COMMAND) {
            continue;
        }

        // Retrieve latest temperature readings
        const SensorData sensorSnapshot = getSensorData();

        const float activeTempF = sensorSnapshot.activeTempF;
        const float ambientTempF = sensorSnapshot.ambientTempF;

        // Validate temperatures
        if (!std::isfinite(activeTempF) || !std::isfinite(ambientTempF)) {
            Serial.println("ERROR: One or more temperature sensors unavailable");
            continue;
        }

        if (!deviceConnected || temperatureCharacteristic == nullptr) {
            Serial.println("ERROR: Temperature BLE characteristic unavailable");
            continue;
        }

        // First 4 bytes: Active temperature
        memcpy(
            temperatureBytes,
            &activeTempF,
            sizeof(float)
        );

        // Next 4 bytes: Ambient temperature
        memcpy(
            temperatureBytes + sizeof(float),
            &ambientTempF,
            sizeof(float)
        );

        // Send both temperatures in one BLE notification
        temperatureCharacteristic->setValue(
            temperatureBytes,
            sizeof(temperatureBytes)
        );

        temperatureCharacteristic->notify();

        Serial.printf(
            "Sent temperatures to mobile app | Active: %.2f F | Ambient: %.2f F\n",
            activeTempF,
            ambientTempF
        );
    }
}

/*
Only this should call scale and its methods!
Then we implement the mobile BLE functionality next, 
the BLE task will read currentWeight, 
not communicate directly with the HX711.
*/
void loadCellSensorTask(void* parameter) {
    TickType_t lastWakeTime = xTaskGetTickCount();

    Serial.printf("Load cell sensor task started on Core %d\n", xPortGetCoreID());

    while (true) {
        float newWeight = NAN;

        // Check if HX711 is responding
        if (scale.wait_ready_timeout(200)) {
            // Read calibrated weight
            newWeight = scale.get_units(1);
        }

        // Validate weight reading
        const bool validWeight = std::isfinite(newWeight);

        // Update shared weight
        updateLoadCellData(validWeight ? newWeight : NAN);

        // Print sensor reading
        if (validWeight) {
            Serial.printf("Load Cell Weight: %.2f\n", newWeight);
        }
        else {
            Serial.println("ERROR: Load cell reading unavailable");
        }

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(LOAD_CELL_UPDATE_INTERVAL_MS));
    }
}

// ===== alarmTask =====
void alarmTask(void *parameter){
    Serial.printf("Alarm task started on Core %d\n\r", xPortGetCoreID());
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    while (1) {
        /*
         * pdTRUE - like a binary semaphore/event
         * pdFALSE - counting semaphore
         *
         * portMAX_DELAY - remain block until notified
         */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        Serial.println("Low-temperature alarm activated.");
        digitalWrite(BUZZER_PIN, HIGH);
        tone(BUZZER_PIN, BUZZER_FREQUENCY);
        delay(BUZZER_DURATION_MS);
        noTone(BUZZER_PIN);
        /*
         * This blocks only alarmTask.
         * temperatureTask and the other tasks continue running.
         */
        vTaskDelay(pdMS_TO_TICKS(ALARM_DURATION_MS));
        digitalWrite(BUZZER_PIN, LOW);
        Serial.println("Low-temperature alarm finished.");
    }
}

// ===== capacityTask =====
void capacityTask(void* parameter) {
    uint8_t command = 0;
    uint8_t capacityBytes[sizeof(float)];

    Serial.printf("Capacity task started on Core %d\n", xPortGetCoreID());

    while (true) {
        // Wait for a capacity command from the mobile app
        if (xQueueReceive(capacityCommandQueue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        Serial.printf("Processing capacity command: %u\n", command);

        if (command != CHECK_CAPACITY_COMMAND) {
            continue;
        }

        // Retrieve latest load cell measurement
        const SensorData sensorSnapshot = getSensorData();
        const float weight = sensorSnapshot.weight;

        // Validate measurement
        if (!std::isfinite(weight)) {
            Serial.println("ERROR: Load cell measurement unavailable.");
            continue;
        }

        // Check BLE connection
        if (!deviceConnected || capacityCharacteristic == nullptr) {
            Serial.println("ERROR: Capacity BLE characteristic unavailable.");
            continue;
        }

        // Convert float to byte array
        memcpy(capacityBytes, &weight, sizeof(float));

        // Send weight to mobile app
        capacityCharacteristic->setValue(capacityBytes, sizeof(capacityBytes));
        capacityCharacteristic->notify();

        Serial.printf("Sent load cell weight to mobile app: %.2f g\n", weight);
    }
}

// ===== uploadPhotoTask =====
void uploadPhotoTask(void* parameter) {
    uint8_t command;
    PhotoPacketMessage packetMessage;
    Serial.printf("Upload photo task started on Core %d\n", xPortGetCoreID());
    
    while (true) {
        /*
         * ==================================
         * WAIT FOR PHOTO UPLOAD REQUEST
         * ==================================
         */
        if (xQueueReceive(uploadPhotoCommandQueue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (command != UPLOAD_PHOTO_COMMAND) {
            continue;
        }
        Serial.println("Beginning photo upload process.");
        /*
         * Stop the sensor dashboard from
         * touching the display.
         */
        currentDisplayMode = DisplayMode::Photo;
        /*
         * ==================================
         * ESP32 IS READY
         * ==================================
         */
        if (deviceConnected && photoCharacteristic != nullptr) {
            uint8_t readyAck = PHOTO_READY_ACK;
            photoCharacteristic->setValue(&readyAck, sizeof(readyAck));
            photoCharacteristic->notify();
            Serial.printf("Photo READY ACK sent: %u\n", readyAck);
        }
        /*
         * ==================================
         * RECEIVE PHOTO PACKETS
         * ==================================
         */
        while (true) {
            if (xQueueReceive(photoPacketQueue, &packetMessage, portMAX_DELAY ) != pdTRUE) {
                continue;
            }
            //Serial.printf("Processing photo packet: type=0x%02X, length=%u\n", packetMessage.data[0], packetMessage.length);
            handleImagePacket(packetMessage.data, packetMessage.length, display);
        }
    }
}