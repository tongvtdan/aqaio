/*
 * AQAIO - Air Quality All-In-One
 * ESP32-C3 + SEN66 (serial terminal output)
 *
 * Reads all SEN66 environmental sensor data over I2C and
 * prints readings to the serial terminal.
 *
 * Wiring:
 * | Function | ESP32-C3 Pin | Component |
 * |----------|:------------:|-----------|
 * | I2C SDA  |      10      | SEN66     |
 * | I2C SCL  |       9      | SEN66     |
 */

#include <Wire.h>
#include <SensirionI2cSen66.h>

// ─── User Configuration ─────────────────────────────────────────────────────

// Update interval in seconds (how often to print a reading).
#define UPDATE_INTERVAL_S 60

// Temperature unit: set to true for °F, false for °C.
#define USE_FAHRENHEIT false

// Air quality thresholds for the terminal status indicator.
#define AQ_CO2_MAX 1000
#define AQ_VOC_MAX 150
#define AQ_NOX_MAX 1
#define AQ_PM_MAX 12

// ─── Pin Definitions ────────────────────────────────────────────────────────

#define I2C_SDA 10
#define I2C_SCL 9

// ─── Globals ────────────────────────────────────────────────────────────────

#ifndef NO_ERROR
#define NO_ERROR 0
#endif

SensirionI2cSen66 sensor;

struct SensorData {
    float pm1p0;
    float pm2p5;
    float pm4p0;
    float pm10p0;
    float humidity;
    float temperature;
    float vocIndex;
    float noxIndex;
    uint16_t co2;
    bool valid;
};

static SensorData sensorData = {0, 0, 0, 0, 0, 0, 0, 0, 0, false};
static unsigned long lastUpdateMillis = 0;
static bool sensorReady = false;

// ─── Helper Functions ───────────────────────────────────────────────────────

float toTerminalTemp(float tempC) {
    if (USE_FAHRENHEIT) {
        return tempC * 9.0f / 5.0f + 32.0f;
    }
    return tempC;
}

const char* tempUnitStr() {
    return USE_FAHRENHEIT ? "F" : "C";
}

bool isGoodAirQuality() {
    return sensorData.valid
        && sensorData.co2 < AQ_CO2_MAX
        && sensorData.vocIndex < AQ_VOC_MAX
        && sensorData.noxIndex <= AQ_NOX_MAX
        && sensorData.pm1p0 < AQ_PM_MAX
        && sensorData.pm2p5 < AQ_PM_MAX
        && sensorData.pm4p0 < AQ_PM_MAX
        && sensorData.pm10p0 < AQ_PM_MAX;
}

void formatElapsed(unsigned long elapsedMs, char* buf, size_t bufLen) {
    unsigned long secs = elapsedMs / 1000;
    if (secs < 60) {
        snprintf(buf, bufLen, "%lus ago", secs);
    } else if (secs < 3600) {
        unsigned long mins = secs / 60;
        snprintf(buf, bufLen, "%lum ago", mins);
    } else {
        unsigned long hrs = secs / 3600;
        unsigned long mins = (secs % 3600) / 60;
        snprintf(buf, bufLen, "%luh%lum ago", hrs, mins);
    }
}

// ─── Terminal Output ────────────────────────────────────────────────────────

void printSensorData() {
    Serial.println();
    Serial.println(F("===== AQAIO reading ====="));

    if (!sensorData.valid) {
        Serial.println(F("Sensor data: unavailable"));
        Serial.println(F("========================="));
        return;
    }

    Serial.print(F("Temperature: "));
    Serial.print(toTerminalTemp(sensorData.temperature), 1);
    Serial.print(F(" "));
    Serial.println(tempUnitStr());

    Serial.print(F("Humidity:    "));
    Serial.print(sensorData.humidity, 1);
    Serial.println(F(" %"));

    Serial.print(F("CO2:         "));
    Serial.print(sensorData.co2);
    Serial.println(F(" ppm"));

    Serial.print(F("VOC index:   "));
    Serial.println(sensorData.vocIndex, 0);

    Serial.print(F("NOx index:   "));
    Serial.println(sensorData.noxIndex, 0);

    Serial.print(F("PM1.0:       "));
    Serial.print(sensorData.pm1p0, 1);
    Serial.println(F(" ug/m3"));

    Serial.print(F("PM2.5:       "));
    Serial.print(sensorData.pm2p5, 1);
    Serial.println(F(" ug/m3"));

    Serial.print(F("PM4.0:       "));
    Serial.print(sensorData.pm4p0, 1);
    Serial.println(F(" ug/m3"));

    Serial.print(F("PM10:        "));
    Serial.print(sensorData.pm10p0, 1);
    Serial.println(F(" ug/m3"));

    Serial.print(F("Air quality: "));
    if (isGoodAirQuality()) {
        Serial.println(F("GOOD"));
    } else {
        Serial.println(F("CHECK"));
    }

    char elapsedStr[24];
    formatElapsed(millis() - lastUpdateMillis, elapsedStr, sizeof(elapsedStr));
    Serial.print(F("Last update: "));
    Serial.println(elapsedStr);
    Serial.println(F("========================="));
}

