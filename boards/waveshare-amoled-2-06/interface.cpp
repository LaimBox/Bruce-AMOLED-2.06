#include "core/bus_HAL.h"
#include "core/powerSave.h"
#include "core/utils.h"
#include <SD_MMC.h>
#include <Wire.h>
#include <XPowersLib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <interface.h>

// ---- Power (AXP2101) ----
static XPowersAXP2101 PMU;
#define AXP2101_ADDR 0x34 // default XPowersLib AXP2101 slave address
static bool pmuReady = false;

// ---- Shared I2C bus guard ----
// Touch is polled from the InputHandler task while the PMU (battery gauge,
// IRQ status) is read from the main task. Arduino's Wire is not re-entrant:
// two tasks talking to the bus at once makes the ESP-IDF I2C driver return
// ESP_ERR_INVALID_STATE and corrupt data. Every transaction goes through this
// recursive mutex instead.
static SemaphoreHandle_t i2cMutex = nullptr;

static inline void i2cInit(void) {
    if (i2cMutex == nullptr) { i2cMutex = xSemaphoreCreateRecursiveMutex(); }
}
static inline void i2cLock(void) {
    i2cInit();
    xSemaphoreTakeRecursive(i2cMutex, portMAX_DELAY);
}
static inline void i2cUnlock(void) { xSemaphoreGiveRecursive(i2cMutex); }

// ---- Touch (FT3168) ----
// Register map (Arduino_FT3x68):
//   0x02      finger count
//   0x03..0x06 X1 high, X1 low, Y1 high, Y1 low (12-bit each)
// The controller is factory calibrated to this panel and reports coordinates
// already in display pixels (0..409 / 0..501), so no scaling is applied.
// The INT pin is active-low and open-drain: it is the only reliable "is a
// finger down?" signal, so it gates every read (otherwise stale/garbage
// finger counts cause phantom menu navigation).
#define FT3168_RD_FINGERNUM 0x02
#define FT3168_RW_POWER_MODE 0xA5

static bool ft3168_poll(int16_t *x, int16_t *y) {
    if (digitalRead(TOUCH_INT) != LOW) return false;

    uint8_t buf[5];
    bool ok = true;

    i2cLock();
    Wire.beginTransmission(FT3168_ADDR);
    Wire.write(FT3168_RD_FINGERNUM);
    if (Wire.endTransmission(false) != 0) ok = false;
    else if (Wire.requestFrom((int)FT3168_ADDR, 5) != 5) ok = false;
    else {
        for (uint8_t i = 0; i < 5; i++) buf[i] = Wire.read();
    }
    i2cUnlock();

    if (!ok) return false;

    uint8_t fingers = buf[0] & 0x0F;
    if (fingers == 0 || fingers > 2) return false;

    int16_t px = ((buf[1] & 0x0F) << 8) | buf[2];
    int16_t py = ((buf[3] & 0x0F) << 8) | buf[4];

    // Sanity check against the panel resolution (410x502).
    if (px < 0 || px >= 410 || py < 0 || py >= 502) return false;

    *x = px;
    *y = py;
    return true;
}

/***************************************************************************************
** Function name: _setup_gpio()
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    bruceConfig.colorInverted = 0;
    Serial.begin(115200);
    delay(100);

    pinMode(BTN_PIN, INPUT_PULLUP);
    pinMode(TOUCH_INT, INPUT_PULLUP);

    // Touch reset pulse
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    delay(20);
    digitalWrite(TOUCH_RST, HIGH);
    delay(50);

    setSysI2CBus(&Wire);
    Wire.begin(SYS_I2C_SDA, SYS_I2C_SCL);

    // Put FT3168 into monitor (interrupt-triggered) power mode
    i2cLock();
    Wire.beginTransmission(FT3168_ADDR);
    Wire.write(FT3168_RW_POWER_MODE);
    Wire.write(0x01);
    Wire.endTransmission();
    i2cUnlock();

    i2cLock();
    pmuReady = PMU.init(Wire, SYS_I2C_SDA, SYS_I2C_SCL, AXP2101_ADDR);
    if (pmuReady) {
        PMU.enableBattDetection();
        PMU.enableSystemVoltageMeasure();
        PMU.enableGauge();
        PMU.enableCellbatteryCharge();
        // AXP2101 reports the power key through the PKEY bits of INTSTS2,
        // NOT through the generic XPOWERS_PWR_BTN_* bits (those map to SOC
        // warning levels on this chip).
        PMU.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ);
        PMU.clearIrqStatus();
    } else {
        Serial.println("AXP2101 PMU not found on I2C bus...");
    }
    i2cUnlock();
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Description:   second stage gpio setup
***************************************************************************************/
void _post_setup_gpio() {
    // Bruce's generic default is colorInverted = 1. This panel is a normal
    // (non-inverted) AMOLED - the official Waveshare example never enables
    // inversion - so an inverted display looks like a colour negative
    // ("acid" colours, unreadable icons). config.json may also carry an old
    // colorInverted value, so force it off again after the config was loaded.
    bruceConfig.colorInverted = 0;
    tft.invertDisplay(false);

    // AMOLED is self-emissive: brightness is set through the CO5300 driver
    // itself (tft.setBrightness), not via a PWM backlight pin.
    _setBrightness(100);

    // This board has no GPS and no Grove connector. Bruce's generic defaults
    // point the GPS UART at GROVE_SDA/SCL, which on this board are the same
    // GPIOs as the system I2C bus (14/15). Opening the GPS menu therefore
    // re-initialised UART1 on top of SDA/SCL and killed touch + PMU: the
    // screen froze and the battery readout dropped to 1%. Point the GPS UART
    // at invalid pins and hide the menu entry by default.
    bruceConfigPins.gps_bus.rx = (gpio_num_t)-1;
    bruceConfigPins.gps_bus.tx = (gpio_num_t)-1;
    bruceConfig.addDisabledMenu("GPS");
}

