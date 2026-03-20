#include "SPI.h"
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
#include <Melopero_AMG8833.h>

// -----------------------------------------------------------------------------
// Thermal camera demo for Raspberry Pi Pico / Pico W:
// - AMG8833 (I2C) provides an 8x8 temperature matrix
// - ILI9341 (SPI) displays a color heatmap
//
// Suggested Pico W wiring (SPI0 + I2C0):
// ILI9341 -> Pico W
//   VCC    -> 3V3
//   GND    -> GND
//   SCK    -> GP18 (SPI0 SCK)
//   MOSI   -> GP19 (SPI0 TX)
//   CS     -> GP28
//   DC     -> GP20
//   RST    -> optional (wired here as GPIO, or tie to RUN/3V3)
//   MISO   -> optional, only needed for display readback features
//
// AMG8833 -> Pico W
//   VIN    -> 3V3
//   GND    -> GND
//   SCL    -> GP5 (I2C0 SCL) or your configured Wire pins
//   SDA    -> GP4 (I2C0 SDA) or your configured Wire pins
// -----------------------------------------------------------------------------

// ILI9341 control pins (data pins use hardware SPI)
#define TFT_DC 20
#define TFT_CS 28
#define TFT_RST -1

// Create display and sensor objects
// Hardware SPI constructor: uses board SPI pins (fast path).
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
Melopero_AMG8833 sensor;

// Optional readback constructor (only if you need display reads via MISO):
// #define TFT_MISO 16 // example: any valid SPI RX pin for your SPI instance
// #define TFT_MOSI 19 // SPI0 TX
// #define TFT_CLK 18  // SPI0 SCK
// Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_CLK, TFT_RST, TFT_MISO);
// Note: normal rendering is write-only, so MISO is typically not required.

// Layout and timing parameters
const int GRID_START_X = 10;
const int GRID_START_Y = 35;
const int CELL_SIZE = 25;
const int GRID_WIDTH = 8 * CELL_SIZE;
const uint32_t TFT_SPI_HZ = 40000000;
const uint32_t FRAME_INTERVAL_MS = 100;
const bool SERIAL_STREAM_MATRIX = true;

// Temperature scale (fixed for consistent colors)
const float TEMP_MIN = 20.0;
const float TEMP_MAX = 35.0;

// Map temperature to RGB565 color.
uint16_t tempToColor(float temp) {
  // Normalize temp to 0..1
  float t = (temp - TEMP_MIN) / (TEMP_MAX - TEMP_MIN);
  t = max(0.0f, min(1.0f, t));
  
  // 4-point gradient: blue -> cyan -> yellow -> red
  uint8_t r, g, b;
  
  if (t < 0.33f) {
    float p = t / 0.33f;
    r = (uint8_t)(0 + p * 0);
    g = (uint8_t)(0 + p * 255);
    b = (uint8_t)(180 + p * (255 - 180));
  } else if (t < 0.66f) {
    float p = (t - 0.33f) / 0.33f;
    r = (uint8_t)(0 + p * 255);
    g = 255;
    b = (uint8_t)(255 - p * 255);
  } else {
    float p = (t - 0.66f) / 0.34f;
    r = 255;
    g = (uint8_t)(255 - p * 255);
    b = 0;
  }
  
  // Convert to RGB565
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Thermal Camera Display ===");
  
  // Initialize display
  tft.begin();
  tft.setSPISpeed(TFT_SPI_HZ);
  tft.setRotation(1);  // Landscape orientation
  tft.fillScreen(ILI9341_BLACK);
  
  // Quick boot benchmark so you can confirm TFT write performance.
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(30, 100);
  tft.println("Display FPS Test");
  
  unsigned long startTime = millis();
  int frameCount = 0;
  
  // Draw 100 rectangles and measure elapsed time.
  while (frameCount < 100) {
    int x = (frameCount % 10) * 30;
    int y = (frameCount / 10) * 30;
    tft.fillRect(x, y, 25, 25, ILI9341_RED);
    frameCount++;
  }
  
  unsigned long elapsed = millis() - startTime;
  float fps = (100.0f / elapsed) * 1000.0f;
  
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(30, 100);
  tft.println("Display FPS:");
  tft.setCursor(30, 130);
  tft.print(fps, 1);
  tft.println(" FPS");
  tft.setCursor(30, 160);
  tft.print(elapsed);
  tft.println(" ms for 100 rects");
  
  Serial.print("Display benchmark: ");
  Serial.print(fps, 1);
  Serial.println(" FPS");
  
  delay(2000);  // Show benchmark for 2 seconds
  
  tft.fillScreen(ILI9341_BLACK);
  
  // Initialize AMG8833 via I2C.
  Wire.begin();
  sensor.initI2C();
  
  Serial.print("Resetting sensor ... ");
  int statusCode = sensor.resetFlagsAndSettings();
  Serial.println(sensor.getErrorDescription(statusCode));
  
  Serial.print("Setting FPS ... ");
  statusCode = sensor.setFPSMode(FPS_MODE::FPS_10);
  Serial.println(sensor.getErrorDescription(statusCode));
  
  // Static UI labels.
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(GRID_START_X, 10);
  tft.println("Thermal Camera 8x8");
  
  // Color scale markers for TEMP_MIN..TEMP_MAX.
  tft.setTextSize(1);
  tft.setCursor(GRID_START_X + GRID_WIDTH + 20, GRID_START_Y);
  tft.setTextColor(ILI9341_BLUE);
  tft.println("20C");
  
  tft.setCursor(GRID_START_X + GRID_WIDTH + 20, GRID_START_Y + GRID_WIDTH / 2 - 5);
  tft.setTextColor(ILI9341_YELLOW);
  tft.println("27C");
  
  tft.setCursor(GRID_START_X + GRID_WIDTH + 20, GRID_START_Y + GRID_WIDTH - 10);
  tft.setTextColor(ILI9341_RED);
  tft.println("35C");
  
  Serial.println("Display initialized and ready!");
}

