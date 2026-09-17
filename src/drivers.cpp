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

MyServerCallbacks serverCallbacks;
OledTextCallbacks oledTextCallbacks;
TemperatureCallbacks temperatureCallbackHandler;
CapacityCallbacks capacityCallbackHandler;
PhotoCallbacks photoCallbackHandler;
OneWire activeOneWire(ACTIVE_SENSOR_PIN);
OneWire ambientOneWire(AMBIENT_SENSOR_PIN);

DallasTemperature activeSensor(&activeOneWire);
DallasTemperature ambientSensor(&ambientOneWire);

bool alarmArmed = false;


int temperatureSensorInit(){
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

  showOLEDMessage(activeSensorMessage);
  Serial.println(activeSensorMessage);

  showOLEDMessage(ambientSensorMessage);
  Serial.println(ambientSensorMessage);

  if (activeSensorCount == 0 || ambientSensorCount == 0){
	// either one failed. return fail status
	return ERR_CODE_TEMP_SENSOR_INIT_FAILED;
  }

  return EXT_CODE_SUCCESS;
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
    // Print to Serial
    Serial.println(message);
    Serial.printf("Error code: 0x%04X\n", errorCode);
    Serial.println("Restarting ESP32...");

    // Print to ST7789
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
  display.println("Senior Design 2026\nGroup 5\nSmart Cofee Cup\nBooting up...\n");
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

void showOLEDMessage(const char* message) {
  display.println("\n");
  display.println(message);
}

int OLEDinit() {
  Serial.println("Starting ST7789...");

	// SCLK, MISO unused, MOSI, CS
    displaySPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

    display.init(240, 320);
    uint8_t rotation = 2; 
    display.setRotation(rotation);  
  	return EXT_CODE_SUCCESS;
}

void oledTextTask(void *parameter){
	OledTextMessage message;

	Serial.printf("OLED text task started on Core %d\n\r", xPortGetCoreID());

	while(1){
		if (xQueueReceive(oledTextQueue, &message, portMAX_DELAY) == pdTRUE){
			Serial.printf("\r\nProcessing OLED message on Core %d: %s\r\n", xPortGetCoreID(), message.text);
			displaySettingStartup();
			display.setTextSize(2);
			showOLEDMessage(message.text);
		}
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


void MyServerCallbacks::onConnect(BLEServer* server) {
    deviceConnected = true;

    Serial.println("BLE device connected!");
    showOLEDMessage("Device is\nconnected.");
}

void MyServerCallbacks::onDisconnect(BLEServer* server) {
    deviceConnected = false;

    Serial.println("BLE device disconnected!");

    BLEDevice::startAdvertising();
    Serial.println("BLE advertising restarted.");

    showOLEDMessage("Waiting for connection...");
}

void OledTextCallbacks::onWrite(BLECharacteristic* characteristic) {
    if (characteristic->getUUID().toString() != OLED_TEXT_CHAR_UUID) {
        return;
    }

    std::string textFromApp = characteristic->getValue();

    Serial.printf("BLE onWrite callback running on Core %d\n", xPortGetCoreID());

    if (textFromApp.empty()) {
        return;
    }

    OledTextMessage message{};

    // Reserve one byte for the null terminator.
    size_t copyLength = textFromApp.length();

    if (copyLength >= OLED_TEXT_MAX_LENGTH) {
        copyLength = OLED_TEXT_MAX_LENGTH - 1;
    }

    memcpy(message.text, textFromApp.data(), copyLength);
    message.text[copyLength] = '\0';

    if (xQueueSend(oledTextQueue, &message, 0) != pdTRUE) {
        Serial.println("OLED queue is full. Message discarded.");
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
		exit(ERR_CODE_TEMPERATURE_COMMAND_RECEIVE_FAILED);
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
		exit(ERR_CODE_CAPACITY_COMMAND_RECEIVE_FAILED);
	}
}

// store received photo bytes to buffer. Multiple onWrite() calls will occur for these stream of bytes
// The mobile app will process the image. Phone will decode PNG to RGB565 pixels
std::vector<uint8_t> photoBuffer;
void PhotoCallbacks::onWrite(BLECharacteristic* characteristic){
    if (characteristic->getUUID().toString() != PHOTO_UPLOAD_UUID) {
		return;
	}

    std::string value = characteristic->getValue();
    if (value.empty()) {
            return;
        }

    const uint8_t* data = reinterpret_cast<const uint8_t*>(value.data());


    photoBuffer.insert(photoBuffer.end(), data, data + value.size());
    Serial.printf("Received %d bytes. Total: %d bytes\n", value.size(), photoBuffer.size());

    // TO-DO:
    // Test with mobile app to see if I can atleast receive the data.
    // Design tiny protocol between ESP32 and app to determine when image is finished
}

int bluetoothinit(){
	Serial.println("Starting BLE...");
	showOLEDMessage("Starting BLE...");
	BLEDevice::init("ESP32-BLE-Test");

	BLEServer* server = BLEDevice::createServer();
	server->setCallbacks(&serverCallbacks);

	// Create one BLE service for your smart coffee cup
	BLEService* smartCupService = server->createService(SERVICE_UUID);

	// Create characteristics
    // recall that the properties are read from the CLIENT'S perspective (the mobile app)
	oledTextCharacteristic = smartCupService->createCharacteristic(OLED_TEXT_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
	heatingPadCharacteristic = smartCupService->createCharacteristic(HEATING_PAD_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
	temperatureCharacteristic = smartCupService->createCharacteristic(TEMPERATURE_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_WRITE);
	capacityCharacteristic = smartCupService->createCharacteristic(CAPACITY_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY |  BLECharacteristic::PROPERTY_WRITE);
    photoCharacteristic = smartCupService->createCharacteristic(PHOTO_UPLOAD_UUID, BLECharacteristic:: PROPERTY_WRITE);
                                                                            
	// Attach the callback that runs when the phone writes data
	oledTextCharacteristic->setCallbacks((BLECharacteristicCallbacks*)&oledTextCallbacks);
	///heatingPadCharacteristic->setCallbacks((BLECharacteristicCallbacks*)&heatingPadCallbacks);
	temperatureCharacteristic->setCallbacks((BLECharacteristicCallbacks*)&temperatureCallbackHandler);
	capacityCharacteristic->setCallbacks((BLECharacteristicCallbacks*)&capacityCallbackHandler);
    photoCharacteristic->setCallbacks((BLECharacteristicCallbacks*)&photoCallbackHandler);

	// Start the BLE service
	smartCupService->start();

	// Advertise the service UUID so the phone can find it
	BLEAdvertising* advertising = BLEDevice::getAdvertising();
	advertising->addServiceUUID(SERVICE_UUID);
	advertising->setScanResponse(true);
	advertising->start();

	Serial.println("BLE advertising started.");
	showOLEDMessage("BLE advertising started.\nWaiting for connection...");

	return EXT_CODE_SUCCESS;
}