/***************************************************************************************
** Function name: getBattery()
** Description:   Battery percentage from the AXP2101 fuel gauge, via I2C
***************************************************************************************/
int getBattery() {
    // drawStatusBar() asks for the percentage on every repaint, and a gauge
    // read is a full I2C transaction. Cache it for a few seconds so the UI
    // stays responsive.
    static uint32_t lastRead = 0;
    static int cached = 100;

    uint32_t now = millis();
    if (lastRead != 0 && (now - lastRead) < 5000) return cached;
    lastRead = now;

    if (!pmuReady) return cached = 100;

    i2cLock();
    int percent = PMU.getBatteryPercent();
    bool batteryConnected = PMU.isBatteryConnect();
    i2cUnlock();

    // -1 means "no reading": either the pack is not connected (board is fed
    // from USB) or the gauge hasn't finished its first measurement. Do not
    // report 0/1% in that case - Bruce draws a red "empty" battery then.
    if (!batteryConnected || percent < 0 || percent > 100) cached = 100;
    else cached = percent < 1 ? 1 : percent;

    return cached;
}

bool isCharging() {
    if (!pmuReady) return false;
    i2cLock();
    bool charging = PMU.isCharging();
    i2cUnlock();
    return charging;
}

/*********************************************************************
** Function: _setBrightness
** Sets AMOLED brightness through the CO5300 driver (0-255), not PWM
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    uint8_t dutyCycle = (brightval * 255) / 100;
    tft.setBrightness(dutyCycle);
}

/*********************************************************************
** Function: InputHandler
** Handles PrevPress, NextPress, SelPress, AnyKeyPress, EscPress
** This board only has BOOT as a physical GPIO button; PWR is read
** through the AXP2101 PMU over I2C, not digitalRead.
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = 0;
    static bool touchWasDown = false;

    if (millis() - tm < 120 && !LongPress) return;

    // ---- BOOT (GPIO0) ----
    if (digitalRead(BTN_PIN) == LOW) {
        tm = millis();
        if (!wakeUpScreen()) AnyKeyPress = true;
        else return;
        SelPress = true;
        return;
    }

    // ---- PWR button (AXP2101 PKEY interrupts) ----
    if (pmuReady) {
        bool shortPress = false;
        bool longPress = false;
        i2cLock();
        PMU.getIrqStatus();
        shortPress = PMU.isPekeyShortPressIrq();
        longPress = PMU.isPekeyLongPressIrq();
        if (shortPress || longPress) PMU.clearIrqStatus();
        i2cUnlock();

        if (shortPress) {
            tm = millis();
            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;
            EscPress = true;
            return;
        }
        if (longPress) { LongPress = true; }
    }

    // ---- Touch (edge triggered: one event per finger-down) ----
    int16_t tx, ty;
    bool down = ft3168_poll(&tx, &ty);

    if (down) {
        if (!touchWasDown) {
            touchWasDown = true;
            tm = millis();
            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

            touchPoint.x = tx;
            touchPoint.y = ty;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        }
    } else {
        touchWasDown = false;
        touchPoint.pressed = false;
    }
}

/*********************************************************************
** Function: powerOff
**********************************************************************/
void powerOff() {
    if (pmuReady) PMU.shutdown();
}

/*********************************************************************
** Function: checkReboot
**********************************************************************/
void checkReboot() {}