// ─── Setup ──────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500); // Allow USB CDC to connect on ESP32-C3.

    Serial.println(F("AQAIO - Air Quality All-In-One"));
    Serial.println(F("Terminal output mode (no display)"));
    Serial.println(F("Initializing..."));

    // Initialize I2C for SEN66.
    Wire.begin(I2C_SDA, I2C_SCL);

    // Initialize SEN66.
    sensor.begin(Wire, SEN66_I2C_ADDR_6B);

    char errorMessage[64];
    int16_t error;

    error = sensor.deviceReset();
    if (error != NO_ERROR) {
        errorToString(error, errorMessage, sizeof(errorMessage));
        Serial.print(F("SEN66 deviceReset() error: "));
        Serial.println(errorMessage);
    }
    delay(1200); // Required after reset.

    // Print serial number.
    int8_t serialNumber[32] = {0};
    error = sensor.getSerialNumber(serialNumber, 32);
    if (error != NO_ERROR) {
        errorToString(error, errorMessage, sizeof(errorMessage));
        Serial.print(F("SEN66 getSerialNumber() error: "));
        Serial.println(errorMessage);
    } else {
        Serial.print(F("SEN66 Serial: "));
        Serial.println((const char*)serialNumber);
    }

    // Start continuous measurement.
    error = sensor.startContinuousMeasurement();
    if (error != NO_ERROR) {
        errorToString(error, errorMessage, sizeof(errorMessage));
        Serial.print(F("SEN66 startContinuousMeasurement() error: "));
        Serial.println(errorMessage);
    } else {
        sensorReady = true;
        Serial.println(F("SEN66 continuous measurement started."));
    }

    Serial.print(F("Update interval: "));
    Serial.print(UPDATE_INTERVAL_S);
    Serial.println(F("s"));

    // Discard the first 10 readings because initial values can be inaccurate.
    if (sensorReady) {
        Serial.println(F("Warming up sensor (10 readings)..."));
        float pm1, pm2, pm4, pm10, humidity, temperature, voc, nox;
        uint16_t co2;
        for (int i = 0; i < 10; i++) {
            delay(1000);
            int16_t warmupError = sensor.readMeasuredValues(
                pm1, pm2, pm4, pm10,
                humidity, temperature,
                voc, nox, co2);

            if (warmupError != NO_ERROR) {
                errorToString(warmupError, errorMessage, sizeof(errorMessage));
                Serial.print(F("  warmup read error: "));
                Serial.println(errorMessage);
            } else {
                Serial.print(F("  warmup "));
                Serial.print(i + 1);
                Serial.println(F("/10"));
            }
        }
        Serial.println(F("Sensor warm-up complete."));
    }
}

// ─── Loop ───────────────────────────────────────────────────────────────────

void loop() {
    if (!sensorReady) {
        // Sensor failed to start; retry without touching any display hardware.
        delay(5000);
        int16_t error = sensor.startContinuousMeasurement();
        if (error == NO_ERROR) {
            sensorReady = true;
            Serial.println(F("SEN66 measurement started (retry)."));
        } else {
            char errorMessage[64];
            errorToString(error, errorMessage, sizeof(errorMessage));
            Serial.print(F("SEN66 retry error: "));
            Serial.println(errorMessage);
        }
        return;
    }

    // Read sensor data.
    float pm1p0 = 0;
    float pm2p5 = 0;
    float pm4p0 = 0;
    float pm10p0 = 0;
    float humidity = 0;
    float temperature = 0;
    float vocIndex = 0;
    float noxIndex = 0;
    uint16_t co2 = 0;

    char errorMessage[64];
    int16_t error = sensor.readMeasuredValues(
        pm1p0, pm2p5, pm4p0, pm10p0,
        humidity, temperature,
        vocIndex, noxIndex, co2);

    if (error != NO_ERROR) {
        errorToString(error, errorMessage, sizeof(errorMessage));
        Serial.print(F("readMeasuredValues() error: "));
        Serial.println(errorMessage);
        // Keep the last valid reading in memory, if one exists.
    } else {
        sensorData.pm1p0 = pm1p0;
        sensorData.pm2p5 = pm2p5;
        sensorData.pm4p0 = pm4p0;
        sensorData.pm10p0 = pm10p0;
        sensorData.humidity = humidity;
        sensorData.temperature = temperature;
        sensorData.vocIndex = vocIndex;
        sensorData.noxIndex = noxIndex;
        sensorData.co2 = co2;
        sensorData.valid = true;
        lastUpdateMillis = millis();
    }

    printSensorData();

    delay((unsigned long)UPDATE_INTERVAL_S * 1000UL);
}
