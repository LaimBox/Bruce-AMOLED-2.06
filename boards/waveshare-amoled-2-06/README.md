# Waveshare ESP32-S3-Touch-AMOLED-2.06 port

Bruce firmware port for the **Waveshare ESP32-S3-Touch-AMOLED-2.06** watch
(ESP32-S3, 16 MB flash, 8 MB PSRAM).

## Hardware

| Part | Details |
| --- | --- |
| Panel | CO5300 AMOLED, 410 x 502, QSPI (CS 12, SCLK 11, D0-D3 4-7, RST 8) |
| Panel offset | column offset 22, row offset 0 |
| Touch | FT3168, I2C 0x38 (SDA 15, SCL 14), INT 38, RST 9 |
| PMU | AXP2101 (same I2C bus) |
| RTC | PCF85063 |
| IMU | QMI8658 |
| Button | BOOT / GPIO0; PWR is read through AXP2101 EXIO6 |
| SD | SDMMC pins, used in SPI mode (CLK 2, CMD 1, D0 3, CS 17) |

The glass is round-cornered: the visible aperture is the full 410 x 502 with a
corner radius of about **90 px** (measured on the panel, not a guess). That is
the cause of most of the layout work in this port.

## Build and flash

A prebuilt image is attached to the
[latest release](https://github.com/LaimBox/Bruce-AMOLED-2.06/releases/latest)
(`firmware.factory.bin`). To build it yourself:

```bash
# from the repository root
pio run -e waveshare-amoled-2-06
```

The resulting image is `.pio/build/waveshare-amoled-2-06/firmware.factory.bin`.
Flash **only the factory image at address 0x0**:

```bash
espflash write-bin 0x0 .pio/build/waveshare-amoled-2-06/firmware.factory.bin \
  --after watchdog-reset -p /dev/ttyACM0
```

Writing the plain `firmware.bin` at 0x0 boots into a `TG0WDT_SYS_RST` loop on
this board.

## What had to be fixed

### 1. The CO5300 only commits a write when the window is completely filled

Two symptoms, both confirmed on hardware:

* a window that is exactly one row high (`PASET start == end`) is never shown,
  even though the pixels were sent;
* widening such a window to two rows is not enough - the data has to cover both
  rows, otherwise the whole write is dropped.

Everything that was invisible before (text without a background colour,
`drawXBitmap` icons, `drawPixel`, 1 px outlines of `drawRect` / `drawLine` /
`drawCircle` / `drawArc`) comes from this. The fix lives in
`lib/HAL/display/ardgfx.h`: a `CO5300_WindowFix` subclass patches
`writeFillRectPreclipped` (1 row -> 2 rows with duplicated data) and
`writePixelPreclipped` (1 pixel -> 1 x 2 block).

### 2. Layout inside the rounded aperture

All values are board-local, declared in `boards/waveshare-amoled-2-06/pins_arduino.h`
with `#ifndef` fallbacks in the generic code, so other boards are unaffected:

* `UI_CORNER_INSET` (34) - frame and touch footer;
* `UI_SAFE_INSET` (27) - the largest axis-aligned rectangle that fits inside the
  corners; used by the keyboard and the file list;
* `UI_CLOCK_X/Y/SIZE/COLOR` - a green, twice-as-tall clock at (68, 12), where the
  corner curve is already 45 px away from the edge;
* `UI_STATUS_INSET` (40) - keeps the battery out of the top-right curve;
* `BORDER_PAD_Y` (46) - first usable row of menus/lists;
* `UI_SCROLLBAR_TOP/BOT` (63/439) - the last five columns are only visible in
  that band, so the submenu scrollbar is clamped to it.

The status bar counts the active status icons before drawing the clock and
automatically falls back to the small font if the icon row would not fit next to
it.

### 3. Portrait geometry of the main menu

`MenuItemInterface` computed the icon/title coordinates in its constructor,
which runs before `tft.begin()` has set the real `tftWidth`/`tftHeight`, so on a
portrait panel the menu was centred for landscape. It now recomputes whenever
the geometry changes.

## Known issues

* The submenu scrollbar can leave thumb-shaped ghosts behind it, at both ends of
  the track. It does not reproduce in a stripped-down test firmware with the
  same fills, which points at AMOLED image retention (a bright element on black
  keeps glowing for a while) rather than at the driver. The bar now sits away
  from the bezel and is drawn as a dark track with a thumb, so leftover pixels
  are visible instead of hiding in the background; if the marks fade after the
  screen has been left alone for a minute, it is the panel, not the code.

## Support this port

The port is free and stays free (AGPLv3). If it saved you a weekend of
debugging and you want to say thanks: **https://boosty.to/arslanchik**

Bug reports and photos from your own unit are just as useful - especially of
any part of the UI that still gets clipped by the corners.

## Credits and licence

Based on [Bruce](https://github.com/pr3y/Bruce) by pr3y and contributors -
**AGPLv3**, see the repository `LICENSE`. This port keeps the whole Bruce tree,
so the source stays available for every published binary.
