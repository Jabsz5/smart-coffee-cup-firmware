#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include <cstring>
#include <string>
#include "smartCupConfig.h"
#include "drivers.h"
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include "photoProtocol.h"
#include <cmath>

// Display pins
#define TFT_CS    7
#define TFT_DC    6
#define TFT_RST   15
#define TFT_MOSI  13
#define TFT_SCLK  12

// Custom SPI bus
SPIClass displaySPI(FSPI);

// ST7789 display
Adafruit_ST7789 display(&displaySPI, TFT_CS, TFT_DC, TFT_RST);

// Going to make display have multiple modes.

enum class DisplayMode : uint8_t {
    Initialization, 
    SensorDashboard,
    Text,
    Photo,
    Drawing
};

extern volatile DisplayMode currentDisplayMode;
volatile DisplayMode currentDisplayMode = DisplayMode::Initialization;

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

void drawSensorValue(
    int16_t y,
    uint16_t color,
    float value,
    const char* unit
) {
    /*
     * Clear only the region containing the old number.
     * Do not clear the entire display.
     */
    display.fillRect(
        115,
        y,
        120,
        24,
        ST77XX_BLACK
    );

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

// Incorrect functions used for just testing right now. Will be replaced by their respective functions that read the actual values from the sensors.
void updateSensorDisplay() {
    const float plateTemp =
        getPlateTemperatureF();

    const float liquidTemp =
        getPlateTemperatureF();

    const float ambientTemp =
        getPlateTemperatureF();

    const float weight =
        getPlateTemperatureF();

    drawSensorValue(
        75,
        ST77XX_YELLOW,
        plateTemp,
        "F"
    );

    drawSensorValue(
        120,
        ST77XX_GREEN,
        liquidTemp,
        "F"
    );

    drawSensorValue(
        165,
        ST77XX_CYAN,
        ambientTemp,
        "F"
    );

    drawSensorValue(
        210,
        ST77XX_WHITE,
        weight,
        "g"
    );
}




MyServerCallbacks serverCallbacks;
DisplayCallbacks displayCallbacks;
TemperatureCallbacks temperatureCallbackHandler;
CapacityCallbacks capacityCallbackHandler;
PhotoCallbacks photoCallbackHandler;
OneWire activeOneWire(ACTIVE_SENSOR_PIN);
OneWire ambientOneWire(AMBIENT_SENSOR_PIN);

DallasTemperature activeSensor(&activeOneWire);
DallasTemperature ambientSensor(&ambientOneWire);

bool alarmArmed = false;


int temperatureSensorInit() {
    constexpr uint8_t MAX_RETRIES = 5;

    for (uint8_t attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        Serial.printf("Temperature sensor initialization attempt %u/%u\n", attempt, MAX_RETRIES);

        activeSensor.begin();
        ambientSensor.begin();

        /*
         * Use 12-bit resolution.
         * 12-bit conversion takes up to approximately 750 ms.
         */
        activeSensor.setResolution(12);
        ambientSensor.setResolution(12);

        /*
         * Do not block inside requestTemperatures().
         * This lets both sensors perform their conversions at the same time.
         */
        activeSensor.setWaitForConversion(false);
        ambientSensor.setWaitForConversion(false);

        uint8_t activeSensorCount = activeSensor.getDeviceCount();
        uint8_t ambientSensorCount = ambientSensor.getDeviceCount();

        char activeSensorMessage[32];
        char ambientSensorMessage[32];

        snprintf(activeSensorMessage, sizeof(activeSensorMessage), "Active sensors found: %u", static_cast<unsigned int>(activeSensorCount));
        snprintf(ambientSensorMessage, sizeof(ambientSensorMessage), "Ambient sensors found: %u", static_cast<unsigned int>(ambientSensorCount));

        displayMessage(activeSensorMessage);
        Serial.println(activeSensorMessage);

        displayMessage(ambientSensorMessage);
        Serial.println(ambientSensorMessage);

        if (activeSensorCount > 0 && ambientSensorCount > 0) {
            Serial.println("Temperature sensors initialized successfully. Heater enabled");
            HEATER_ENABLED = true;
            return EXT_CODE_SUCCESS;
        }

        Serial.println("Temperature sensor initialization failed.");

        if (attempt < MAX_RETRIES) {
            delay(500);
        }
    }

    /*
     * Temperature sensing is required for safe heater operation.
     * Disable the heater if either sensor cannot be detected.
     */
    HEATER_ENABLED = false;
    digitalWrite(HEATER_PIN, LOW);
    Serial.println("Temperature sensors failed after 5 attempts. Heater disabled.");

    return ERR_CODE_TEMP_SENSOR_INIT_FAILED;
}

// Latest valid plate-temperature reading.
// NAN means that no valid reading is currently available.
static float plateTempF = NAN;

// Protects plateTempF because one FreeRTOS task writes it while
// other tasks may read it.
static portMUX_TYPE plateTemperatureMux = portMUX_INITIALIZER_UNLOCKED;


void plateTemperatureSensorInit() {
    pinMode(THERMISTOR_PIN, INPUT);

    // ESP32 ADC range: 0–4095.
    analogReadResolution(12);

    // Increases the measurable input-voltage range.
    analogSetPinAttenuation(THERMISTOR_PIN, ADC_11db);

    Serial.printf("Plate thermistor initialized on GPIO %u\n", THERMISTOR_PIN);
}

float readPlateTemperature() {
    uint32_t total = 0;

    // Average eight readings to reduce ADC noise.
    for (uint8_t sample = 0; sample < 8; sample++) {
        total += analogRead(THERMISTOR_PIN);
    }

    const float adc = static_cast<float>(total) / 8.0f;

    // Zero or full-scale normally indicates invalid wiring,
    // a short circuit, or a disconnected thermistor.
    if (adc <= 0.0f || adc >= ADC_MAX_VALUE) {
        return NAN;
    }

    const float resistance = SERIES_RESISTOR * adc / (ADC_MAX_VALUE - adc);

    if (resistance <= 0.0f || !std::isfinite(resistance)) {
        return NAN;
    }

    // Beta-parameter thermistor equation.
    const float inverseTemperatureK = (1.0f / (TEMP_NOMINAL_C + 273.15f)) + (1.0f / THERMISTOR_BETA) * std::log(resistance / THERMISTOR_NOMINAL);

    if (inverseTemperatureK <= 0.0f || !std::isfinite(inverseTemperatureK)) {
        return NAN;
    }

    const float temperatureK = 1.0f / inverseTemperatureK;

    const float temperatureC = temperatureK - 273.15f;

    const float temperatureF = temperatureC * 9.0f / 5.0f + 32.0f;

    const float correctedTemperatureF = temperatureF + PLATE_TEMP_CORRECTION_F;

    if (!std::isfinite(correctedTemperatureF)) {
        return NAN;
    }

    return correctedTemperatureF;
}


float getPlateTemperatureF() {
    float temperatureSnapshot;

    portENTER_CRITICAL(&plateTemperatureMux);
    temperatureSnapshot = plateTempF;
    portEXIT_CRITICAL(&plateTemperatureMux);

    return temperatureSnapshot;
}

void soundAlarm(){
 Serial.println();
 Serial.println("*** HIGH TEMPERATURE ALARM ***");


 tone(BUZZER_PIN, BUZZER_FREQUENCY);
 delay(BUZZER_DURATION_MS);
 noTone(BUZZER_PIN);
}

int buzzerGPIOinit() {
    if (!GPIO_IS_VALID_OUTPUT_GPIO(BUZZER_PIN)) {
        return ERR_CODE_BUZZER_INIT_FAIL;
    }
	pinMode(BUZZER_PIN, OUTPUT);
	digitalWrite(BUZZER_PIN, HIGH);

    return EXT_CODE_SUCCESS;
}

void printError(const char* message, uint16_t errorCode) {
    // ==========================================
    // Print to Serial
    // ==========================================
    Serial.println();
    Serial.println("========== ERROR ==========");
    Serial.println(message);
    Serial.printf("Error code: 0x%04X\n", errorCode);
    Serial.println("===========================");
    Serial.println();

    // ==========================================
    // Print to ST7789
    // ==========================================
    display.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    display.println(message);

    display.setTextColor(ST77XX_RED, ST77XX_BLACK);
    display.printf("Error code: 0x%04X\n", errorCode);

    display.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    //display.println("Restarting ESP32...");

    Serial.flush();
}

// default boot up screen
int bootupScreen(){
  displaySettingStartup();
  constexpr int MESSAGE_LENGTH = 65;
  size_t bytes = display.println("There should be stuff on the display if you are reading this :p\n");

  if (bytes != MESSAGE_LENGTH){
    return ERR_CODE_FAILED_WRITE_TO_DISPLAY;
  }
  return EXT_CODE_SUCCESS;
}

void displaySettingStartup() {
    display.fillScreen(ST77XX_BLACK);

	// Use size 1 for initiatlization messages
	// Use size 2 for actual messages to be displayed on the OLED screen
    display.setTextSize(1);
    display.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    display.setCursor(0, 0);
    display.setTextWrap(false);
}

void displayMessage(const char* message) {
  display.println("\n");
  display.println(message);
}

int OLEDinit() {
    Serial.println("Starting ST7789...");

    displaySPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

    // Check whether host=side SPI subsystem initialized correctly.
    // Not true hardware validation.
    if (displaySPI.bus() == nullptr) {
        Serial.println("SPI bus initialization failed.");
        return ERR_CODE_SPI_INIT_FAILED;
    }

    if (displaySPI.getClockDivider() == 0) {
        Serial.println("SPI clock configuration failed.");
        return ERR_CODE_SPI_INIT_FAILED;
    }

    display.init(240, 320);
    uint8_t rotation = 2;
    display.setRotation(rotation);

    return EXT_CODE_SUCCESS;
}

/*
Two different behaviors occuring;
1. State transition
2. State update while continuously in said state.
*/
void displayTask(void* parameter) {
    Serial.println("Display task entered.");
    constexpr TickType_t DISPLAY_INTERVAL = pdMS_TO_TICKS(100);

    TickType_t lastWakeTime = xTaskGetTickCount();

    DisplayMode previousDisplayMode = currentDisplayMode;

    Serial.printf("Display task started on Core %d\n", xPortGetCoreID());

    while (true) {

        
        if (currentDisplayMode != previousDisplayMode) {

            switch (currentDisplayMode) {

                case DisplayMode::Initialization:
                    /*
                    Do nothing.
                    setup() already makes the display init.
                    No need to rewrite itself.
                    */
                    break;

                case DisplayMode::SensorDashboard:
                    Serial.println("Switching display to Sensor Dashboard mode\n");
                    drawSensorDisplayLayout();
                    updateSensorDisplay();
                    break;

                case DisplayMode::Text:
                    // Draw text screen later
                    break;

                case DisplayMode::Photo:
                    // Draw photo screen later
                    break;

                case DisplayMode::Drawing:
                    // Draw drawing screen later
                    break;
            }
            previousDisplayMode = currentDisplayMode;
        }

        // Continuously update dynamic screens
        if (currentDisplayMode == DisplayMode::SensorDashboard){
            updateSensorDisplay();
        }

        vTaskDelayUntil(&lastWakeTime, DISPLAY_INTERVAL);
    }
}

void plateTemperatureTask(void* parameters) {
    // Initialize the first wake time once.
    TickType_t lastWakeTime =
        xTaskGetTickCount();

    Serial.println(
        "Plate temperature task started"
    );

    while (true) {
        const float newPlateTempF =
            readPlateTemperature();

        portENTER_CRITICAL(&plateTemperatureMux);
        plateTempF = newPlateTempF;
        portEXIT_CRITICAL(&plateTemperatureMux);

        if (std::isfinite(newPlateTempF)) {
            Serial.printf(
                "Plate temperature: %.1f F\n",
                newPlateTempF
            );
        } else {
            Serial.println(
                "Plate temperature unavailable"
            );
        }

        /*
         * vTaskDelayUntil produces a stable 50 ms period.
         * A regular vTaskDelay(50) would add execution time
         * to every interval.
         */
        vTaskDelayUntil(
            &lastWakeTime,
            pdMS_TO_TICKS(
                PLATE_UPDATE_INTERVAL_MS
            )
        );
    }
}

#define DO_NOT_STOP_HERE 0
void temperatureTask(void *parameter){
	uint8_t temperatureBytes[sizeof(float)] = {0};
	uint8_t command = 0;
    TickType_t lastWakeTime = xTaskGetTickCount();
	bool alreadyAlarmed = true;
	Serial.printf("Temperature task started on Core %d\n\r", xPortGetCoreID());

	while(1){

		// we're gonna need to also check the temperature continuously so the buzzer can buzz. 
		ambientSensor.requestTemperatures();
 		activeSensor.requestTemperatures();

 		float ambientF = ambientSensor.getTempFByIndex(0);
 		float probeF = activeSensor.getTempFByIndex(0);

		const bool probeReadingValid = isfinite(probeF) && probeF != DEVICE_DISCONNECTED_F;

        if (!probeReadingValid) {
            Serial.printf("Invalid probe temperature: %.2f F\n\r", probeF);
        	} else {
            /*
             * Trigger once when the temperature crosses
             * below the low-temperature threshold.
             */
            	if (alreadyAlarmed && (probeF < LOW_TEMP_ALARM_F)) {
                	Serial.printf("Probe temperature %.2f F is below %.2f F.\n\r", probeF, LOW_TEMP_ALARM_F);
					Serial.printf("Alarm should be going off here!\n\r");
					// alert phone app via characteristic update 
					memcpy(temperatureBytes, &probeF, sizeof(probeF));
					// no need to clear arrray since memcpy() overwrites it
					temperatureCharacteristic->setValue(temperatureBytes, sizeof(temperatureBytes));
					temperatureCharacteristic->notify();
					// alarm fired, don't fire again...
					alreadyAlarmed = false;

                if (alarmTaskHandle != nullptr) {
                    xTaskNotifyGive(alarmTaskHandle);
                }
            }

            /*
             * Do not allow another alarm until the temperature
             * has risen safely above the rearm threshold.
             */
            else if (!alreadyAlarmed && (probeF >= LOW_TEMP_REARM_F)) {
                Serial.printf("Low-temperature alarm rearmed at %.2f F.\n\r", probeF);
				alreadyAlarmed = true;
            }
        }

		if (xQueueReceive(temperatureCommandQueue, &command, 0) == pdTRUE){
			Serial.printf("Processing temperature command on Core %d: %d\n\r", xPortGetCoreID(), command);
		}
		// TO-DO: Implement temperature control logic here.
		if (command == CHECK_TEMPERATURE_COMMAND) {
			// for now it is just going to send a dummy value to the phone app

			ambientSensor.requestTemperatures();
 			activeSensor.requestTemperatures();
 			float ambientF = ambientSensor.getTempFByIndex(0);
 			float probeF = activeSensor.getTempFByIndex(0);

			memcpy(temperatureBytes, &probeF, sizeof(probeF));
			// Send the temperature value to the phone app
			temperatureCharacteristic->setValue(temperatureBytes, sizeof(temperatureBytes));
			temperatureCharacteristic->notify();
			// no need to clear arrray since memcpy() overwrites it
		}
		vTaskDelay(pdMS_TO_TICKS(1000)); // Delay for 1 second
		
	}
}

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

void capacityTask(void *parameter){
	uint8_t command;
	Serial.printf("Capacity task started on Core %d\n\r", xPortGetCoreID());

	while(1){
		if (xQueueReceive(capacityCommandQueue, &command, portMAX_DELAY) == pdTRUE){
			Serial.printf("Processing capacity command on Core %d: %d\n\r", xPortGetCoreID(), command);
		}
		// TO-DO: Implement capacity control logic here.
		if (command == CHECK_CAPACITY_COMMAND) {
			// for now it is just going to send a dummy value to the phone app
			float dummyCapacity = 0.75; // 75% full
			uint8_t capacityBytes[sizeof(dummyCapacity)];
			memcpy(capacityBytes, &dummyCapacity, sizeof(dummyCapacity));
			// Send the capacity value to the phone app
			capacityCharacteristic->setValue(capacityBytes, sizeof(capacityBytes));
			capacityCharacteristic->notify();
		}
		vTaskDelay(pdMS_TO_TICKS(1000)); // Delay for 1 second
		
	}
}

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
            Serial.printf("Processing photo packet: type=0x%02X, length=%u\n", packetMessage.data[0], packetMessage.length);
            handleImagePacket(packetMessage.data, packetMessage.length, display);
        }
    }
}

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

    // Reserve one byte for the null terminator.
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

