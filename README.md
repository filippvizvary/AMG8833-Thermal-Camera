# Thermal Camera (AMG8833 + ILI9341 + Pico W)

A small thermal camera project that reads an AMG8833 8x8 thermal sensor and renders a live heatmap on an ILI9341 TFT.

This repo also includes an optional desktop viewer ([thermal_viewer.py](thermal_viewer.py)) that reads matrix frames from serial and visualizes them with Tkinter.

## Features

- Live 8x8 thermal heatmap on ILI9341 display
- Fixed color scale (default 20 C to 35 C)
- 10 Hz update loop (sensor + display)
- On-screen frame min/max and thermistor temperature
- On-screen CPU duty estimate
- Optional serial matrix streaming for desktop visualization
- Boot-time TFT speed benchmark

## Hardware

- Raspberry Pi Pico or Pico W
- AMG8833 thermal sensor
- ILI9341 SPI TFT display
- Jumper wires

## Wiring

### ILI9341 to Pico W (SPI0)

- VCC -> 3V3
- GND -> GND
- SCK -> GP18 (SPI0 SCK)
- MOSI -> GP19 (SPI0 TX)
- CS -> GP28
- DC -> GP20
- RST -> optional (wired in code as GPIO, can also be tied appropriately)
- MISO -> optional (usually not needed for write-only drawing)

### AMG8833 to Pico W (I2C)

- VIN -> 3V3
- GND -> GND
- SCL -> GP5 (I2C0 SCL) or your configured Wire pins
- SDA -> GP4 (I2C0 SDA) or your configured Wire pins

## Why MISO is optional

For this project, the TFT is used in write-only mode (drawing pixels/text). Display readback is not required, so MISO normally has no visible effect. You only need MISO if you plan to read data back from the display controller.

## Firmware setup (PlatformIO)

Project config is in [platformio.ini](platformio.ini).

Build:

```bash
platformio run
```

Upload:

```bash
platformio run --target upload
```

Main firmware is in [src/main.cpp](src/main.cpp).

## Serial matrix stream (optional)

The firmware can emit frames in this exact format (8 rows x 8 floats):

```text
22.50 22.75 23.50 23.50 25.25 26.25 27.25 26.75
...
(8 total rows)
```

Toggle this in [src/main.cpp](src/main.cpp) by changing:

- `SERIAL_STREAM_MATRIX = true` to enable
- `SERIAL_STREAM_MATRIX = false` to disable

Disabling serial stream can improve display responsiveness.

## Desktop viewer (optional)

The desktop viewer is [thermal_viewer.py](thermal_viewer.py).

Install dependency:

```bash
python3 -m pip install pyserial
```

Run:

```bash
python3 thermal_viewer.py --port /dev/ttyACM0 --baud 115200 --tmin 20 --tmax 35
```

Notes:

- Viewer expects clean numeric rows (8 numbers per line, 8 lines per frame).
- The viewer ignores non-matrix debug lines.

## Known limits

- AMG8833 max frame rate is 10 FPS, which is the primary update limit.
- Faster TFT SPI does not bypass the sensor frame-rate limit.

## License

Use and modify freely for personal/educational projects.
