#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <string>
#include "smartCupConfig.h"
#include "drivers.h"
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include "photoProtocol.h"
#include <cmath>
#include "bluetoothCallbacks.h"
#include "displayController.h"

// Changed from FSPI to VSPI for this ESP32 chip model... :p
SPIClass displaySPI(VSPI);
// ST7789 display
Adafruit_ST7789 display(&displaySPI, TFT_CS, TFT_DC, TFT_RST);

// Temperature probes
OneWire activeOneWire(ACTIVE_SENSOR_PIN);
OneWire ambientOneWire(AMBIENT_SENSOR_PIN);
DallasTemperature activeSensor(&activeOneWire);
DallasTemperature ambientSensor(&ambientOneWire);
float activeTempF = NAN;
float ambientTempF = NAN;
portMUX_TYPE temperatureMux = portMUX_INITIALIZER_UNLOCKED;

// Load cell
// Global HX711 instance
HX711 scale;

bool alarmArmed = false;

int temperatureSensorInit() {
    constexpr uint8_t MAX_RETRIES = 5;
    for (uint8_t attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        Serial.printf("Temperature sensor initialization attempt %u/%u\n", attempt, MAX_RETRIES);
        activeSensor.begin();
        ambientSensor.begin();
        /*
         * Use 11-bit resolution.
         * 11-bit conversion takes up to approximately 300 ms.
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

int plateTemperatureSensorInit() {
    pinMode(THERMISTOR_PIN, INPUT);
    // ESP32 ADC range: 0–4095.
    analogReadResolution(12);
    // Increases the measurable input-voltage range.
    analogSetPinAttenuation(THERMISTOR_PIN, ADC_11db);
    Serial.printf("Plate thermistor initialized on GPIO %u\n", THERMISTOR_PIN);
    return EXT_CODE_SUCCESS;
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

int loadcellInit() {
    const int MAX_RETRIES = 5;

    // Initialize HX711
    scale.begin(HX711_DOUT, HX711_SCK);

    // Attempt to detect HX711 up to 5 times
    bool initialized = false;

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        Serial.printf("Initializing HX711 (Attempt %d/%d)...\n",
                      attempt, MAX_RETRIES);

        if (scale.wait_ready_timeout(1000)) {
            initialized = true;
            break;
        }

        Serial.printf("HX711 not responding (Attempt %d/%d).\n",
                      attempt, MAX_RETRIES);
    }

    // Return failure if all attempts were unsuccessful
    if (!initialized) {
        Serial.println("ERROR: HX711 load cell initialization failed!");
        return ERR_CODE_LOAD_CELL_INIT_FAILED;
    }

    // Apply calibration factor
    scale.set_scale(60.29f);

    // Tare the scale (set current weight to zero)
    scale.tare(10);

    Serial.println("HX711 load cell initialized successfully!");

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

int bootupScreen() {
    displaySettingStartup();
    constexpr char message[] = "Good to know you can see this! :D";
    const size_t written = display.println(message);
    const size_t expected = sizeof(message) - 1U + 2U;
    if (written != expected) {
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
    Serial.flush();
    displaySPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

    if (displaySPI.bus() == nullptr) {
        Serial.println("SPI bus initialization failed.");
        return ERR_CODE_SPI_INIT_FAILED;
    }

    display.init(240, 320);
    display.setRotation(2);
    Serial.println("ST7789 initialization finished.");
    return EXT_CODE_SUCCESS;
}

bool tryBluetoothInit() {

    Serial.println("Starting BLE...");
    displayMessage("Starting BLE...");

    BLEDevice::init("ESP32-BLE-Test");
    BLEDevice::setMTU(517);
    BLEServer* server = BLEDevice::createServer();
    if (server == nullptr) {
        Serial.println("Failed to create BLE server.");
        return false;
    }

    server->setCallbacks(&serverCallbacks);
    BLEService* smartCupService = server->createService(BLEUUID(std::string(SERVICE_UUID)), 30);
    if (smartCupService == nullptr) {
        Serial.println("Failed to create BLE service.");
        return false;
    }

    displayCharacteristic = smartCupService->createCharacteristic(
    OLED_TEXT_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
);

heatingPadCharacteristic = smartCupService->createCharacteristic(
    HEATING_PAD_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
);

temperatureCharacteristic = smartCupService->createCharacteristic(
    TEMPERATURE_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_WRITE
);

plateTemperatureCharacteristic = smartCupService->createCharacteristic(
    PLATE_TEMPERATURE_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_WRITE
);

capacityCharacteristic = smartCupService->createCharacteristic(
    CAPACITY_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_WRITE
);

photoCharacteristic = smartCupService->createCharacteristic(
    PHOTO_UPLOAD_UUID,
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
);

displayControlCharacteristic = smartCupService->createCharacteristic(
    DISPLAY_CONTROL_UUID,
    BLECharacteristic::PROPERTY_WRITE
);

    if (displayCharacteristic == nullptr || 
        heatingPadCharacteristic == nullptr || 
        temperatureCharacteristic == nullptr || 
        capacityCharacteristic == nullptr || 
        photoCharacteristic == nullptr || 
        displayControlCharacteristic == nullptr ||
        plateTemperatureCharacteristic == nullptr){
        Serial.println("Failed to create one or more BLE characteristics.");
        return false;
    }

    photoCharacteristic->addDescriptor(new BLE2902());
    plateTemperatureCharacteristic->addDescriptor(new BLE2902());

    displayCharacteristic->setCallbacks((BLECharacteristicCallbacks*)&displayCallbacks);
    // heatingPadCharacteristic->setCallbacks(
    //     (BLECharacteristicCallbacks*)&heatingPadCallbacks
    // );
    temperatureCharacteristic->setCallbacks((BLECharacteristicCallbacks*)&temperatureCallbackHandler);
    plateTemperatureCharacteristic->setCallbacks((BLECharacteristicCallbacks*)&plateTemperatureCallbackHandler);
    capacityCharacteristic->setCallbacks((BLECharacteristicCallbacks*)&capacityCallbackHandler);
    photoCharacteristic->setCallbacks((BLECharacteristicCallbacks*)&photoCallbackHandler);
    displayControlCharacteristic->setCallbacks((BLECharacteristicCallbacks*)&displayControlCallbackHandler);

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
    displayControlCharacteristic = nullptr;

    return ERR_CODE_BLUETOOTH_INIT_FAILED;
}