void loop() {
  // Run the full pipeline at a fixed rate (10 Hz when FRAME_INTERVAL_MS = 100).
  static uint32_t lastFrameMs = 0;
  uint32_t nowMs = millis();
  if (nowMs - lastFrameMs < FRAME_INTERVAL_MS) {
    return;
  }
  lastFrameMs = nowMs;
  uint32_t frameStartUs = micros();

  // Read fresh thermal data.
  int statusCode = sensor.updateThermistorTemperature();
  if (statusCode != 0) {
    Serial.println("Error updating thermistor!");
    return;
  }
  
  statusCode = sensor.updatePixelMatrix();
  if (statusCode != 0) {
    Serial.println("Error updating pixel matrix!");
    return;
  }
  
  // Find frame min/max for status text.
  float minTemp = 999.0, maxTemp = -999.0;
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      float temp = sensor.pixelMatrix[y][x];
      if (temp < minTemp) minTemp = temp;
      if (temp > maxTemp) maxTemp = temp;
    }
  }

  // Optional serial stream for PC visualizer (8 rows x 8 floats per frame).
  // Keep rows as pure numeric lines so thermal_viewer.py can parse them directly.
  if (SERIAL_STREAM_MATRIX) {
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        Serial.print(sensor.pixelMatrix[y][x], 2);
        if (x < 7) {
          Serial.print(" ");
        }
      }
      Serial.println();
    }
  }
  
  // Draw the 8x8 heatmap as filled cells.
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      float temp = sensor.pixelMatrix[y][x];
      uint16_t color = tempToColor(temp);
      
      int px = GRID_START_X + x * CELL_SIZE;
      int py = GRID_START_Y + y * CELL_SIZE;
      
      tft.fillRect(px, py, CELL_SIZE, CELL_SIZE, color);
    }
  }
  
  // Update bottom status area.
  tft.fillRect(GRID_START_X, GRID_START_Y + GRID_WIDTH + 10, 220, 40, ILI9341_BLACK);
  
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  
  tft.setCursor(GRID_START_X, GRID_START_Y + GRID_WIDTH + 10);
  tft.print("Therm: ");
  tft.print(sensor.thermistorTemperature, 1);
  tft.println("C");
  
  tft.setCursor(GRID_START_X, GRID_START_Y + GRID_WIDTH + 20);
  tft.print("Min: ");
  tft.print(minTemp, 1);
  tft.print("  Max: ");
  tft.print(maxTemp, 1);
  tft.println("C");

  // CPU load estimate: frame work time divided by frame budget.
  // This is a duty-cycle estimate for this loop, not full RTOS/system CPU usage.
  uint32_t frameWorkUs = micros() - frameStartUs;
  float instantLoad = (frameWorkUs / 1000.0f) * (100.0f / FRAME_INTERVAL_MS);
  if (instantLoad > 100.0f) {
    instantLoad = 100.0f;
  }
  static float cpuLoadPct = 0.0f;
  cpuLoadPct = cpuLoadPct * 0.8f + instantLoad * 0.2f;

  const int cpuBoxX = 245;
  const int cpuBoxY = 2;
  const int cpuBoxW = 72;
  const int cpuBoxH = 12;
  tft.fillRect(cpuBoxX, cpuBoxY, cpuBoxW, cpuBoxH, ILI9341_BLACK);
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(1);
  tft.setCursor(cpuBoxX, cpuBoxY + 2);
  tft.print("CPU ");
  tft.print(cpuLoadPct, 0);
  tft.print("%");
}
