#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include "soc/soc_caps.h"
#include <stdint.h>

#ifndef DEVICE_NAME
#define DEVICE_NAME "Waveshare AMOLED 2.06 Watch"
#endif

// ---- I2C bus (shared: touch FT3168, AXP2101 PMU, PCF85063 RTC, QMI8658 IMU) ----
#define SYS_I2C_SDA 15
#define SYS_I2C_SCL 14
static const uint8_t SDA = SYS_I2C_SDA;
static const uint8_t SCL = SYS_I2C_SCL;

// ---- Touch (FT3168) ----
#define TOUCH_INT 38
#define TOUCH_RST 9
#define FT3168_ADDR 0x38

// ---- Buttons ----
// BOOT is a normal ESP32 GPIO0. PWR is NOT a GPIO at all: it's read through
// the AXP2101's extended IO pin EXIO6 over I2C (see interface.cpp).
#define HAS_BTN 1
#define BTN_ALIAS "\"Boot\""
#define BTN_PIN 0
#define BTN_ACT LOW

// ---- SD Card (SDMMC 1-bit/4-bit) ----
#define SDMMC_CLK 2
#define SDMMC_CMD 1
#define SDMMC_D0 3
#define SDCARD_CS 17

// ---- Display (see platformio.ini for TFT_* QSPI build flags) ----
#define HAS_SCREEN 1
#define ROTATION 0
#define MINBRIGHT (uint8_t)1

// This panel has large rounded corners. Status bar items and the touch footer
// are shifted inward by this many pixels so text/icons don't get clipped.
#define UI_CORNER_INSET 34

// The visible aperture is the full 410x502 with corner radius ~= 90 px (measured
// on the glass). The largest axis-aligned rectangle that fits inside those
// corners is inset by 27 px, so anything that must stay fully visible (keyboard
// keys, file list) is kept inside [27 .. width-27] x [27 .. height-27].
#define UI_SAFE_INSET 27

// Status bar. The clock is twice as tall (the bitmap font is 6x8, so size 2 is
// 12x16 px) and green. At y=12 the corner curve is still 45 px away from the
// left edge, so x=68 clears it, and the clock (60 px wide) ends well before the
// icons. The battery keeps UI_STATUS_INSET from the right edge; 40 px is the
// smallest inset that keeps its top-right corner inside the curve.
#define UI_STATUS_INSET 40
#define UI_CLOCK_X 68
#define UI_CLOCK_Y 12
#define UI_CLOCK_SIZE 2
#define UI_CLOCK_COLOR 0x07E0 // pure green
// No separator line: the rounded frame already starts at y=39 and separates the
// status bar from the list. (0 = do not draw it.)
#define UI_STATUS_LINE_Y 0

// First row the menus/lists are allowed to use. The frame is drawn at 5+34 = 39,
// and y=46 is where the corner curve has receded past x=12, so text starting at
// x=12 is no longer clipped by it.
#define BORDER_PAD_Y 46

// The scrollbar lives at x = width-5..width-1, i.e. right under the bezel. Those
// columns are only visible for y in 61..441 (measured), so the track must be
// clamped to that band instead of spanning the whole screen.
#define UI_SCROLLBAR_TOP 63
#define UI_SCROLLBAR_BOT 439

// Font sizes
#define FP 1
#define FM 2
#define FG 3

#define HAS_TOUCH 1

// No dedicated IR/RF/BadUSB pins confirmed yet for this board - left undefined
// on purpose until verified, to avoid clobbering shared pins.

// ---- SPI aliases required by the ESP32 Arduino core (SPI.cpp always
// expects these globals to exist, even if unused by our QSPI display) ----
static const uint8_t SS = -1;
static const uint8_t MOSI = -1;
static const uint8_t MISO = -1;
static const uint8_t SCK = -1;

// ---- External SPI bus (for optional wired modules: RFID/MFRC522, CC1101,
// NRF24, W5500). This board has no dedicated external SPI header, so these
// are left unconnected (-1) unless you wire your own module to spare GPIOs. ----
#define SPI_SS_PIN -1
#define SPI_MOSI_PIN -1
#define SPI_MISO_PIN -1
#define SPI_SCK_PIN -1

// ---- BadUSB via external CH9329 UART adapter (not wired on this board) ----
#define BAD_RX -1
#define BAD_TX -1

#endif /* Pins_Arduino_h */
