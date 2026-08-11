# AQAIO

Air Quality All-In-One using a Sensirion SEN66 and a Seeed Studio XIAO ESP32-C6.

The root [`aqaio.ino`](./aqaio.ino) sketch is the current firmware path. It reads the SEN66 over I2C and prints the measurements over USB serial. The ESPHome component remains available for the older e-paper display path.

## Bill of materials

- [SEN66 sensor](https://sensirion.com/products/catalog/SEN66)
- [Seeed Studio XIAO ESP32-C6](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C6-p-5884.html)
- Optional [1.54-inch GDEH0154D67 e-paper display](https://www.aliexpress.us/item/3256806104827483.html) for the ESPHome path
- [3D-printed enclosure](./3D)
- M2.5 x 6 mm screws

The enclosure CAD and wiring PNG were made for the previous XIAO ESP32-C3/e-paper prototype. Confirm the C6 board, antenna, and connector clearances before manufacturing.

## Arduino firmware wiring

Connect the SEN66 to the XIAO ESP32-C6 as follows:

| Function | XIAO pin | GPIO | SEN66 |
|----------|:--------:|:----:|:-----|
| I2C SDA  | D10      | 18   | SDA   |
| I2C SCL  | D9       | 20   | SCL   |
| Power    | 3V3      | —    | VCC   |
| Ground   | GND      | —    | GND   |

The SEN66 uses I2C address `0x6B` in the sketch. The C6 supports remapped I2C on D10/D9; use a 3.3 V supply and ensure the I2C lines have pull-ups.

## Flash the Arduino sketch

1. Install the Espressif ESP32 Arduino core and the Sensirion SEN66 library.
2. Select the `XIAO_ESP32C6` board.
3. Enable USB CDC on boot if your Arduino IDE exposes that option.
4. Upload [`aqaio.ino`](./aqaio.ino).
5. Open a serial monitor at `115200` baud.

The sketch spends about 10 seconds warming up the sensor before reporting readings.

## ESPHome e-paper wiring

The ESPHome example keeps I2C on D4/D5 because D10 is reserved for the e-paper MOSI line in that profile:

| Function | XIAO pin | GPIO | E-paper |
|----------|:--------:|:----:|:--------|
| SPI MOSI | D10      | 18   | MOSI    |
| SPI SCLK | D8       | 19   | SCLK    |
| CS       | D3       | 21   | CS      |
| DC       | D2       | 2    | D/C     |
| RST      | D1       | 1    | RST     |
| BUSY     | D0       | 0    | BUSY    |

See [`example.yaml`](./example.yaml) for the complete ESPHome configuration. The existing PNG wiring image documents the previous C3 prototype and should not be used for this C6 wiring.