void TemperatureCallbacks::onWrite(BLECharacteristic* characteristic) {
    if (characteristic->getUUID().toString() != TEMPERATURE_CHAR_UUID) {
        return;
    }

    std::string temperatureValue = characteristic->getValue();

    if (temperatureValue.empty()) {
        Serial.println("Temperature command was empty.");
        return;
    }

    uint8_t command = static_cast<uint8_t>(
        static_cast<unsigned char>(temperatureValue[0])
    );

    temperatureControlValue = command;

    Serial.printf("Temperature control command received: %u\r\n", temperatureControlValue);

    if (command == CHECK_TEMPERATURE_COMMAND) {
        xQueueOverwrite(temperatureCommandQueue, &command);
    } else{
		Serial.println("Exiting program. Failed to receive temperature command. Err code: ERR_CODE_TEMPERATURE_COMMAND_RECEIVE_FAILED");
		ESP.restart();
	}
}

// value range is from [0, 1] for capacity value. Will convert to percentage 
void CapacityCallbacks::onWrite(BLECharacteristic* characteristic) {
	if (characteristic->getUUID().toString() != CAPACITY_CHAR_UUID) {
		return;
	}

	std::string capacityValue = characteristic->getValue();

	if (capacityValue.empty()) {
		Serial.println("Capacity command was empty.");
		return;
	}

	uint8_t command = static_cast<uint8_t>(
		static_cast<unsigned char>(capacityValue[0])
	);

	Serial.printf("Capacity command received: %u\r\n", command);

	// Here you can add logic to handle the capacity command as needed.
	if (command == CHECK_CAPACITY_COMMAND) {
        xQueueOverwrite(capacityCommandQueue, &command);
    } else{
		Serial.println("Exiting program. Failed to receive capacity command. Err code: ERR_CODE_CAPACITY_COMMAND_RECEIVE_FAILED");
		ESP.restart();
	}
}

