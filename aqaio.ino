/*
 * AQAIO - Air Quality All-In-One
 * Seeed Studio XIAO ESP32-C6 + SEN66 (serial terminal output)
 *
 * Reads all SEN66 environmental sensor data over I2C and
 * prints readings to the serial terminal.
 *
 * Wiring:
 * | Function | XIAO Pin | GPIO | Component |
 * |----------|:--------:|:----:|-----------|
 * | I2C SDA  |   D10    |  18  | SEN66     |
 * | I2C SCL  |   D9     |  20  | SEN66     |
 */

#include <Wire.h>
#include <SensirionI2cSen66.h>
#include <string.h>

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

// Consider the device stale after two missed measurement intervals.
#define STATUS_STALE_INTERVALS 2

// ─── Pin Definitions ────────────────────────────────────────────────────────

#define I2C_SDA 18  // XIAO D10 / SDA
#define I2C_SCL 20  // XIAO D9 / SCL

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
static unsigned long nextUpdateMillis = 0;
static unsigned long nextRetryMillis = 0;
static bool sensorReady = false;
static bool measurementAttempted = false;
static bool lastMeasurementSucceeded = false;
static bool hasLastUpdate = false;
static bool readNowRequested = false;

static SEN66DeviceStatus sen66Status = {};
static bool sen66StatusValid = false;
static int16_t lastStatusError = NO_ERROR;

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
    if (secs == 0) {
        snprintf(buf, bufLen, "just now");
    } else if (secs < 60) {
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

void formatUptime(unsigned long elapsedMs, char* buf, size_t bufLen) {
    unsigned long secs = elapsedMs / 1000;
    if (secs < 60) {
        snprintf(buf, bufLen, "%lus", secs);
    } else if (secs < 3600) {
        unsigned long mins = secs / 60;
        snprintf(buf, bufLen, "%lum%lus", mins, secs % 60);
    } else {
        unsigned long hrs = secs / 3600;
        unsigned long mins = (secs % 3600) / 60;
        snprintf(buf, bufLen, "%luh%lum", hrs, mins);
    }
}

bool timeReached(unsigned long now, unsigned long deadline) {
    return static_cast<long>(now - deadline) >= 0;
}

unsigned long staleAfterMillis() {
    return static_cast<unsigned long>(UPDATE_INTERVAL_S)
        * 1000UL * STATUS_STALE_INTERVALS;
}

bool sen66HasError() {
    return sen66Status.fanError
        || sen66Status.rhtError
        || sen66Status.gasError
        || sen66Status.co22Error
        || sen66Status.hchoError
        || sen66Status.pmError
        || sen66Status.co21Error;
}

bool sen66HasWarning() {
    return sen66Status.fanSpeedWarning;
}

const char* deviceStatusLabel() {
    if (!sensorReady) {
        return "OFFLINE";
    }
    if (!measurementAttempted) {
        return "STARTING";
    }
    if (!sensorData.valid && !lastMeasurementSucceeded) {
        return "ERROR";
    }
    if (sen66StatusValid && sen66HasError()) {
        return "SENSOR ERROR";
    }
    if (sen66StatusValid && sen66HasWarning()) {
        return "SENSOR WARNING";
    }
    if (!lastMeasurementSucceeded) {
        return "READ ERROR";
    }
    if (hasLastUpdate && millis() - lastUpdateMillis > staleAfterMillis()) {
        return "STALE";
    }
    return "ONLINE";
}

bool refreshSEN66Status() {
    SEN66DeviceStatus status;
    int16_t error = sensor.readDeviceStatus(status);
    lastStatusError = error;
    if (error != NO_ERROR) {
        sen66StatusValid = false;
        return false;
    }

    sen66Status = status;
    sen66StatusValid = true;
    return true;
}

void printSEN66Status() {
    if (!sen66StatusValid) {
        Serial.print(F("SEN66 status: unavailable"));
        if (lastStatusError != NO_ERROR) {
            char errorMessage[64];
            errorToString(lastStatusError, errorMessage, sizeof(errorMessage));
            Serial.print(F(" ("));
            Serial.print(errorMessage);
            Serial.print(F(")"));
        }
        Serial.println();
        return;
    }

    Serial.print(F("SEN66 status: "));
    if (sen66HasError()) {
        Serial.println(F("ERROR"));
    } else if (sen66HasWarning()) {
        Serial.println(F("WARNING"));
    } else {
        Serial.println(F("OK"));
    }

    Serial.print(F("SEN66 status flags: 0x"));
    Serial.println(sen66Status.value, HEX);

    if (sen66Status.fanError) Serial.println(F("  - fan error"));
    if (sen66Status.rhtError) Serial.println(F("  - RH/T error"));
    if (sen66Status.gasError) Serial.println(F("  - gas error"));
    if (sen66Status.co22Error) Serial.println(F("  - CO2 error 2"));
    if (sen66Status.hchoError) Serial.println(F("  - HCHO error"));
    if (sen66Status.pmError) Serial.println(F("  - particulate matter error"));
    if (sen66Status.co21Error) Serial.println(F("  - CO2 error 1"));
    if (sen66Status.fanSpeedWarning) Serial.println(F("  - fan speed warning"));
}

void printLastUpdate() {
    Serial.print(F("Last update: "));
    if (!hasLastUpdate) {
        Serial.println(F("never"));
        return;
    }

    char elapsedStr[24];
    formatElapsed(millis() - lastUpdateMillis, elapsedStr, sizeof(elapsedStr));
    Serial.println(elapsedStr);
}

void printDeviceStatus() {
    // Read the sensor's native status register when the user asks for status.
    if (sensorReady) {
        refreshSEN66Status();
    }

    Serial.println();
    Serial.println(F("===== AQAIO status ====="));
    Serial.print(F("Device status: "));
    Serial.println(deviceStatusLabel());
    Serial.print(F("Measurement:   "));
    if (!sensorReady) {
        Serial.println(F("not running"));
    } else if (!measurementAttempted) {
        Serial.println(F("starting"));
    } else if (lastMeasurementSucceeded) {
        Serial.println(F("running"));
    } else {
        Serial.println(F("read failed"));
    }
    printSEN66Status();
    printLastUpdate();

    char uptimeStr[24];
    formatUptime(millis(), uptimeStr, sizeof(uptimeStr));
    Serial.print(F("Uptime:       "));
    Serial.println(uptimeStr);
    Serial.println(F("========================"));
}

void printCommandHelp() {
    Serial.println(F("Commands: status (or s), read now (or r), help (or ?)."));
}

void handleSerialCommands() {
    static char command[16];
    static size_t commandLength = 0;

    while (Serial.available() > 0) {
        char incoming = static_cast<char>(Serial.read());
        if (incoming == '\r' || incoming == '\n') {
            command[commandLength] = '\0';

            if (strcmp(command, "s") == 0 || strcmp(command, "status") == 0) {
                printDeviceStatus();
            } else if (strcmp(command, "r") == 0 || strcmp(command, "read") == 0) {
                readNowRequested = true;
                Serial.println(F("Reading requested."));
            } else if (strcmp(command, "?") == 0 || strcmp(command, "help") == 0) {
                printCommandHelp();
            } else if (commandLength > 0) {
                Serial.println(F("Unknown command. Send 'help' for options."));
            }

            commandLength = 0;
        } else if (commandLength < sizeof(command) - 1) {
            command[commandLength++] = incoming;
        }
    }
}

// ─── Terminal Output ────────────────────────────────────────────────────────

void printSensorData() {
    Serial.println();
    Serial.println(F("===== AQAIO reading ====="));

    if (!sensorData.valid) {
        Serial.println(F("Sensor data: unavailable"));
    } else {
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
    }

    Serial.print(F("Device status: "));
    Serial.println(deviceStatusLabel());
    printSEN66Status();
    printLastUpdate();
    Serial.println(F("========================="));
}

bool readSensorData() {
    measurementAttempted = true;

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

    lastMeasurementSucceeded = error == NO_ERROR;
    if (!lastMeasurementSucceeded) {
        errorToString(error, errorMessage, sizeof(errorMessage));
        Serial.print(F("readMeasuredValues() error: "));
        Serial.println(errorMessage);
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
        hasLastUpdate = true;
    }

    // Read the SEN66's native health flags after every measurement attempt.
    refreshSEN66Status();
    return lastMeasurementSucceeded;
}

// ─── Setup ──────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500); // Allow USB CDC to connect on the XIAO ESP32-C6.

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

    nextUpdateMillis = millis();
    nextRetryMillis = millis();
    printCommandHelp();
}

// ─── Loop ───────────────────────────────────────────────────────────────────

void loop() {
    handleSerialCommands();

    unsigned long now = millis();
    if (!sensorReady) {
        if (timeReached(now, nextRetryMillis)) {
            // Sensor failed to start; retry without blocking status commands.
            nextRetryMillis = now + 1000UL;
            int16_t error = sensor.startContinuousMeasurement();
            if (error == NO_ERROR) {
                sensorReady = true;
                nextUpdateMillis = millis() + 1200UL;
                Serial.println(F("SEN66 measurement started (retry)."));
            } else {
                char errorMessage[64];
                errorToString(error, errorMessage, sizeof(errorMessage));
                Serial.print(F("SEN66 retry error: "));
                Serial.println(errorMessage);
            }
        }
    } else if (readNowRequested || timeReached(now, nextUpdateMillis)) {
        readNowRequested = false;
        readSensorData();
        printSensorData();
        nextUpdateMillis = millis()
            + static_cast<unsigned long>(UPDATE_INTERVAL_S) * 1000UL;
    }

    delay(10);
}
