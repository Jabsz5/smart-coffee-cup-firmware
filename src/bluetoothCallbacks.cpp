#include <Arduino.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLECharacteristic.h>

#include <cstring>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "smartCupConfig.h"
#include "drivers.h"
#include "bluetoothCallbacks.h"
#include "photoProtocol.h"
#include "displayController.h"

/*
 * ============================================================
 * BLE CALLBACK OBJECTS
 * ============================================================
 */

MyServerCallbacks serverCallbacks;
DisplayCallbacks displayCallbacks;
TemperatureCallbacks temperatureCallbackHandler;
CapacityCallbacks capacityCallbackHandler;
PhotoCallbacks photoCallbackHandler;
DisplayControlCallbacks displayControlCallbackHandler;

/*
 * ============================================================
 * SERVER CALLBACKS
 * ============================================================
 */

void MyServerCallbacks::onConnect(BLEServer* server) {
    deviceConnected = true;
    Serial.println("BLE device connected!");
    displayMessage("Device is\nconnected.");
    Serial.println("Changing display mode to SensorDashboard.");

    currentDisplayMode = DisplayMode::SensorDashboard;
}


void MyServerCallbacks::onDisconnect(BLEServer* server) {
    deviceConnected = false;
    Serial.println("BLE device disconnected!");
    BLEDevice::startAdvertising();

    Serial.println("BLE advertising restarted.");

    displayMessage("Waiting for connection...");
}


/*
 * ============================================================
 * DISPLAY CALLBACK
 * ============================================================
 */

void DisplayCallbacks::onWrite(BLECharacteristic* characteristic) {
    if (characteristic->getUUID().toString() != OLED_TEXT_CHAR_UUID) {
        return;
    }

    std::string textFromApp = characteristic->getValue();

    Serial.printf("BLE onWrite callback running on Core %d\n", xPortGetCoreID());

    if (textFromApp.empty()) {
        return;
    }

    displayMessage_t message{};

    /*
     * Reserve one byte for the null terminator.
     */
    size_t copyLength = textFromApp.length();

    if (copyLength >= OLED_TEXT_MAX_LENGTH) {
        copyLength = OLED_TEXT_MAX_LENGTH - 1;
    }

    memcpy(message.text, textFromApp.data(), copyLength);

    message.text[copyLength] = '\0';

    if (xQueueSend(displayQueue, &message, 0) != pdTRUE) {
        Serial.println("Display queue is full. Message discarded.");
    }
}


/*
 * ============================================================
 * TEMPERATURE CALLBACK
 * ============================================================
 */

void TemperatureCallbacks::onWrite(BLECharacteristic* characteristic) {
    if (characteristic->getUUID().toString() != TEMPERATURE_CHAR_UUID) {
        return;
    }

    std::string temperatureValue = characteristic->getValue();

    if (temperatureValue.empty()) {
        Serial.println("Temperature command was empty.");
        return;
    }

    uint8_t command = static_cast<uint8_t>(static_cast<unsigned char>(temperatureValue[0]));

    temperatureControlValue = command;

    Serial.printf("Temperature control command received: %u\r\n", temperatureControlValue);

    if (command == CHECK_TEMPERATURE_COMMAND) {
        xQueueOverwrite(temperatureCommandQueue, &command);
    }
    else {
        Serial.println("Failed to receive valid. Temperature command.");
    }
}


/*
 * ============================================================
 * CAPACITY CALLBACK
 * ============================================================
 */

void CapacityCallbacks::onWrite(BLECharacteristic* characteristic) {
    if (characteristic->getUUID().toString() != CAPACITY_CHAR_UUID) {
        return;
    }

    std::string capacityValue = characteristic->getValue();

    if (capacityValue.empty()) {
        Serial.println("Capacity command was empty.");
        return;
    }

    uint8_t command = static_cast<uint8_t>(static_cast<unsigned char>(capacityValue[0]));

    Serial.printf("Capacity command received: %u\r\n", command);

    if (command == CHECK_CAPACITY_COMMAND) {
        xQueueOverwrite(capacityCommandQueue, &command);
    }
    else {
        Serial.println("Failed to receive valid capacity command.");

    }
}


/*
 * ============================================================
 * PHOTO CALLBACK
 * ============================================================
 */

void PhotoCallbacks::onWrite(BLECharacteristic* characteristic) {
    if (characteristic->getUUID().toString() != PHOTO_UPLOAD_UUID) {
        return;
    }

    std::string value = characteristic->getValue();

    if (value.empty()) {
        Serial.println("Photo characteristic received empty value.");
        return;
    }

    const uint8_t* packet = reinterpret_cast<const uint8_t*>(value.data());
    const size_t packetLength = value.length();

    /*
     * ========================================================
     * PHOTO CONTROL COMMAND
     * ========================================================
     */

    if (packetLength == 1 && packet[0] == UPLOAD_PHOTO_COMMAND) {
        uint8_t command = packet[0];

        Serial.printf("Photo upload command received: %u\n", command);

        xQueueOverwrite(uploadPhotoCommandQueue, &command);

        return;
    }

    /*
     * ========================================================
     * PHOTO PROTOCOL PACKET
     * ========================================================
     */

    if (packetLength > PHOTO_PACKET_MAX_SIZE) {
        Serial.printf("Photo packet too large: %u bytes\n", static_cast<unsigned int>(packetLength));
        return;
    }

    PhotoPacketMessage message{};

    message.length = static_cast<uint16_t>(packetLength);

    memcpy(message.data, packet, packetLength);

    if (xQueueSend(photoPacketQueue, &message, 0) != pdTRUE) {
        Serial.println("Photo packet queue full. Packet dropped.");
        return;
    }

    // Serial.printf("Queued photo packet: type=0x%02X, length=%u\n", message.data[0], message.length);
}



// Works! Next to need to ensure atomic control of display mode changes. 
    void DisplayControlCallbacks::onWrite(BLECharacteristic* characteristic){
        const std::string value = characteristic->getValue();

        if (value.size() != 1) {
            Serial.println("Display control command must be one byte");
            return;
        }

        const uint8_t command = static_cast<uint8_t>(value[0]);

        switch (command) {
            case VIEW_SENSOR_DASHBOARD_COMMAND:
                currentDisplayMode.store(DisplayMode::SensorDashboard);
                Serial.println("Sensor Dashboard mode requested");
                break;

            default:
                Serial.printf("Unknown display control command: %u\n", command);
                break;
        }
    };