/* TO-DO:
Remove handleIimagePacket() out of BLE callback. 
BLE callback should do as little work as possible.
Handle work in the actual queue next.
For now, test sending the command byte from mobile app first
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
     * -----------------------------
     * PHOTO CONTROL COMMAND
     * -----------------------------
     */

    if (packetLength == 1 && packet[0] == UPLOAD_PHOTO_COMMAND) {
        uint8_t command = packet[0];
        Serial.printf("Photo upload command received: %u\n",command);
        xQueueOverwrite(uploadPhotoCommandQueue, &command);
        return;
    }

    /*
     * -----------------------------
     * PHOTO PROTOCOL PACKET
     * -----------------------------
     * Anything else is going to be a photo packet
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

    Serial.printf("Queued photo packet: type=0x%02X, length=%u\n", message.data[0], message.length);
}

bool tryBluetoothInit() {
    Serial.println("Starting BLE...");
    displayMessage("Starting BLE...");

    BLEDevice::init("ESP32-BLE-Test");
    BLEDevice::setMTU(247);

    BLEServer* server = BLEDevice::createServer();

    if (server == nullptr) {
        Serial.println("Failed to create BLE server.");
        return false;
    }

    server->setCallbacks(&serverCallbacks);

    BLEService* smartCupService = server->createService(SERVICE_UUID);

    if (smartCupService == nullptr) {
        Serial.println("Failed to create BLE service.");
        return false;
    }

    displayCharacteristic = smartCupService->createCharacteristic(OLED_TEXT_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
    heatingPadCharacteristic = smartCupService->createCharacteristic(HEATING_PAD_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
    temperatureCharacteristic = smartCupService->createCharacteristic(TEMPERATURE_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_WRITE);
    capacityCharacteristic = smartCupService->createCharacteristic(CAPACITY_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_WRITE);

    photoCharacteristic = smartCupService->createCharacteristic(PHOTO_UPLOAD_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);

    if (displayCharacteristic == nullptr || heatingPadCharacteristic == nullptr || temperatureCharacteristic == nullptr || capacityCharacteristic == nullptr || photoCharacteristic == nullptr) {
        Serial.println("Failed to create one or more BLE characteristics.");
        return false;
    }

    photoCharacteristic->addDescriptor(new BLE2902());

    displayCharacteristic->setCallbacks((BLECharacteristicCallbacks*)&displayCallbacks);
    // heatingPadCharacteristic->setCallbacks(
    //     (BLECharacteristicCallbacks*)&heatingPadCallbacks
    // );
    temperatureCharacteristic->setCallbacks((BLECharacteristicCallbacks*)&temperatureCallbackHandler);
    capacityCharacteristic->setCallbacks((BLECharacteristicCallbacks*)&capacityCallbackHandler);
    photoCharacteristic->setCallbacks((BLECharacteristicCallbacks*)&photoCallbackHandler);

    smartCupService->start();

    BLEAdvertising* advertising = BLEDevice::getAdvertising();

    if (advertising == nullptr) {
        Serial.println("Failed to get BLE advertising object.");
        return false;
    }

    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->start();

    Serial.println("BLE advertising started.");
    displayMessage("BLE advertising started.\n Waiting for connection...");

    return true;
}

int bluetoothinit() {
    constexpr uint8_t MAX_RETRIES = 5;

    for (uint8_t attempt = 1; attempt <= MAX_RETRIES; attempt++) {

        Serial.printf("BLE initialization attempt %u/%u\n", attempt, MAX_RETRIES);

        if (tryBluetoothInit()) {
            return EXT_CODE_SUCCESS;
        }

        Serial.printf("BLE initialization attempt %u failed.\n", attempt);

        /*
         * Remove objects/resources created during
         * the unsuccessful BLE initialization.
         */
        BLEDevice::deinit(true);

        if (attempt < MAX_RETRIES) {
            delay(500);
        }
    }

    Serial.println("BLE failed after 5 attempts. Continuing locally.");
    displayCharacteristic = nullptr;
    heatingPadCharacteristic = nullptr;
    temperatureCharacteristic = nullptr;
    capacityCharacteristic = nullptr;
    photoCharacteristic = nullptr;

    return ERR_CODE_BLUETOOTH_INIT_FAILED;
}