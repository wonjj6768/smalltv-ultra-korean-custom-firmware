// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * SmallTV-Ultra Korean Custom Firmware
 * Copyright (C) 2026 Times-Z
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <SPI.h>
#include <Logger.h>
#include <LittleFS.h>
#include <array>
#include <algorithm>
#include <tuple>
#include <cstring>
#include <Arduino.h>
#include <cmath>
#include <ctime>

#include "project_version.h"
#include "display/ClockDashboardScene.h"
#include "display/ClockDigitFont.h"
#include "display/UiTextFont.h"
#include "display/DisplayManager.h"
#include "config/ConfigManager.h"
#include "display/Gif.h"
#include "weather/WeatherClient.h"
#include "wireless/WiFiManager.h"

static Gif* g_gif = nullptr;

extern ConfigManager configManager;
extern WeatherClient* weatherClient;
extern WiFiManager* wifiManager;
extern bool g_legacyUpdateModeEnabled;
extern const char* AP_SSID;
#if defined(HOST_EXACT)
extern time_t g_hostNow;
#endif

static Arduino_HWSPI g_lcdBus = Arduino_HWSPI(LCD_DC_GPIO, -1, &SPI, true);
static Arduino_ST7789 g_lcd = Arduino_ST7789(&g_lcdBus, -1, 0, true, LCD_W, LCD_H);

static constexpr uint32_t LCD_HARDWARE_RESET_DELAY_MS = 120;
static constexpr uint32_t LCD_BEGIN_DELAY_MS = 10;
static constexpr int16_t DISPLAY_PADDING = 10;
static constexpr int16_t DISPLAY_INFO_Y = 100;
static constexpr int WRAP_MAX_LINE_SLOTS = 10;
static constexpr time_t CLOCK_REASONABLE_EPOCH = 1600000000UL;
static constexpr uint32_t STARTUP_CLOCK_PAUSE_MS = 12000;
static constexpr uint32_t TRANSIENT_CLOCK_PAUSE_MS = 5000;
static constexpr uint32_t BRIGHTNESS_SCHEDULE_CHECK_FALLBACK_MS = 3600000;
static constexpr int16_t CURRENT_WEATHER_ICON_Y = 0;
static constexpr int16_t CURRENT_WEATHER_BADGE_SIZE = 22;
static constexpr int16_t CURRENT_WEATHER_HEADER_TEXT_HEIGHT = 28;
static constexpr int16_t CLOCK_TIME_CANVAS_WIDTH = 212;
static constexpr int16_t CLOCK_SECONDS_CANVAS_WIDTH = 23;
static constexpr int16_t CLOCK_SECONDS_DIGIT_GAP = 2;

static unsigned long g_clockPausedUntilMs = 0;
static time_t g_lastClockDrawnSecond = 0;
static time_t g_lastClockStaticMinute = -1;
static bool g_clockLastHadValidTime = false;
static uint8_t g_clockLastLayoutKey = 0xFF;
static String g_clockTitleCache;
static String g_clockPrimaryTimeCache;
static String g_clockSecondaryBlockCache;
static String g_clockDateCache;
static String g_weatherTitleCache;
static String g_weatherCurrentCache;
static String g_todayHighCache;
static uint8_t g_lastAppliedBrightness = 0xFF;
static unsigned long g_nextBrightnessScheduleCheckMs = 0;
static std::array<String, 4> g_weatherForecastCache{};
static int g_currentWeatherIconCache = -999;
static std::array<int, 4> g_forecastWeatherIconCache = {-999, -999, -999, -999};
static String g_currentIpCache;
static bool g_updateAvailable = false;
static bool g_updateAvailableCache = false;
static bool g_currentUmbrellaBadgeCache = false;
static String g_waitLine1Cache;
static String g_waitLine2Cache;
static int16_t g_clockPrimaryRegionWidth = 0;
static int16_t g_clockSecondaryRegionX = -1;
static int16_t g_clockSecondaryRegionWidth = 0;
static Arduino_Canvas_Indexed* g_clockTimeCanvas = nullptr;
static int16_t g_clockTimeCanvasHeight = 0;
static String g_clockTimeCanvasCache;
static Arduino_Canvas_Indexed* g_clockSecondsCanvas = nullptr;
static char g_clockSecondsCanvasCache[3] = "";

static constexpr auto lcdColor565(uint8_t red, uint8_t green, uint8_t blue) -> uint16_t;
static constexpr auto lcdClockPrimaryTextColor() -> uint16_t;
static constexpr auto lcdClockSecondaryTextColor() -> uint16_t;
static constexpr auto lcdClockTemperatureTextColor() -> uint16_t;
static constexpr auto lcdClockDividerColor() -> uint16_t;
static constexpr auto lcdClockIpTextColor() -> uint16_t;
static auto lcdBlendColor565(uint16_t fgColor, uint16_t bgColor, uint8_t alpha, uint8_t maxAlpha) -> uint16_t;
static auto lcdReadPackedAlpha(const uint8_t* bitmap, size_t pixelIndex, uint8_t bitsPerPixel) -> uint8_t;

static void lcdDestroyIndexedCanvas(Arduino_Canvas_Indexed*& canvas) {
    if (canvas == nullptr) {
        return;
    }

    canvas->~Arduino_Canvas_Indexed();
    ::operator delete(canvas);
    canvas = nullptr;
}

static auto lcdClockTimeCanvasWidth() -> int16_t {
    return CLOCK_TIME_CANVAS_WIDTH;
}

static auto lcdCurrentTimeNow() -> time_t {
#if defined(HOST_EXACT)
    if (g_hostNow > 0) {
        return g_hostNow;
    }
#endif
    return time(nullptr);
}

static auto lcdClockTimeCanvasHeight() -> int16_t {
    const int16_t primaryHeight = static_cast<int16_t>(ClockDigitFont::fontSet(ClockDigitFont::Kind::Main).lineHeight);
    const int16_t secondaryHeight =
        static_cast<int16_t>(ClockDigitFont::fontSet(ClockDigitFont::Kind::Secondary).lineHeight);
    return static_cast<int16_t>(std::max<int16_t>(primaryHeight + 2, secondaryHeight + 12));
}

static auto lcdClockSecondsCanvasHeight() -> int16_t {
    const int16_t secondaryHeight =
        static_cast<int16_t>(ClockDigitFont::fontSet(ClockDigitFont::Kind::Secondary).lineHeight);
    return static_cast<int16_t>((secondaryHeight * 2) + CLOCK_SECONDS_DIGIT_GAP);
}

static auto lcdClockSecondsCanvasX() -> int16_t {
    return static_cast<int16_t>(ClockDashboard::TIME_LEFT_X + lcdClockTimeCanvasWidth());
}

static auto lcdClockSecondsCanvasY() -> int16_t {
    const int16_t primaryHeight = static_cast<int16_t>(ClockDigitFont::fontSet(ClockDigitFont::Kind::Main).lineHeight);
    return static_cast<int16_t>(ClockDashboard::TIME_TOP_Y + primaryHeight - lcdClockSecondsCanvasHeight());
}

static auto lcdIsNightBrightnessActive() -> bool {
    if (!configManager.isDisplayNightBrightnessEnabled()) {
        return false;
    }

    const time_t now = lcdCurrentTimeNow();
    if (now <= CLOCK_REASONABLE_EPOCH) {
        return false;
    }

    const tm* timeInfo = std::localtime(&now);
    if (timeInfo == nullptr) {
        return false;
    }

    const uint8_t currentHour = static_cast<uint8_t>(timeInfo->tm_hour);
    const uint8_t startHour = configManager.getDisplayNightStartHour();
    const uint8_t endHour = configManager.getDisplayNightEndHour();

    if (startHour == endHour) {
        return false;
    }
    if (startHour < endHour) {
        return currentHour >= startHour && currentHour < endHour;
    }
    return currentHour >= startHour || currentHour < endHour;
}

static auto lcdConfiguredBrightness() -> uint8_t {
    return lcdIsNightBrightnessActive() ? configManager.getDisplayNightBrightness()
                                        : configManager.getDisplayBrightness();
}

static auto lcdBrightnessScheduleDelayMs() -> uint32_t {
    const time_t now = lcdCurrentTimeNow();
    if (now <= CLOCK_REASONABLE_EPOCH) {
        return BRIGHTNESS_SCHEDULE_CHECK_FALLBACK_MS;
    }

    const tm* timeInfo = std::localtime(&now);
    if (timeInfo == nullptr) {
        return BRIGHTNESS_SCHEDULE_CHECK_FALLBACK_MS;
    }

    const int secondsUntilNextHour = ((59 - timeInfo->tm_min) * 60) + (60 - timeInfo->tm_sec);
    return static_cast<uint32_t>(secondsUntilNextHour * 1000);
}

static void lcdEnsureClockTimeCanvas() {
    if (g_clockTimeCanvas != nullptr) {
        return;
    }

    g_clockTimeCanvasHeight = lcdClockTimeCanvasHeight();
    g_clockTimeCanvas = new Arduino_Canvas_Indexed(lcdClockTimeCanvasWidth(), g_clockTimeCanvasHeight, &g_lcd,
                                                   ClockDashboard::TIME_LEFT_X, ClockDashboard::TIME_TOP_Y);
    if (g_clockTimeCanvas == nullptr) {
        Logger::warn("Clock time canvas allocation failed, falling back to direct LCD updates", "DisplayManager");
        g_clockTimeCanvasHeight = 0;
        return;
    }

    if (!g_clockTimeCanvas->begin(GFX_SKIP_OUTPUT_BEGIN)) {
        Logger::warn("Clock time canvas init failed, falling back to direct LCD updates", "DisplayManager");
        lcdDestroyIndexedCanvas(g_clockTimeCanvas);
        g_clockTimeCanvasHeight = 0;
        return;
    }

    g_clockTimeCanvas->fillScreen(LCD_BLACK);
    g_clockTimeCanvas->flush();
    Logger::info("Clock time canvas initialized", "DisplayManager");
}

static void lcdEnsureClockSecondsCanvas() {
    if (g_clockSecondsCanvas != nullptr) {
        return;
    }

    g_clockSecondsCanvas =
        new Arduino_Canvas_Indexed(CLOCK_SECONDS_CANVAS_WIDTH, lcdClockSecondsCanvasHeight(), &g_lcd,
                                   lcdClockSecondsCanvasX(), lcdClockSecondsCanvasY());
    if (g_clockSecondsCanvas == nullptr) {
        Logger::warn("Clock seconds canvas allocation failed, falling back to direct LCD updates", "DisplayManager");
        return;
    }

    if (!g_clockSecondsCanvas->begin(GFX_SKIP_OUTPUT_BEGIN)) {
        Logger::warn("Clock seconds canvas init failed, falling back to direct LCD updates", "DisplayManager");
        lcdDestroyIndexedCanvas(g_clockSecondsCanvas);
        return;
    }

    g_clockSecondsCanvas->fillScreen(LCD_BLACK);
    g_clockSecondsCanvas->flush();
    Logger::info("Clock seconds canvas initialized", "DisplayManager");
}

static void lcdResetClockLayoutCache() {
    g_clockLastLayoutKey = 0xFF;
    g_lastClockStaticMinute = -1;
    g_clockTitleCache = "";
    g_clockPrimaryTimeCache = "";
    g_clockSecondaryBlockCache = "";
    g_clockDateCache = "";
    g_weatherTitleCache = "";
    g_weatherCurrentCache = "";
    g_todayHighCache = "";
    g_waitLine1Cache = "";
    g_waitLine2Cache = "";
    g_clockPrimaryRegionWidth = 0;
    g_clockSecondaryRegionX = -1;
    g_clockSecondaryRegionWidth = 0;
    g_clockTimeCanvasCache = "";
    g_clockSecondsCanvasCache[0] = '\0';
    g_currentWeatherIconCache = -999;
    g_currentIpCache = "";
    g_updateAvailableCache = false;
    g_currentUmbrellaBadgeCache = false;
    g_forecastWeatherIconCache.fill(-999);
    for (auto& line : g_weatherForecastCache) {
        line = "";
    }
}

static void lcdDrawTextWrapped(Arduino_GFX* target, int16_t startX, int16_t startY, const String& text,
                               uint8_t textSize, uint16_t fgColor, uint16_t bgColor, bool clearBg);
static void lcdConfigureTextRenderer(Arduino_GFX* target, uint8_t textSize, uint16_t fgColor, uint16_t bgColor);
static auto lcdMeasureTextWidth(Arduino_GFX* target, const String& text) -> int16_t;
static void lcdResetTextRenderer(Arduino_GFX* target);
static auto lcdMeasureTextWidthForSize(Arduino_GFX* target, const String& text, uint8_t textSize) -> int16_t;
static auto lcdSelectTextSizeToFit(Arduino_GFX* target, const String& text, uint8_t maxSize, uint8_t minSize,
                                   int16_t maxWidth) -> uint8_t;
static auto lcdTrimTextToWidth(Arduino_GFX* target, const String& text, uint8_t textSize, int16_t maxWidth) -> String;
static auto lcdMeasureSevenSegmentTextWidth(const String& text, int16_t digitHeight) -> int16_t;
static void lcdDrawTextAt(Arduino_GFX* target, int16_t xPos, int16_t topY, const String& text, uint8_t textSize,
                          uint16_t fgColor, uint16_t bgColor);
static auto lcdVectorPixelWidth(char value, int16_t digitHeight) -> int16_t;
static auto lcdSevenSegmentCellWidth(char value, int16_t digitHeight) -> int16_t;
static void lcdDrawSevenSegmentDigit(Arduino_GFX* target, int16_t xPos, int16_t topY, char value, int16_t digitHeight,
                                     uint16_t color);
static void lcdDrawSevenSegmentText(Arduino_GFX* target, int16_t xPos, int16_t topY, const String& text,
                                    int16_t digitHeight, uint16_t color);
static void lcdDrawClockDashboardIcons(Arduino_GFX* target, const ClockDashboard::Scene& scene, bool forceRedraw,
                                       bool useCache);
static auto lcdForecastMetricColor(const String& text) -> uint16_t;

static void lcdDrawCenteredText(Arduino_GFX* target, int16_t topY, const String& text, uint8_t textSize,
                                uint16_t fgColor, uint16_t bgColor, bool clearBg, int16_t minX, int16_t maxWidth);

static void lcdDrawClockTimeCanvas(const String& primaryTime, const String& secondsDigits, const String& suffixText,
                                   int16_t primaryHeight, int16_t secondaryHeight, int16_t secondaryX,
                                   int16_t secondaryDigitsX, int16_t secondaryY, int16_t secondaryBlockWidth,
                                   int16_t suffixY) {
    if (g_clockTimeCanvas == nullptr) {
        return;
    }

    const String cacheKey =
        primaryTime + "|" + secondsDigits + "|" + suffixText + "|" + String(secondaryX) + "|" + String(secondaryBlockWidth);
    if (g_clockTimeCanvasCache == cacheKey) {
        return;
    }

    g_clockTimeCanvas->fillScreen(LCD_BLACK);

    if (!primaryTime.isEmpty()) {
        lcdDrawSevenSegmentText(g_clockTimeCanvas, 0, 0, primaryTime, primaryHeight, lcdClockPrimaryTextColor());
    }
    if (!secondsDigits.isEmpty()) {
        lcdDrawSevenSegmentText(g_clockTimeCanvas,
                                static_cast<int16_t>(secondaryDigitsX - ClockDashboard::TIME_LEFT_X), secondaryY,
                                secondsDigits, secondaryHeight, lcdClockSecondaryTextColor());
    }
    if (!suffixText.isEmpty()) {
        lcdDrawCenteredText(g_clockTimeCanvas, suffixY, suffixText, 1, lcdClockSecondaryTextColor(), LCD_BLACK, false,
                            static_cast<int16_t>(secondaryX - ClockDashboard::TIME_LEFT_X), secondaryBlockWidth);
    }

    g_clockTimeCanvas->flush();
    g_clockTimeCanvasCache = cacheKey;
}

static void lcdDrawClockSecondsCanvas(const char* secondsDigits) {
    if (secondsDigits == nullptr) {
        secondsDigits = "";
    }

    if (strncmp(g_clockSecondsCanvasCache, secondsDigits, sizeof(g_clockSecondsCanvasCache)) == 0) {
        return;
    }

    Arduino_GFX* secondsTarget = g_clockSecondsCanvas != nullptr ? static_cast<Arduino_GFX*>(g_clockSecondsCanvas) : &g_lcd;
    const int16_t originX = g_clockSecondsCanvas != nullptr ? 0 : lcdClockSecondsCanvasX();
    const int16_t originY = g_clockSecondsCanvas != nullptr ? 0 : lcdClockSecondsCanvasY();
    const int16_t secondaryHeight =
        static_cast<int16_t>(ClockDigitFont::fontSet(ClockDigitFont::Kind::Secondary).lineHeight);
    const int16_t canvasHeight = lcdClockSecondsCanvasHeight();

    secondsTarget->fillRect(originX, originY, CLOCK_SECONDS_CANVAS_WIDTH, canvasHeight, LCD_BLACK);

    if (secondsDigits[0] != '\0' && secondsDigits[1] != '\0') {
        const char tensDigit = secondsDigits[0];
        const char onesDigit = secondsDigits[1];
        const int16_t tensWidth = lcdSevenSegmentCellWidth(tensDigit, secondaryHeight);
        const int16_t onesWidth = lcdSevenSegmentCellWidth(onesDigit, secondaryHeight);
        const int16_t tensX = static_cast<int16_t>(originX + ((CLOCK_SECONDS_CANVAS_WIDTH - tensWidth) / 2));
        const int16_t onesX = static_cast<int16_t>(originX + ((CLOCK_SECONDS_CANVAS_WIDTH - onesWidth) / 2));
        const int16_t tensDrawX = static_cast<int16_t>(
            tensX + std::max<int16_t>((tensWidth - lcdVectorPixelWidth(tensDigit, secondaryHeight)) / 2, 0));
        const int16_t onesDrawX = static_cast<int16_t>(
            onesX + std::max<int16_t>((onesWidth - lcdVectorPixelWidth(onesDigit, secondaryHeight)) / 2, 0));
        const int16_t firstDigitY = originY;
        const int16_t secondDigitY = static_cast<int16_t>(originY + secondaryHeight + CLOCK_SECONDS_DIGIT_GAP);

        lcdDrawSevenSegmentDigit(secondsTarget, tensDrawX, firstDigitY, tensDigit, secondaryHeight,
                                 lcdClockSecondaryTextColor());
        lcdDrawSevenSegmentDigit(secondsTarget, onesDrawX, secondDigitY, onesDigit, secondaryHeight,
                                 lcdClockSecondaryTextColor());
    }

    if (g_clockSecondsCanvas != nullptr) {
        g_clockSecondsCanvas->flush();
    }
    strncpy(g_clockSecondsCanvasCache, secondsDigits, sizeof(g_clockSecondsCanvasCache) - 1);
    g_clockSecondsCanvasCache[sizeof(g_clockSecondsCanvasCache) - 1] = '\0';
}

static void lcdFlushClockCanvases() {
    if (g_clockTimeCanvas != nullptr) {
        g_clockTimeCanvas->flush();
    }
    if (g_clockSecondsCanvas != nullptr) {
        g_clockSecondsCanvas->flush();
    }
}

static void lcdResetWeatherCache() {
    g_weatherTitleCache = "";
    g_weatherCurrentCache = "";
    g_todayHighCache = "";
    g_currentWeatherIconCache = -999;
    g_currentUmbrellaBadgeCache = false;
    g_forecastWeatherIconCache.fill(-999);
    for (auto& line : g_weatherForecastCache) {
        line = "";
    }
}

static void lcdDrawDashboardChrome(Arduino_GFX* target, bool showWeather) {
    target->fillScreen(LCD_BLACK);
    if (!showWeather) {
        return;
    }

    for (int16_t index = 1; index < 4; ++index) {
        const int16_t separatorX = static_cast<int16_t>(
            ClockDashboard::FORECAST_LEFT + (index * (ClockDashboard::FORECAST_WIDTH + ClockDashboard::FORECAST_GAP)) -
            (ClockDashboard::FORECAST_GAP / 2));
        target->drawFastVLine(separatorX, ClockDashboard::FORECAST_TOP,
                              static_cast<int16_t>(ClockDashboard::FORECAST_HEIGHT - 9), lcdClockDividerColor());
    }
}

static auto lcdForecastMetricColor(const String& text) -> uint16_t {
    if (text.endsWith("mm")) {
        return lcdColor565(120, 196, 255);
    }
    if (text.endsWith("%")) {
        return lcdColor565(198, 204, 210);
    }
    return lcdClockPrimaryTextColor();
}

static constexpr auto lcdColor565(uint8_t red, uint8_t green, uint8_t blue) -> uint16_t {
    return static_cast<uint16_t>(((red & 0xF8U) << 8) | ((green & 0xFCU) << 3) | (blue >> 3));
}

static constexpr auto lcdClockPrimaryTextColor() -> uint16_t {
    return lcdColor565(245, 247, 248);
}

static constexpr auto lcdClockSecondaryTextColor() -> uint16_t {
    return lcdColor565(143, 183, 198);
}

static constexpr auto lcdClockDateTextColor() -> uint16_t {
    return lcdColor565(164, 176, 182);
}

static constexpr auto lcdClockForecastHourTextColor() -> uint16_t {
    return lcdColor565(150, 208, 224);
}

static constexpr auto lcdClockTemperatureTextColor() -> uint16_t {
    return lcdColor565(255, 179, 138);
}

static constexpr auto lcdClockDividerColor() -> uint16_t {
    return lcdColor565(82, 96, 103);
}

static constexpr auto lcdClockIpTextColor() -> uint16_t {
    return lcdColor565(36, 40, 44);
}

static auto lcdBlendColor565(uint16_t fgColor, uint16_t bgColor, uint8_t alpha, uint8_t maxAlpha) -> uint16_t {
    if (alpha == 0U) {
        return bgColor;
    }
    if (alpha >= maxAlpha) {
        return fgColor;
    }

    const uint8_t fgR = static_cast<uint8_t>((fgColor >> 11U) & 0x1FU);
    const uint8_t fgG = static_cast<uint8_t>((fgColor >> 5U) & 0x3FU);
    const uint8_t fgB = static_cast<uint8_t>(fgColor & 0x1FU);
    const uint8_t bgR = static_cast<uint8_t>((bgColor >> 11U) & 0x1FU);
    const uint8_t bgG = static_cast<uint8_t>((bgColor >> 5U) & 0x3FU);
    const uint8_t bgB = static_cast<uint8_t>(bgColor & 0x1FU);
    const uint8_t invAlpha = static_cast<uint8_t>(maxAlpha - alpha);

    const uint8_t outR = static_cast<uint8_t>((fgR * alpha + bgR * invAlpha + (maxAlpha / 2U)) / maxAlpha);
    const uint8_t outG = static_cast<uint8_t>((fgG * alpha + bgG * invAlpha + (maxAlpha / 2U)) / maxAlpha);
    const uint8_t outB = static_cast<uint8_t>((fgB * alpha + bgB * invAlpha + (maxAlpha / 2U)) / maxAlpha);

    return static_cast<uint16_t>((outR << 11U) | (outG << 5U) | outB);
}

static auto lcdReadPackedAlpha(const uint8_t* bitmap, size_t pixelIndex, uint8_t bitsPerPixel) -> uint8_t {
    switch (bitsPerPixel) {
        case 1: {
            const uint8_t packed = pgm_read_byte(&bitmap[pixelIndex / 8U]);
            const uint8_t mask = static_cast<uint8_t>(1U << (7U - (pixelIndex % 8U)));
            return (packed & mask) != 0U ? 1U : 0U;
        }
        case 2: {
            const uint8_t packed = pgm_read_byte(&bitmap[pixelIndex / 4U]);
            const uint8_t shift = static_cast<uint8_t>(6U - ((pixelIndex % 4U) * 2U));
            uint8_t alpha = static_cast<uint8_t>((packed >> shift) & 0x03U);
            if (alpha > 0U && alpha < 3U) {
                alpha = static_cast<uint8_t>(alpha + 1U);
            }
            return alpha;
        }
        case 4: {
            const uint8_t packed = pgm_read_byte(&bitmap[pixelIndex / 2U]);
            const uint8_t shift = static_cast<uint8_t>((pixelIndex % 2U) == 0U ? 4U : 0U);
            uint8_t alpha = static_cast<uint8_t>((packed >> shift) & 0x0FU);
            if (alpha > 0U && alpha < 15U) {
                alpha = static_cast<uint8_t>(std::min<int>(15, alpha + 1));
            }
            return alpha;
        }
        default:
            return 0U;
    }
}

static auto lcdShouldShowUmbrellaBadge(const WeatherClient::Snapshot& weather) -> bool {
    if (weather.currentPrecipitation > 0.0F || weather.currentRain > 0.0F) {
        return true;
    }

    for (const auto& entry : weather.forecast) {
        if (entry.timestamp > 0 && entry.precipitation > 0.0F) {
            return true;
        }
    }

    return false;
}

static auto lcdSizedWeatherIconPath(const char* slot, int16_t maxSize) -> String {
    return String("/weather-icons/") + slot + "-" + String(maxSize) + ".bmp";
}

static auto lcdDrawBmpIcon(Arduino_GFX* target, const String& path, int16_t x, int16_t y, int16_t maxSize,
                           bool useColor) -> bool {
    if (!LittleFS.exists(path)) {
        return false;
    }

    File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }

    uint8_t header[54];
    if (file.read(header, sizeof(header)) != sizeof(header) || header[0] != 'B' || header[1] != 'M') {
        file.close();
        return false;
    }

    const uint32_t dataOffset = static_cast<uint32_t>(header[10]) | (static_cast<uint32_t>(header[11]) << 8) |
                                (static_cast<uint32_t>(header[12]) << 16) |
                                (static_cast<uint32_t>(header[13]) << 24);
    const int32_t width = static_cast<int32_t>(header[18]) | (static_cast<int32_t>(header[19]) << 8) |
                          (static_cast<int32_t>(header[20]) << 16) | (static_cast<int32_t>(header[21]) << 24);
    const int32_t height = static_cast<int32_t>(header[22]) | (static_cast<int32_t>(header[23]) << 8) |
                           (static_cast<int32_t>(header[24]) << 16) | (static_cast<int32_t>(header[25]) << 24);
    const uint16_t bitsPerPixel = static_cast<uint16_t>(header[28]) | (static_cast<uint16_t>(header[29]) << 8);

    if (width <= 0 || height == 0 || bitsPerPixel != 24) {
        file.close();
        return false;
    }

    const int32_t absHeight = height > 0 ? height : -height;
    const size_t rowSize = static_cast<size_t>(((width * 3) + 3) & ~3);
    std::array<uint8_t, 512> row{};
    if (rowSize > row.size()) {
        file.close();
        return false;
    }

    int16_t drawWidth = maxSize;
    int16_t drawHeight = maxSize;
    if (width > 0 && absHeight > 0) {
        const float scale = std::min(static_cast<float>(maxSize) / static_cast<float>(width),
                                     static_cast<float>(maxSize) / static_cast<float>(absHeight));
        drawWidth = static_cast<int16_t>(std::max(1L, lroundf(static_cast<float>(width) * scale)));
        drawHeight = static_cast<int16_t>(std::max(1L, lroundf(static_cast<float>(absHeight) * scale)));
    }
    const int16_t xOffset = static_cast<int16_t>((maxSize - drawWidth) / 2);
    const int16_t yOffset = static_cast<int16_t>((maxSize - drawHeight) / 2);

    for (int32_t rowIndex = 0; rowIndex < absHeight && rowIndex < drawHeight; ++rowIndex) {
        const int32_t sampleRow =
            std::min<int32_t>(absHeight - 1, static_cast<int32_t>((static_cast<int64_t>(rowIndex) * absHeight) / drawHeight));
        const int32_t sourceRow = height > 0 ? (absHeight - 1 - sampleRow) : sampleRow;
        file.seek(dataOffset + static_cast<uint32_t>(sourceRow) * static_cast<uint32_t>(rowSize), SeekSet);
        if (file.read(row.data(), rowSize) != static_cast<int>(rowSize)) {
            file.close();
            return false;
        }

        for (int32_t col = 0; col < drawWidth; ++col) {
            const int32_t sampleCol =
                std::min<int32_t>(width - 1, static_cast<int32_t>((static_cast<int64_t>(col) * width) / drawWidth));
            const size_t offset = static_cast<size_t>(sampleCol) * 3U;
            const uint8_t blue = row[offset];
            const uint8_t green = row[offset + 1U];
            const uint8_t red = row[offset + 2U];
            if (useColor) {
                if (red > 8U || green > 8U || blue > 8U) {
                    target->drawPixel(x + xOffset + static_cast<int16_t>(col), y + yOffset + static_cast<int16_t>(rowIndex),
                                      lcdColor565(red, green, blue));
                }
            } else {
                const uint16_t brightness = static_cast<uint16_t>(red) + static_cast<uint16_t>(green) +
                                            static_cast<uint16_t>(blue);
                if (brightness > 96U) {
                    target->drawPixel(x + xOffset + static_cast<int16_t>(col), y + yOffset + static_cast<int16_t>(rowIndex),
                                      LCD_WHITE);
                }
            }
        }
    }

    file.close();
    return true;
}

static void lcdDrawUmbrellaBadge(Arduino_GFX* target, int16_t x, int16_t y, int16_t size) {
    const int16_t iconInset = static_cast<int16_t>(std::max<int16_t>(2, size / 7));
    const int16_t iconSize = static_cast<int16_t>(size - (iconInset * 2));
    const String iconPath = lcdSizedWeatherIconPath("umbrella", iconSize);
    if (!LittleFS.exists(iconPath)) {
        return;
    }

    const uint16_t badgeBg = lcdColor565(20, 24, 30);
    const uint16_t badgeBorder = lcdColor565(118, 140, 150);
    const int16_t centerX = static_cast<int16_t>(x + (size / 2));
    const int16_t centerY = static_cast<int16_t>(y + (size / 2));
    const int16_t radius = static_cast<int16_t>(size / 2);

    target->fillCircle(centerX, centerY, radius, badgeBg);
    target->drawCircle(centerX, centerY, radius, badgeBorder);
    lcdDrawBmpIcon(target, iconPath, static_cast<int16_t>(x + iconInset), static_cast<int16_t>(y + iconInset), iconSize,
                   true);
}

static void lcdDrawWeatherIcon(Arduino_GFX* target, int weatherCode, int16_t x, int16_t y, int16_t maxSize = 20,
                               bool useColor = false) {
    const String path = lcdSizedWeatherIconPath(ClockDashboard::weatherIconSlot(weatherCode), maxSize);
    target->fillRect(x, y, maxSize, maxSize, LCD_BLACK);
    lcdDrawBmpIcon(target, path, x, y, maxSize, useColor);
}

static auto lcdSevenSegmentGap(int16_t digitHeight) -> int16_t {
    const ClockDigitFont::Kind kind = digitHeight >= 40 ? ClockDigitFont::Kind::Main : ClockDigitFont::Kind::Secondary;
    return ClockDigitFont::fontSet(kind).tracking;
}

static auto lcdVectorPixelWidth(char value, int16_t digitHeight) -> int16_t {
    const ClockDigitFont::Kind kind = digitHeight >= 40 ? ClockDigitFont::Kind::Main : ClockDigitFont::Kind::Secondary;
    const ClockDigitFont::Glyph* glyph = ClockDigitFont::glyph(kind, value);
    return glyph == nullptr ? 0 : glyph->width;
}

static auto lcdSevenSegmentCellWidth(char value, int16_t digitHeight) -> int16_t {
    if (value == ':') {
        return 20;
    }

    int16_t maxWidth = 0;
    for (char digit = '0'; digit <= '9'; ++digit) {
        maxWidth = std::max<int16_t>(maxWidth, lcdVectorPixelWidth(digit, digitHeight));
    }
    return maxWidth;
}

static void lcdDrawSevenSegmentDigit(Arduino_GFX* target, int16_t xPos, int16_t topY, char value, int16_t digitHeight,
                                     uint16_t color) {
    const ClockDigitFont::Kind kind = digitHeight >= 40 ? ClockDigitFont::Kind::Main : ClockDigitFont::Kind::Secondary;
    const ClockDigitFont::Glyph* glyph = ClockDigitFont::glyph(kind, value);
    if (glyph == nullptr) {
        return;
    }

    const ClockDigitFont::FontSet& fontSet = ClockDigitFont::fontSet(kind);
    const uint8_t maxAlpha = static_cast<uint8_t>((1U << fontSet.bitsPerPixel) - 1U);
    const int16_t drawY = static_cast<int16_t>(topY + glyph->yOffset);
    const size_t pixelCount = static_cast<size_t>(glyph->width) * static_cast<size_t>(glyph->height);
    for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        const uint8_t alpha = lcdReadPackedAlpha(glyph->bitmap, pixelIndex, fontSet.bitsPerPixel);
        if (alpha == 0U) {
            continue;
        }

        const int16_t pixelX = static_cast<int16_t>(xPos + (pixelIndex % glyph->width));
        const int16_t pixelY = static_cast<int16_t>(drawY + (pixelIndex / glyph->width));
        target->drawPixel(pixelX, pixelY, lcdBlendColor565(color, LCD_BLACK, alpha, maxAlpha));
    }
}

static void lcdDrawSevenSegmentColon(Arduino_GFX* target, int16_t xPos, int16_t topY, int16_t digitHeight,
                                     uint16_t color) {
    lcdDrawSevenSegmentDigit(target, xPos, topY, ':', digitHeight, color);
}

static void lcdDrawCenteredText(Arduino_GFX* target, int16_t topY, const String& text, uint8_t textSize,
                                uint16_t fgColor, uint16_t bgColor, bool clearBg, int16_t minX, int16_t maxWidth) {
    const int16_t textWidth = lcdMeasureTextWidthForSize(target, text, textSize);

    int16_t drawX = minX;
    if (maxWidth > textWidth) {
        drawX = static_cast<int16_t>(minX + ((maxWidth - textWidth) / 2));
    }
    if (drawX < minX) {
        drawX = minX;
    }

    lcdDrawTextWrapped(target, drawX, topY, text, textSize, fgColor, bgColor, clearBg);
}

static void lcdDrawCachedLine(Arduino_GFX* target, String& cache, int16_t yPos, int16_t height, const String& text,
                              uint8_t textSize) {
    if (cache == text) {
        return;
    }

    target->fillRect(0, yPos, ClockDashboard::SCREEN_W, height, LCD_BLACK);
    if (!text.isEmpty()) {
        lcdDrawTextWrapped(target, DISPLAY_PADDING, yPos, text, textSize, LCD_WHITE, LCD_BLACK, false);
    }
    cache = text;
}

static auto lcdString(const std::string& value) -> String { return String(value.c_str()); }

static auto lcdBuildClockDashboardScene(time_t now, bool validTime, bool showClock, bool showWeather)
    -> ClockDashboard::Scene {
    ClockDashboard::Input input;
    input.showClock = showClock;
    input.showWeather = showWeather;
    input.use24Hour = configManager.isClockUse24Hour();
    input.showSeconds = false;
    input.validTime = validTime;
    input.isAccessPointMode = wifiManager != nullptr && wifiManager->isApMode();
    input.showLegacyUpdateRoute = g_legacyUpdateModeEnabled;
    input.now = now;
    input.accessPointSsid = AP_SSID != nullptr ? AP_SSID : "";

    if (showWeather && weatherClient != nullptr) {
        const WeatherClient::Snapshot& weather = weatherClient->getSnapshot();
        input.weather.hasData = weather.hasData;
        input.weather.isRaining = weather.isRaining;
        input.weather.currentTemperature = weather.currentTemperature;
        input.weather.currentRain = weather.currentRain;
        input.weather.currentPrecipitation = weather.currentPrecipitation;
        input.weather.currentPrecipitationProbability = weather.currentPrecipitationProbability;
        input.weather.currentHumidity = weather.currentHumidity;
        input.weather.currentWeatherCode = weather.currentWeatherCode;
        input.weather.timezone = weather.timezone.length() != 0 ? weather.timezone.c_str() : configManager.getWeatherTimezone();
        input.weather.locationName = configManager.getWeatherLocationName();
        input.weather.status = weather.status.c_str();

        const size_t forecastCopyCount = std::min(input.weather.forecast.size(), weather.forecast.size());
        for (size_t i = 0; i < forecastCopyCount; ++i) {
            input.weather.forecast[i].hasData = weather.forecast[i].timestamp > 0;
            input.weather.forecast[i].timestamp = weather.forecast[i].timestamp;
            input.weather.forecast[i].temperature = weather.forecast[i].temperature;
            input.weather.forecast[i].precipitation = weather.forecast[i].precipitation;
            input.weather.forecast[i].precipitationProbability = weather.forecast[i].precipitationProbability;
            input.weather.forecast[i].humidity = weather.forecast[i].humidity;
            input.weather.forecast[i].weatherCode = weather.forecast[i].weatherCode;
        }
    }

    if (input.weather.timezone.empty()) {
        input.weather.timezone = configManager.getTimezoneRegion();
    }
    return ClockDashboard::buildScene(input);
}

static void lcdDrawClockInner() {
    const time_t now = lcdCurrentTimeNow();
    const bool validTime = now > CLOCK_REASONABLE_EPOCH;
    const bool showClock = configManager.isClockEnabled();
    const bool showWeather = configManager.isWeatherEnabled();
    const bool showSeconds = showClock;
    const bool use24Hour = configManager.isClockUse24Hour();
    const uint8_t layoutKey = static_cast<uint8_t>((showClock ? 1U : 0U) | (showWeather ? 2U : 0U) |
                                                   (validTime ? 4U : 0U) | (showSeconds ? 8U : 0U) |
                                                   (use24Hour ? 16U : 0U));
    const time_t drawTick = showSeconds ? now : (now / 60);
    const time_t minuteTick = now / 60;

    if (g_lastClockDrawnSecond == drawTick && g_clockLastHadValidTime == validTime && g_clockLastLayoutKey == layoutKey) {
        return;
    }

    if (showSeconds && validTime && g_clockLastHadValidTime == validTime && g_clockLastLayoutKey == layoutKey &&
        g_lastClockStaticMinute == minuteTick) {
        tm* timeInfo = std::localtime(&now);
        char secondsDigits[3] = "";
        if (timeInfo != nullptr) {
            snprintf(secondsDigits, sizeof(secondsDigits), "%02d", timeInfo->tm_sec);
        }
        g_lastClockDrawnSecond = drawTick;
        lcdDrawClockSecondsCanvas(secondsDigits);
        return;
    }

    g_lastClockDrawnSecond = drawTick;
    g_clockLastHadValidTime = validTime;
    const bool forceStaticRedraw = g_clockLastLayoutKey != layoutKey;
    if (forceStaticRedraw) {
        lcdDrawDashboardChrome(&g_lcd, showWeather);
        lcdResetClockLayoutCache();
        g_clockLastLayoutKey = layoutKey;
    }

    Arduino_GFX* clockTarget = &g_lcd;
    const ClockDashboard::Scene scene = lcdBuildClockDashboardScene(now, validTime, showClock, showWeather);
    lcdEnsureClockSecondsCanvas();

    lcdDrawCachedLine(clockTarget, g_waitLine1Cache, ClockDashboard::WAIT_LINE_1_Y, ClockDashboard::WAIT_LINE_HEIGHT,
                      lcdString(scene.waitLine1), 2);
    lcdDrawCachedLine(clockTarget, g_waitLine2Cache, ClockDashboard::WAIT_LINE_2_Y, ClockDashboard::WAIT_LINE_HEIGHT,
                      lcdString(scene.waitLine2), 2);

    if (!scene.waitLine1.empty() || !scene.waitLine2.empty()) {
        lcdDrawClockSecondsCanvas("");
        return;
    }

    lcdDrawCachedLine(clockTarget, g_waitLine1Cache, ClockDashboard::WAIT_LINE_1_Y, ClockDashboard::WAIT_LINE_HEIGHT, "", 2);
    lcdDrawCachedLine(clockTarget, g_waitLine2Cache, ClockDashboard::WAIT_LINE_2_Y, ClockDashboard::WAIT_LINE_HEIGHT, "", 2);

    String headerLine;
    const int16_t currentIconX = static_cast<int16_t>(
        ClockDashboard::SCREEN_W - ClockDashboard::HEADER_RIGHT_PADDING - ClockDashboard::CURRENT_ICON_SIZE);
    const int16_t headerWidth = static_cast<int16_t>(ClockDashboard::SCREEN_W - ClockDashboard::HEADER_X -
                                                     ClockDashboard::HEADER_RIGHT_PADDING -
                                                     ClockDashboard::CURRENT_ICON_SIZE - 8);
    headerLine = lcdTrimTextToWidth(clockTarget, headerLine, 1, headerWidth);
    if (g_clockTitleCache != headerLine) {
        clockTarget->fillRect(0, ClockDashboard::HEADER_Y - 2, static_cast<int16_t>(currentIconX - 8),
                              CURRENT_WEATHER_HEADER_TEXT_HEIGHT, LCD_BLACK);
        g_currentWeatherIconCache = -999;
        g_currentIpCache = "";
        g_currentUmbrellaBadgeCache = false;
        if (!headerLine.isEmpty()) {
            lcdDrawTextAt(clockTarget, ClockDashboard::HEADER_X, ClockDashboard::HEADER_Y, headerLine, 1, LCD_WHITE,
                          LCD_BLACK);
        }
        g_clockTitleCache = headerLine;
    }

    const String currentIp =
        (wifiManager != nullptr) ? wifiManager->getIP().toString() : String("");
    const String currentIpValue = (currentIp == "0.0.0.0") ? String("") : currentIp;
    constexpr int16_t CURRENT_IP_X = 8;
    constexpr int16_t CURRENT_IP_Y = 4;
    constexpr int16_t CURRENT_IP_WIDTH = 78;
    constexpr int16_t CURRENT_IP_HEIGHT = 10;
    constexpr int16_t UPDATE_NOTICE_Y = 19;
    constexpr int16_t UPDATE_NOTICE_HEIGHT = 15;
    if (g_currentIpCache != currentIpValue || g_updateAvailableCache != g_updateAvailable) {
        clockTarget->fillRect(CURRENT_IP_X, CURRENT_IP_Y, CURRENT_IP_WIDTH,
                              CURRENT_IP_HEIGHT + UPDATE_NOTICE_HEIGHT, LCD_BLACK);
        if (!currentIpValue.isEmpty()) {
            const String trimmedIp = lcdTrimTextToWidth(clockTarget, currentIpValue, 1, CURRENT_IP_WIDTH);
            lcdDrawTextAt(clockTarget, CURRENT_IP_X, CURRENT_IP_Y, trimmedIp, 1, lcdClockIpTextColor(), LCD_BLACK);
        }
        if (g_updateAvailable) {
            lcdDrawTextAt(clockTarget, CURRENT_IP_X, UPDATE_NOTICE_Y, String(F("업데이트있음")), 1,
                          lcdClockIpTextColor(), LCD_BLACK);
        }
        g_currentIpCache = currentIpValue;
        g_updateAvailableCache = g_updateAvailable;
    }

    const String todayHighLabel = lcdString(scene.todayHighLabel);
    constexpr int16_t TODAY_HIGH_X = 86;
    constexpr int16_t TODAY_HIGH_Y = CURRENT_IP_Y;
    constexpr uint8_t TODAY_HIGH_SIZE = 2;
    constexpr int16_t TODAY_HIGH_HEIGHT = 20;
    const int16_t todayHighWidth = static_cast<int16_t>(currentIconX - TODAY_HIGH_X - 4);
    if (g_todayHighCache != todayHighLabel) {
        clockTarget->fillRect(TODAY_HIGH_X, TODAY_HIGH_Y, todayHighWidth, TODAY_HIGH_HEIGHT, LCD_BLACK);
        if (!todayHighLabel.isEmpty() && todayHighWidth > 0) {
            const String trimmedHigh = lcdTrimTextToWidth(clockTarget, todayHighLabel, TODAY_HIGH_SIZE, todayHighWidth);
            lcdDrawTextAt(clockTarget, TODAY_HIGH_X, TODAY_HIGH_Y, trimmedHigh, TODAY_HIGH_SIZE,
                          lcdClockDateTextColor(), LCD_BLACK);
        }
        g_todayHighCache = todayHighLabel;
    }

    String primaryTime = scene.clockPrimary.empty() ? lcdString(scene.clockTime) : lcdString(scene.clockPrimary);
    tm* timeInfo = std::localtime(&now);
    char secondsDigits[3] = "";
    if (showClock && validTime && timeInfo != nullptr) {
        snprintf(secondsDigits, sizeof(secondsDigits), "%02d", timeInfo->tm_sec);
    }
    const String suffixText = lcdString(scene.clockSuffix);
    const uint8_t suffixSize = suffixText.isEmpty() ? 0U : 1U;
    const int16_t suffixWidth = suffixText.isEmpty() ? 0 : lcdMeasureTextWidthForSize(clockTarget, suffixText, suffixSize);

    const int16_t primaryHeight = static_cast<int16_t>(ClockDigitFont::fontSet(ClockDigitFont::Kind::Main).lineHeight);
    const int16_t secondaryHeight = 0;
    const int16_t mainTimeWidth = lcdMeasureSevenSegmentTextWidth(primaryTime, primaryHeight);
    const int16_t secondaryBlockWidth = suffixWidth;
    const int16_t secondaryX =
        secondaryBlockWidth == 0
            ? 0
            : static_cast<int16_t>(ClockDashboard::SCREEN_W - ClockDashboard::TIME_RIGHT_PADDING - secondaryBlockWidth);
    const int16_t secondaryDigitsX = 0;
    const int16_t secondaryY = 0;
    const int16_t suffixY = static_cast<int16_t>(ClockDashboard::TIME_TOP_Y + 2);
    const int16_t timeBlockHeight =
        g_clockTimeCanvas != nullptr ? g_clockTimeCanvasHeight : static_cast<int16_t>(primaryHeight + 2);
    if (g_clockTimeCanvas != nullptr) {
        lcdDrawClockTimeCanvas(primaryTime, "", suffixText, primaryHeight, secondaryHeight, secondaryX,
                               secondaryDigitsX, static_cast<int16_t>(secondaryY - ClockDashboard::TIME_TOP_Y),
                               secondaryBlockWidth, static_cast<int16_t>(suffixY - ClockDashboard::TIME_TOP_Y));
        g_clockPrimaryTimeCache = primaryTime;
        g_clockPrimaryRegionWidth = mainTimeWidth;
        g_clockSecondaryBlockCache = suffixText + "|" + String(secondaryX) + "|" + String(secondaryBlockWidth);
        g_clockSecondaryRegionX = secondaryBlockWidth > 0 ? secondaryX : -1;
        g_clockSecondaryRegionWidth = secondaryBlockWidth;
    } else {
        if (g_clockPrimaryTimeCache != primaryTime) {
            const int16_t clearWidth = std::max<int16_t>(g_clockPrimaryRegionWidth, mainTimeWidth);
            if (clearWidth > 0) {
                clockTarget->fillRect(ClockDashboard::TIME_LEFT_X, ClockDashboard::TIME_TOP_Y,
                                      static_cast<int16_t>(clearWidth + 2), timeBlockHeight, LCD_BLACK);
            }
            if (!primaryTime.isEmpty()) {
                lcdDrawSevenSegmentText(clockTarget, ClockDashboard::TIME_LEFT_X, ClockDashboard::TIME_TOP_Y, primaryTime,
                                        primaryHeight, lcdClockPrimaryTextColor());
            }
            g_clockPrimaryTimeCache = primaryTime;
            g_clockPrimaryRegionWidth = mainTimeWidth;
        }

        const String secondaryBlockKey = suffixText + "|" + String(secondaryX) + "|" + String(secondaryBlockWidth);
        if (g_clockSecondaryBlockCache != secondaryBlockKey) {
            int16_t clearX = secondaryX;
            int16_t clearWidth = secondaryBlockWidth;
            if (g_clockSecondaryRegionWidth > 0) {
                if (clearWidth <= 0) {
                    clearX = g_clockSecondaryRegionX;
                    clearWidth = g_clockSecondaryRegionWidth;
                } else {
                    const int16_t previousRight =
                        static_cast<int16_t>(g_clockSecondaryRegionX + g_clockSecondaryRegionWidth);
                    const int16_t currentRight = static_cast<int16_t>(secondaryX + secondaryBlockWidth);
                    clearX = std::min<int16_t>(g_clockSecondaryRegionX, secondaryX);
                    clearWidth = static_cast<int16_t>(std::max<int16_t>(previousRight, currentRight) - clearX);
                }
            }

            if (clearWidth > 0) {
                clockTarget->fillRect(clearX, ClockDashboard::TIME_TOP_Y, clearWidth, timeBlockHeight, LCD_BLACK);
            }

            if (!suffixText.isEmpty()) {
                lcdDrawCenteredText(clockTarget, suffixY, suffixText, suffixSize, lcdClockSecondaryTextColor(), LCD_BLACK,
                                    false, secondaryX, secondaryBlockWidth);
            }

            g_clockSecondaryBlockCache = secondaryBlockKey;
            g_clockSecondaryRegionX = secondaryBlockWidth > 0 ? secondaryX : -1;
            g_clockSecondaryRegionWidth = secondaryBlockWidth;
        }
    }

    lcdDrawClockSecondsCanvas(secondsDigits);

    String clockDate = lcdString(scene.locationName);
    if (!scene.clockDate.empty()) {
        if (!clockDate.isEmpty()) {
            clockDate += " ";
        }
        clockDate += lcdString(scene.clockDate);
    }
    const uint8_t dateSize =
        clockDate.isEmpty()
            ? 1U
            : lcdSelectTextSizeToFit(clockTarget, clockDate, 2, 1, static_cast<int16_t>(ClockDashboard::SCREEN_W - 24));
    const UiTextFont::Kind dateKind = dateSize >= 2 ? UiTextFont::Kind::Large : UiTextFont::Kind::Small;
    const int16_t dateHeight = static_cast<int16_t>(UiTextFont::fontSet(dateKind).lineHeight);
    const int16_t preferredDateY = static_cast<int16_t>(ClockDashboard::TIME_TOP_Y + primaryHeight + 14);
    const int16_t maxDateY =
        static_cast<int16_t>(ClockDashboard::FORECAST_TOP - dateHeight - 3);
    const int16_t dateY = std::min<int16_t>(preferredDateY, maxDateY);
    const String clockDateKey = clockDate + "|" + String(dateY) + "|" + String(dateSize);
    if (g_clockDateCache != clockDateKey) {
        clockTarget->fillRect(0, dateY, ClockDashboard::SCREEN_W, static_cast<int16_t>(dateHeight + 3), LCD_BLACK);
        if (!clockDate.isEmpty()) {
            lcdDrawCenteredText(clockTarget, dateY, clockDate, dateSize, lcdClockDateTextColor(), LCD_BLACK, false, 0,
                                ClockDashboard::SCREEN_W);
        }
        g_clockDateCache = clockDateKey;
    }

    g_weatherTitleCache = "";
    g_weatherCurrentCache = "";

    for (int16_t index = 0; index < static_cast<int16_t>(scene.weatherForecastVisuals.size()); ++index) {
        const auto& visual = scene.weatherForecastVisuals[static_cast<size_t>(index)];
        String cacheKey;
        if (visual.hasData) {
            cacheKey = lcdString(visual.hourLabel + "|" + visual.temperatureLabel + "|" + visual.precipitationLabel + "|" +
                                 std::to_string(visual.weatherCode));
        }

        if (g_weatherForecastCache[static_cast<size_t>(index)] == cacheKey) {
            continue;
        }

        const int16_t cardX = static_cast<int16_t>(
            ClockDashboard::FORECAST_LEFT + (index * (ClockDashboard::FORECAST_WIDTH + ClockDashboard::FORECAST_GAP)));
        const int16_t cardY = ClockDashboard::FORECAST_TOP;
        const int16_t forecastIconY = static_cast<int16_t>(cardY + ClockDashboard::FORECAST_ICON_Y_OFFSET - 18);
        clockTarget->fillRect(cardX, cardY, ClockDashboard::FORECAST_WIDTH, ClockDashboard::FORECAST_HEIGHT, LCD_BLACK);
        g_forecastWeatherIconCache[static_cast<size_t>(index)] = -999;

        if (visual.hasData) {
            const uint8_t hourSize =
                lcdSelectTextSizeToFit(clockTarget, lcdString(visual.hourLabel), 2, 1, ClockDashboard::FORECAST_WIDTH - 8);
            lcdDrawCenteredText(clockTarget, cardY, lcdString(visual.hourLabel), hourSize, lcdClockForecastHourTextColor(),
                                LCD_BLACK, false, static_cast<int16_t>(cardX + 2), ClockDashboard::FORECAST_WIDTH - 4);
            clockTarget->fillRect(static_cast<int16_t>(cardX + ((ClockDashboard::FORECAST_WIDTH - ClockDashboard::FORECAST_ICON_SIZE) / 2)),
                                  forecastIconY, ClockDashboard::FORECAST_ICON_SIZE,
                                  ClockDashboard::FORECAST_ICON_SIZE, LCD_BLACK);
            lcdDrawCenteredText(clockTarget, static_cast<int16_t>(cardY + 52), lcdString(visual.temperatureLabel), 2,
                                lcdClockTemperatureTextColor(), LCD_BLACK, false, static_cast<int16_t>(cardX + 2),
                                ClockDashboard::FORECAST_WIDTH - 4);
            if (!visual.precipitationLabel.empty()) {
                const String precipitationText = lcdString(visual.precipitationLabel);
                lcdDrawCenteredText(clockTarget, static_cast<int16_t>(cardY + 72), lcdString(visual.precipitationLabel), 1,
                                    lcdForecastMetricColor(precipitationText), LCD_BLACK, false, static_cast<int16_t>(cardX + 2),
                                    ClockDashboard::FORECAST_WIDTH - 4);
            }
        }

        g_weatherForecastCache[static_cast<size_t>(index)] = cacheKey;
    }
    lcdDrawClockDashboardIcons(&g_lcd, scene, forceStaticRedraw, true);
    lcdFlushClockCanvases();
    g_lastClockStaticMinute = minuteTick;
}

static void lcdResetTextRenderer(Arduino_GFX* target) {
    target->setFont(static_cast<const GFXfont*>(nullptr));
    target->setTextSize(1);
    target->setTextColor(LCD_WHITE, LCD_BLACK);
}

static void lcdConfigureTextRenderer(Arduino_GFX* target, uint8_t textSize, uint16_t fgColor, uint16_t bgColor) {
    target->setFont(static_cast<const GFXfont*>(nullptr));
    target->setTextSize(textSize);
    target->setTextColor(fgColor, bgColor);
}

static auto lcdUtf8CharLength(uint8_t leadByte) -> size_t {
    if ((leadByte & 0x80U) == 0U) {
        return 1;
    }
    if ((leadByte & 0xE0U) == 0xC0U) {
        return 2;
    }
    if ((leadByte & 0xF0U) == 0xE0U) {
        return 3;
    }
    if ((leadByte & 0xF8U) == 0xF0U) {
        return 4;
    }

    return 1;
}

static auto lcdReadUtf8Char(const char* text, size_t textLength, size_t& index) -> String {
    const size_t charLength = std::min(lcdUtf8CharLength(static_cast<uint8_t>(text[index])), textLength - index);
    String value;
    value.reserve(charLength);
    for (size_t offset = 0; offset < charLength; ++offset) {
        value += text[index + offset];
    }
    index += charLength;

    return value;
}

static auto lcdUtf8CodepointFromGlyph(const String& glyph) -> uint32_t {
    const char* bytes = glyph.c_str();
    const size_t length = glyph.length();
    if (length == 0) {
        return 0;
    }

    const uint8_t first = static_cast<uint8_t>(bytes[0]);
    if ((first & 0x80U) == 0U) {
        return first;
    }
    if (length >= 2 && (first & 0xE0U) == 0xC0U) {
        return static_cast<uint32_t>(((first & 0x1FU) << 6) | (static_cast<uint8_t>(bytes[1]) & 0x3FU));
    }
    if (length >= 3 && (first & 0xF0U) == 0xE0U) {
        return static_cast<uint32_t>(((first & 0x0FU) << 12) | ((static_cast<uint8_t>(bytes[1]) & 0x3FU) << 6) |
                                     (static_cast<uint8_t>(bytes[2]) & 0x3FU));
    }
    if (length >= 4 && (first & 0xF8U) == 0xF0U) {
        return static_cast<uint32_t>(((first & 0x07U) << 18) | ((static_cast<uint8_t>(bytes[1]) & 0x3FU) << 12) |
                                     ((static_cast<uint8_t>(bytes[2]) & 0x3FU) << 6) |
                                     (static_cast<uint8_t>(bytes[3]) & 0x3FU));
    }

    return first;
}

static auto lcdUiTextKindForSize(uint8_t textSize) -> UiTextFont::Kind {
    return textSize >= 2 ? UiTextFont::Kind::Large : UiTextFont::Kind::Small;
}

static auto lcdCanUseVectorUiFont(const String& text, uint8_t textSize) -> bool {
    if (textSize == 0 || text.isEmpty()) {
        return false;
    }

    const UiTextFont::Kind kind = lcdUiTextKindForSize(textSize);
    const char* rawText = text.c_str();
    const size_t textLength = text.length();
    size_t index = 0;
    while (index < textLength) {
        const String glyph = lcdReadUtf8Char(rawText, textLength, index);
        if (glyph == "\r" || glyph == "\n" || glyph == "\t") {
            continue;
        }
        const uint32_t codepoint = lcdUtf8CodepointFromGlyph(glyph);
        if (UiTextFont::glyph(kind, codepoint) == nullptr) {
            return false;
        }
    }

    return true;
}

static auto lcdMeasureVectorUiTextWidth(const String& text, uint8_t textSize) -> int16_t {
    const UiTextFont::Kind kind = lcdUiTextKindForSize(textSize);
    const UiTextFont::FontSet& fontSet = UiTextFont::fontSet(kind);

    int16_t width = 0;
    bool firstGlyph = true;
    const char* rawText = text.c_str();
    const size_t textLength = text.length();
    size_t index = 0;
    while (index < textLength) {
        const String glyphText = lcdReadUtf8Char(rawText, textLength, index);
        if (glyphText == "\r" || glyphText == "\n") {
            break;
        }
        if (glyphText == "\t") {
            if (!firstGlyph) {
                width = static_cast<int16_t>(width + fontSet.tracking);
            }
            width = static_cast<int16_t>(width + (fontSet.lineHeight / 2));
            firstGlyph = false;
            continue;
        }

        const UiTextFont::Glyph* glyph = UiTextFont::glyph(kind, lcdUtf8CodepointFromGlyph(glyphText));
        if (glyph == nullptr) {
            continue;
        }
        if (!firstGlyph) {
            width = static_cast<int16_t>(width + fontSet.tracking);
        }
        width = static_cast<int16_t>(width + glyph->advance);
        firstGlyph = false;
    }

    return width;
}

static void lcdDrawVectorUiGlyph(Arduino_GFX* target, int16_t xPos, int16_t topY, uint32_t codepoint, uint8_t textSize,
                                 uint16_t fgColor, uint16_t bgColor) {
    const UiTextFont::Kind kind = lcdUiTextKindForSize(textSize);
    const UiTextFont::Glyph* glyph = UiTextFont::glyph(kind, codepoint);
    if (glyph == nullptr || glyph->width == 0 || glyph->height == 0) {
        return;
    }

    const UiTextFont::FontSet& fontSet = UiTextFont::fontSet(kind);
    const uint8_t maxAlpha = static_cast<uint8_t>((1U << fontSet.bitsPerPixel) - 1U);
    const int16_t drawY = static_cast<int16_t>(topY + glyph->yOffset);
    const size_t pixelCount = static_cast<size_t>(glyph->width) * static_cast<size_t>(glyph->height);
    for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        const uint8_t alpha = lcdReadPackedAlpha(glyph->bitmap, pixelIndex, fontSet.bitsPerPixel);
        if (alpha == 0U) {
            continue;
        }

        const int16_t pixelX = static_cast<int16_t>(xPos + (pixelIndex % glyph->width));
        const int16_t pixelY = static_cast<int16_t>(drawY + (pixelIndex / glyph->width));
        target->drawPixel(pixelX, pixelY, lcdBlendColor565(fgColor, bgColor, alpha, maxAlpha));
    }
}

static void lcdDrawVectorUiTextLine(Arduino_GFX* target, int16_t xPos, int16_t topY, const String& text, uint8_t textSize,
                                    uint16_t fgColor, uint16_t bgColor) {
    const UiTextFont::Kind kind = lcdUiTextKindForSize(textSize);
    const UiTextFont::FontSet& fontSet = UiTextFont::fontSet(kind);

    int16_t cursorX = xPos;
    bool firstGlyph = true;
    const char* rawText = text.c_str();
    const size_t textLength = text.length();
    size_t index = 0;
    while (index < textLength) {
        const String glyphText = lcdReadUtf8Char(rawText, textLength, index);
        if (glyphText == "\r" || glyphText == "\n") {
            break;
        }
        if (glyphText == "\t") {
            if (!firstGlyph) {
                cursorX = static_cast<int16_t>(cursorX + fontSet.tracking);
            }
            cursorX = static_cast<int16_t>(cursorX + (fontSet.lineHeight / 2));
            firstGlyph = false;
            continue;
        }

        const uint32_t codepoint = lcdUtf8CodepointFromGlyph(glyphText);
        const UiTextFont::Glyph* glyph = UiTextFont::glyph(kind, codepoint);
        if (glyph == nullptr) {
            continue;
        }

        if (!firstGlyph) {
            cursorX = static_cast<int16_t>(cursorX + fontSet.tracking);
        }
        lcdDrawVectorUiGlyph(target, cursorX, topY, codepoint, textSize, fgColor, bgColor);
        cursorX = static_cast<int16_t>(cursorX + glyph->advance);
        firstGlyph = false;
    }
}

static auto lcdTrimTextToWidth(Arduino_GFX* target, const String& text, uint8_t textSize, int16_t maxWidth) -> String {
    if (lcdMeasureTextWidthForSize(target, text, textSize) <= maxWidth) {
        return text;
    }

    const String ellipsis = "...";
    const int16_t ellipsisWidth = lcdMeasureTextWidthForSize(target, ellipsis, textSize);
    if (ellipsisWidth >= maxWidth) {
        return "";
    }

    const char* rawText = text.c_str();
    const size_t rawLength = text.length();
    size_t index = 0;
    String result;
    while (index < rawLength) {
        const String nextChar = lcdReadUtf8Char(rawText, rawLength, index);
        const String candidate = result + nextChar + ellipsis;
        if (lcdMeasureTextWidthForSize(target, candidate, textSize) > maxWidth) {
            break;
        }
        result += nextChar;
    }

    return result + ellipsis;
}

static auto lcdMeasureSevenSegmentTextWidth(const String& text, int16_t digitHeight) -> int16_t {
    if (text.isEmpty()) {
        return 0;
    }

    const int16_t gap = lcdSevenSegmentGap(digitHeight);
    int16_t cursorX = 0;

    for (uint32_t index = 0; index < text.length(); ++index) {
        const char value = text.charAt(index);
        cursorX = static_cast<int16_t>(cursorX + lcdSevenSegmentCellWidth(value, digitHeight));
        if (index + 1 < text.length()) {
            cursorX = static_cast<int16_t>(cursorX + gap);
        }
    }

    return std::min<int16_t>(cursorX, ClockDashboard::CLOCK_TIME_MAX_W);
}

static auto lcdMeasureTextWidth(Arduino_GFX* target, const String& text) -> int16_t {
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t width = 0;
    uint16_t height = 0;

    target->getTextBounds(text, 0, 0, &x1, &y1, &width, &height);

    return static_cast<int16_t>(width);
}

static auto lcdMeasureTextWidthForSize(Arduino_GFX* target, const String& text, uint8_t textSize) -> int16_t {
    if (lcdCanUseVectorUiFont(text, textSize)) {
        return lcdMeasureVectorUiTextWidth(text, textSize);
    }

    lcdConfigureTextRenderer(target, textSize, LCD_WHITE, LCD_BLACK);
    const int16_t width = lcdMeasureTextWidth(target, text);
    lcdResetTextRenderer(target);
    return width;
}

static auto lcdSelectTextSizeToFit(Arduino_GFX* target, const String& text, uint8_t maxSize, uint8_t minSize,
                                   int16_t maxWidth) -> uint8_t {
    for (int size = maxSize; size >= static_cast<int>(minSize); --size) {
        if (lcdMeasureTextWidthForSize(target, text, static_cast<uint8_t>(size)) <= maxWidth) {
            return static_cast<uint8_t>(size);
        }
    }
    return minSize;
}

static void lcdDrawTextAt(Arduino_GFX* target, int16_t xPos, int16_t topY, const String& text, uint8_t textSize,
                          uint16_t fgColor, uint16_t bgColor) {
    lcdDrawTextWrapped(target, xPos, topY, text, textSize, fgColor, bgColor, false);
}

static void lcdDrawSevenSegmentText(Arduino_GFX* target, int16_t xPos, int16_t topY, const String& text,
                                    int16_t digitHeight, uint16_t color) {
    const int16_t gap = lcdSevenSegmentGap(digitHeight);
    int16_t cursorX = xPos;

    for (uint32_t index = 0; index < text.length(); ++index) {
        const char value = text.charAt(index);
        const int16_t cellWidth = lcdSevenSegmentCellWidth(value, digitHeight);
        const int16_t glyphWidth = lcdVectorPixelWidth(value, digitHeight);
        const int16_t drawX = static_cast<int16_t>(cursorX + std::max<int16_t>((cellWidth - glyphWidth) / 2, 0));
        if (value == ':') {
            lcdDrawSevenSegmentColon(target, drawX, topY, digitHeight, color);
        } else {
            lcdDrawSevenSegmentDigit(target, drawX, topY, value, digitHeight, color);
        }

        cursorX = static_cast<int16_t>(cursorX + cellWidth);
        if (index + 1 < text.length()) {
            cursorX = static_cast<int16_t>(cursorX + gap);
        }
    }
}

static void lcdDrawClockDashboardIcons(Arduino_GFX* target, const ClockDashboard::Scene& scene, bool forceRedraw,
                                       bool useCache) {
    const WeatherClient::Snapshot* weather = weatherClient != nullptr ? &weatherClient->getSnapshot() : nullptr;
    const int16_t currentIconX = static_cast<int16_t>(
        ClockDashboard::SCREEN_W - ClockDashboard::HEADER_RIGHT_PADDING - ClockDashboard::CURRENT_ICON_SIZE);
    const bool showUmbrella = weather != nullptr && lcdShouldShowUmbrellaBadge(*weather);
    const bool redrawCurrent = !useCache || forceRedraw || g_currentWeatherIconCache != scene.weatherIconCode ||
                               g_currentUmbrellaBadgeCache != showUmbrella;
    if (scene.weatherIconCode >= 0 && redrawCurrent) {
        target->fillRect(currentIconX, CURRENT_WEATHER_ICON_Y, ClockDashboard::CURRENT_ICON_SIZE,
                         ClockDashboard::CURRENT_ICON_SIZE, LCD_BLACK);
        lcdDrawWeatherIcon(target, scene.weatherIconCode, currentIconX, CURRENT_WEATHER_ICON_Y,
                           ClockDashboard::CURRENT_ICON_SIZE, true);
        if (showUmbrella) {
            lcdDrawUmbrellaBadge(target,
                                 static_cast<int16_t>(currentIconX + ClockDashboard::CURRENT_ICON_SIZE -
                                                      CURRENT_WEATHER_BADGE_SIZE),
                                 static_cast<int16_t>(CURRENT_WEATHER_ICON_Y + ClockDashboard::CURRENT_ICON_SIZE -
                                                      CURRENT_WEATHER_BADGE_SIZE),
                                 CURRENT_WEATHER_BADGE_SIZE);
        }
        if (useCache) {
            g_currentWeatherIconCache = scene.weatherIconCode;
            g_currentUmbrellaBadgeCache = showUmbrella;
        }
    } else if (scene.weatherIconCode < 0 && (!useCache || g_currentWeatherIconCache != -999)) {
        target->fillRect(currentIconX, CURRENT_WEATHER_ICON_Y, ClockDashboard::CURRENT_ICON_SIZE,
                         ClockDashboard::CURRENT_ICON_SIZE, LCD_BLACK);
        if (useCache) {
            g_currentWeatherIconCache = -999;
            g_currentUmbrellaBadgeCache = false;
        }
    }

    for (int16_t index = 0; index < static_cast<int16_t>(scene.weatherForecastVisuals.size()); ++index) {
        const auto& visual = scene.weatherForecastVisuals[static_cast<size_t>(index)];
        const int16_t boxX = static_cast<int16_t>(
            ClockDashboard::FORECAST_LEFT + (index * (ClockDashboard::FORECAST_WIDTH + ClockDashboard::FORECAST_GAP)));
        const int16_t iconX =
            static_cast<int16_t>(boxX + ((ClockDashboard::FORECAST_WIDTH - ClockDashboard::FORECAST_ICON_SIZE) / 2));
        const int16_t iconY =
            static_cast<int16_t>(ClockDashboard::FORECAST_TOP + ClockDashboard::FORECAST_ICON_Y_OFFSET - 18);
        if (!visual.hasData || visual.weatherCode < 0) {
            if (!useCache || g_forecastWeatherIconCache[static_cast<size_t>(index)] != -999) {
                target->fillRect(iconX, iconY, ClockDashboard::FORECAST_ICON_SIZE, ClockDashboard::FORECAST_ICON_SIZE,
                                 LCD_BLACK);
            }
            if (useCache) {
                g_forecastWeatherIconCache[static_cast<size_t>(index)] = -999;
            }
            continue;
        }

        if (useCache && !forceRedraw && g_forecastWeatherIconCache[static_cast<size_t>(index)] == visual.weatherCode) {
            continue;
        }

        target->fillRect(iconX, iconY, ClockDashboard::FORECAST_ICON_SIZE, ClockDashboard::FORECAST_ICON_SIZE, LCD_BLACK);
        lcdDrawWeatherIcon(target, visual.weatherCode, iconX, iconY, ClockDashboard::FORECAST_ICON_SIZE, true);
        if (useCache) {
            g_forecastWeatherIconCache[static_cast<size_t>(index)] = visual.weatherCode;
        }
    }
}

static void lcdMeasureTextMetrics(Arduino_GFX* target, uint8_t textSize, int16_t& baselineOffset, int16_t& lineHeight) {
    const String sample = "Ag";
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t width = 0;
    uint16_t height = 0;

    target->getTextBounds(sample, 0, 0, &x1, &y1, &width, &height);

    baselineOffset = (y1 < 0) ? static_cast<int16_t>(-y1) : 0;
    lineHeight = static_cast<int16_t>(height + 2U);

    if (lineHeight <= 0) {
        lineHeight = static_cast<int16_t>(textSize * 8);
    }
}

static auto lcdPushWrappedLine(std::array<String, WRAP_MAX_LINE_SLOTS>& outLines, const String& line, int& lineCount,
                               int maxLines) -> bool {
    if (lineCount >= maxLines) {
        return false;
    }

    outLines[lineCount++] = line;

    return true;
}

static auto lcdAppendWordWrapped(std::array<String, WRAP_MAX_LINE_SLOTS>& outLines, int& lineCount, int maxLines,
                                 String& currentLine, const String& word, int16_t maxWidth, bool prependSpace,
                                 Arduino_GFX* target) -> bool {
    if (word.isEmpty()) {
        return true;
    }

    String candidate = currentLine;
    if (prependSpace && !candidate.isEmpty()) {
        candidate += ' ';
    }
    candidate += word;

    if (lcdMeasureTextWidth(target, candidate) <= maxWidth) {
        currentLine = candidate;

        return true;
    }

    if (!currentLine.isEmpty()) {
        if (!lcdPushWrappedLine(outLines, currentLine, lineCount, maxLines)) {
            return false;
        }

        currentLine = "";
        candidate = word;
        if (lcdMeasureTextWidth(target, candidate) <= maxWidth) {
            currentLine = candidate;

            return true;
        }
    }

    const char* wordText = word.c_str();
    const size_t wordLength = word.length();
    size_t index = 0;
    while (index < wordLength) {
        const String glyph = lcdReadUtf8Char(wordText, wordLength, index);
        const String glyphCandidate = currentLine + glyph;
        if (currentLine.isEmpty() || lcdMeasureTextWidth(target, glyphCandidate) <= maxWidth) {
            currentLine = glyphCandidate;
            continue;
        }

        if (!lcdPushWrappedLine(outLines, currentLine, lineCount, maxLines)) {
            return false;
        }

        currentLine = glyph;
    }

    return true;
}

// Screen cmd
static constexpr uint8_t ST7789_SLEEP_DELAY_MS = 120;
static constexpr uint8_t ST7789_SLEEP_OUT = 0x11;
static constexpr uint8_t ST7789_PORCH = 0xB2;
static constexpr uint8_t ST7789_PORCH_SETTINGS = 0x1F;
static constexpr uint8_t ST7789_SW_RESET = 0x01;

static constexpr uint8_t ST7789_TEARING_EFFECT = 0x35;
static constexpr uint8_t ST7789_MEMORY_ACCESS_CONTROL = 0x36;
static constexpr uint8_t ST7789_COLORMODE = 0x3A;
static constexpr uint8_t ST7789_COLORMODE_RGB565 = 0x05;

static constexpr uint8_t ST7789_POWER_B7 = 0xB7;
static constexpr uint8_t ST7789_POWER_BB = 0xBB;
static constexpr uint8_t ST7789_POWER_C0 = 0xC0;
static constexpr uint8_t ST7789_POWER_C2 = 0xC2;
static constexpr uint8_t ST7789_POWER_C3 = 0xC3;
static constexpr uint8_t ST7789_POWER_C4 = 0xC4;
static constexpr uint8_t ST7789_POWER_C6 = 0xC6;
static constexpr uint8_t ST7789_POWER_D0 = 0xD0;
static constexpr uint8_t ST7789_POWER_D6 = 0xD6;

static constexpr uint8_t ST7789_GAMMA_POS = 0xE0;
static constexpr uint8_t ST7789_GAMMA_NEG = 0xE1;
static constexpr uint8_t ST7789_GAMMA_CTRL = 0xE4;

static constexpr uint8_t ST7789_INVERSION_ON = 0x21;
static constexpr uint8_t ST7789_DISPLAY_ON = 0x29;
static constexpr uint8_t ST7789_NORMAL_DISPLAY_MODE = 0x13;

// Porch parameters used in sequence
static constexpr uint8_t ST7789_PORCH_PARAM_HS = 0x1F;
static constexpr uint8_t ST7789_PORCH_PARAM_VS = 0x1F;
static constexpr uint8_t ST7789_PORCH_PARAM_DUMMY = 0x00;
static constexpr uint8_t ST7789_PORCH_PARAM_HBP = 0x33;
static constexpr uint8_t ST7789_PORCH_PARAM_VBP = 0x33;

// Simple params for commands
static constexpr uint8_t ST7789_TEARING_PARAM_OFF = 0x00;
static constexpr uint8_t ST7789_MADCTL_PARAM_DEFAULT = 0x00;
static constexpr uint8_t ST7789_B7_PARAM_DEFAULT = 0x00;
static constexpr uint8_t ST7789_BB_PARAM_VOLTAGE = 0x36;
static constexpr uint8_t ST7789_C0_PARAM_1 = 0x2C;
static constexpr uint8_t ST7789_C2_PARAM_1 = 0x01;
static constexpr uint8_t ST7789_C3_PARAM_1 = 0x13;
static constexpr uint8_t ST7789_C4_PARAM_1 = 0x20;
static constexpr uint8_t ST7789_C6_PARAM_1 = 0x13;
static constexpr uint8_t ST7789_D6_PARAM_1 = 0xA1;
static constexpr uint8_t ST7789_D0_PARAM_1 = 0xA4;
static constexpr uint8_t ST7789_D0_PARAM_2 = 0xA1;

// Gamma parameter blocks
static constexpr std::array<uint8_t, 14> ST7789_GAMMA_POS_DATA = {0xF0, 0x08, 0x0E, 0x09, 0x08, 0x04, 0x2F,
                                                                  0x33, 0x45, 0x36, 0x13, 0x12, 0x2A, 0x2D};
static constexpr std::array<uint8_t, 14> ST7789_GAMMA_NEG_DATA = {0xF0, 0x0E, 0x12, 0x0C, 0x0A, 0x15, 0x2E,
                                                                  0x32, 0x44, 0x39, 0x17, 0x18, 0x2B, 0x2F};
static constexpr std::array<uint8_t, 3> ST7789_GAMMA_CTRL_DATA = {0x1D, 0x00, 0x00};

// Column/row address parameters
static constexpr uint8_t ST7789_ADDR_START_HIGH = 0x00;
static constexpr uint8_t ST7789_ADDR_START_LOW = 0x00;
static constexpr uint8_t ST7789_ADDR_END_HIGH = 0x00;
static constexpr uint8_t ST7789_ADDR_END_LOW = 0xEF;

/**
 * @brief Get the Arduino_GFX instance used for the LCD
 *
 * @return Pointer to the Arduino_GFX instance
 */
auto DisplayManager::getGfx() -> Arduino_GFX* { return &g_lcd; }

void DisplayManager::setUpdateAvailable(bool available) {
    if (g_updateAvailable == available) {
        return;
    }

    g_updateAvailable = available;
    g_updateAvailableCache = !available;
}

/**
 * @brief Turn the LCD backlight on
 *
 * @return void
 */
static inline void lcdApplyBacklightBrightness(uint8_t brightnessPercent) {
    if (brightnessPercent < 5U) {
        brightnessPercent = 5U;
    }
    if (brightnessPercent > 100U) {
        brightnessPercent = 100U;
    }

    static constexpr uint16_t PWM_RANGE = 1023;
    static constexpr uint16_t PWM_FREQ_HZ = 1000;
    const uint16_t activeDuty = static_cast<uint16_t>((static_cast<uint32_t>(brightnessPercent) * PWM_RANGE) / 100U);
    const uint16_t duty = LCD_BACKLIGHT_ACTIVE_LOW ? static_cast<uint16_t>(PWM_RANGE - activeDuty) : activeDuty;

    pinMode((uint8_t)LCD_BACKLIGHT_GPIO, OUTPUT);
    analogWriteRange(PWM_RANGE);
    analogWriteFreq(PWM_FREQ_HZ);
    analogWrite((uint8_t)LCD_BACKLIGHT_GPIO, duty);
}

void DisplayManager::setBrightness(uint8_t brightnessPercent) { lcdApplyBacklightBrightness(brightnessPercent); }

void DisplayManager::applyConfiguredBrightness() {
    const uint8_t brightness = lcdConfiguredBrightness();
    if (brightness == g_lastAppliedBrightness) {
        return;
    }

    lcdApplyBacklightBrightness(brightness);
    g_lastAppliedBrightness = brightness;
}

/**
 * @brief Write a single command byte to the ST7789 via the data bus
 *
 * @return void
 */
static inline void ST7789_WriteCommand(uint8_t cmd) { g_lcdBus.writeCommand(cmd); }

/**
 * @brief Write a single data byte to the ST7789 via the data bus
 *
 * @return void
 */
static inline void ST7789_WriteData(uint8_t data) { g_lcdBus.write(data); }

/**
 * @brief Run a vendor-specific initialization sequence for the ST7789 panel
 *
 *  - Sleep out (0x11)
 *
 *  - Porch settings (0xB2)
 *
 *  - Tearing effect on (0x35)
 *
 *  - Memory access control/MADCTL (0x36)
 *
 *  - Color mode to 16-bit RGB565 (0x3A)
 *
 *  - Various power control settings (0xB7, 0xBB, 0xC0-0xC6, 0xD0, 0xD6)
 *
 *  - Gamma correction settings (0xE0, 0xE1, 0xE4)
 *
 *  - Display inversion on (0x21)
 *
 *  - Display on (0x29)
 *
 *  - Full window setup and RAMWR command (0x2A, 0x2B, 0x2C)
 *
 * @return void
 */
static void lcdRunVendorInit() {
    g_lcdBus.beginWrite();

    ST7789_WriteCommand(ST7789_SLEEP_OUT);
    delay(ST7789_SLEEP_DELAY_MS);

    ST7789_WriteCommand(ST7789_PORCH);
    ST7789_WriteData(ST7789_PORCH_PARAM_HS);
    ST7789_WriteData(ST7789_PORCH_PARAM_VS);
    ST7789_WriteData(ST7789_PORCH_PARAM_DUMMY);
    ST7789_WriteData(ST7789_PORCH_PARAM_HBP);
    ST7789_WriteData(ST7789_PORCH_PARAM_VBP);

    ST7789_WriteCommand(ST7789_TEARING_EFFECT);
    ST7789_WriteData(ST7789_TEARING_PARAM_OFF);

    ST7789_WriteCommand(ST7789_MEMORY_ACCESS_CONTROL);
    ST7789_WriteData(ST7789_MADCTL_PARAM_DEFAULT);

    ST7789_WriteCommand(ST7789_COLORMODE);
    ST7789_WriteData(ST7789_COLORMODE_RGB565);

    ST7789_WriteCommand(ST7789_POWER_B7);
    ST7789_WriteData(ST7789_B7_PARAM_DEFAULT);

    ST7789_WriteCommand(ST7789_POWER_BB);
    ST7789_WriteData(ST7789_BB_PARAM_VOLTAGE);

    ST7789_WriteCommand(ST7789_POWER_C0);
    ST7789_WriteData(ST7789_C0_PARAM_1);

    ST7789_WriteCommand(ST7789_POWER_C2);
    ST7789_WriteData(ST7789_C2_PARAM_1);

    ST7789_WriteCommand(ST7789_POWER_C3);
    ST7789_WriteData(ST7789_C3_PARAM_1);

    ST7789_WriteCommand(ST7789_POWER_C4);
    ST7789_WriteData(ST7789_C4_PARAM_1);

    ST7789_WriteCommand(ST7789_POWER_C6);
    ST7789_WriteData(ST7789_C6_PARAM_1);

    ST7789_WriteCommand(ST7789_POWER_D6);
    ST7789_WriteData(ST7789_D6_PARAM_1);

    ST7789_WriteCommand(ST7789_POWER_D0);
    ST7789_WriteData(ST7789_D0_PARAM_1);
    ST7789_WriteData(ST7789_D0_PARAM_2);

    ST7789_WriteCommand(ST7789_POWER_D6);
    ST7789_WriteData(ST7789_D6_PARAM_1);

    ST7789_WriteCommand(ST7789_GAMMA_POS);
    for (uint8_t v : ST7789_GAMMA_POS_DATA) {
        ST7789_WriteData(v);
    }

    ST7789_WriteCommand(ST7789_GAMMA_NEG);
    for (uint8_t v : ST7789_GAMMA_NEG_DATA) {
        ST7789_WriteData(v);
    }

    ST7789_WriteCommand(ST7789_GAMMA_CTRL);
    for (uint8_t v : ST7789_GAMMA_CTRL_DATA) {
        ST7789_WriteData(v);
    }

    ST7789_WriteCommand(ST7789_INVERSION_ON);

    ST7789_WriteCommand(ST7789_DISPLAY_ON);

    ST7789_WriteCommand(ST7789_CASET);
    ST7789_WriteData(ST7789_ADDR_START_HIGH);
    ST7789_WriteData(ST7789_ADDR_START_LOW);
    ST7789_WriteData(ST7789_ADDR_END_HIGH);
    ST7789_WriteData(ST7789_ADDR_END_LOW);

    ST7789_WriteCommand(ST7789_RASET);
    ST7789_WriteData(ST7789_ADDR_START_HIGH);
    ST7789_WriteData(ST7789_ADDR_START_LOW);
    ST7789_WriteData(ST7789_ADDR_END_HIGH);
    ST7789_WriteData(ST7789_ADDR_END_LOW);

    ST7789_WriteCommand(ST7789_RAMWR);

    g_lcdBus.endWrite();
}

/**
 * @brief Perform a hardware reset of the LCD panel
 *
 * Toggles the RST GPIO if defined, with appropriate delays
 *
 * @return void
 */
static void lcdHardReset() {
    pinMode((uint8_t)LCD_RST_GPIO, OUTPUT);
    digitalWrite((uint8_t)LCD_RST_GPIO, HIGH);
    delay(LCD_HARDWARE_RESET_DELAY_MS);
    digitalWrite((uint8_t)LCD_RST_GPIO, LOW);
    delay(LCD_HARDWARE_RESET_DELAY_MS);
    digitalWrite((uint8_t)LCD_RST_GPIO, HIGH);
    delay(LCD_HARDWARE_RESET_DELAY_MS);
}

/**
 * @brief Ensure the LCD is initialized and ready for drawing
 *
 * @return void
 */
static void lcdEnsureInit() {
    Logger::info("Initialization started", "DisplayManager");

    DisplayManager::applyConfiguredBrightness();

    uint8_t rotation = configManager.getLCDRotationSafe();

    // SPI mode 3 is required. This toggles the pin from LOW to HIGH after reset, which my guess
    // is after reset "initializes" the SPI interface of the display, as CS is tied to GND?
    // ...strange that SPI_MODE0 will not work as the IC doesn't care about CLK's polarity
    g_lcdBus.begin((int32_t)LCD_SPI_HZ, (int8_t)LCD_SPI_MODE);
    lcdHardReset();
    lcdRunVendorInit();
    delay(LCD_BEGIN_DELAY_MS);

    g_lcd.setRotation(rotation);

    Logger::info(
        ("Width=" + String(g_lcd.width()) + " height=" + String(g_lcd.height()) + " rotation=" + String(rotation))
            .c_str(),
        "DisplayManager");

    g_lcd.fillScreen(LCD_BLACK);
    g_lcd.setTextColor(LCD_WHITE, LCD_BLACK);

    Logger::info("Initialization completed", "DisplayManager");
}

/**
 * @brief Draw text on the display with simple word-wrapping
 *
 * @param startX Starting X coordinate in pixels
 * @param startY Starting Y coordinate in pixels
 * @param text The text to draw (can contain newlines)
 * @param textSize Font size multiplier (integer)
 * @param fgColor Foreground color (16-bit RGB565)
 * @param bgColor Background color (16-bit RGB565)
 * @param clearBg If true, clears the background rectangle before drawing
 *
 * @return void
 */
static void lcdDrawTextWrapped(Arduino_GFX* target, int16_t startX, int16_t startY, const String& text,
                               uint8_t textSize, uint16_t fgColor, uint16_t bgColor, bool clearBg) {
    const auto screenW = static_cast<int16_t>(target->width());
    const auto screenH = static_cast<int16_t>(target->height());
    const bool useVectorUiFont = lcdCanUseVectorUiFont(text, textSize);

    if (startX < 0) {
        startX = 0;
    }

    if (startY < 0) {
        startY = 0;
    }

    if (startX >= screenW || startY >= screenH) {
        Logger::warn("Text start position out of bounds", "DisplayManager");

        return;
    }

    if (useVectorUiFont) {
        const UiTextFont::Kind fontKind = lcdUiTextKindForSize(textSize);
        const UiTextFont::FontSet& fontSet = UiTextFont::fontSet(fontKind);
        const int16_t lineHeight = static_cast<int16_t>(fontSet.lineHeight + 2);
        const int16_t maxWidth = static_cast<int16_t>(screenW - startX);
        int maxLines = (screenH - startY) / lineHeight;
        if (maxWidth <= 0 || maxLines <= 0) {
            Logger::warn("No space for text", "DisplayManager");
            return;
        }

        if (maxLines > WRAP_MAX_LINE_SLOTS) {
            maxLines = WRAP_MAX_LINE_SLOTS;
        }

        auto measureVectorLine = [textSize](const String& value) -> int16_t {
            return lcdMeasureVectorUiTextWidth(value, textSize);
        };

        auto appendVectorWord = [&](std::array<String, WRAP_MAX_LINE_SLOTS>& outLines, int& lineCount, String& currentLine,
                                    const String& word, bool prependSpace) -> bool {
            if (word.isEmpty()) {
                return true;
            }

            String candidate = currentLine;
            if (prependSpace && !candidate.isEmpty()) {
                candidate += ' ';
            }
            candidate += word;

            if (measureVectorLine(candidate) <= maxWidth) {
                currentLine = candidate;
                return true;
            }

            if (!currentLine.isEmpty()) {
                if (!lcdPushWrappedLine(outLines, currentLine, lineCount, maxLines)) {
                    return false;
                }

                currentLine = "";
                candidate = word;
                if (measureVectorLine(candidate) <= maxWidth) {
                    currentLine = candidate;
                    return true;
                }
            }

            const char* wordText = word.c_str();
            const size_t wordLength = word.length();
            size_t wordIndex = 0;
            while (wordIndex < wordLength) {
                const String glyph = lcdReadUtf8Char(wordText, wordLength, wordIndex);
                const String glyphCandidate = currentLine + glyph;
                if (currentLine.isEmpty() || measureVectorLine(glyphCandidate) <= maxWidth) {
                    currentLine = glyphCandidate;
                    continue;
                }

                if (!lcdPushWrappedLine(outLines, currentLine, lineCount, maxLines)) {
                    return false;
                }

                currentLine = glyph;
            }

            return true;
        };

        std::array<String, WRAP_MAX_LINE_SLOTS> lines{};
        int lineCount = 0;
        String currentLine;
        String currentWord;
        bool pendingSpace = false;
        const char* rawText = text.c_str();
        const size_t textLength = text.length();

        for (size_t index = 0; index < textLength;) {
            const String glyph = lcdReadUtf8Char(rawText, textLength, index);

            if (glyph == "\r") {
                continue;
            }

            if (glyph == "\n") {
                if (!appendVectorWord(lines, lineCount, currentLine, currentWord, pendingSpace)) {
                    break;
                }

                currentWord = "";
                pendingSpace = false;

                if (!lcdPushWrappedLine(lines, currentLine, lineCount, maxLines)) {
                    break;
                }

                currentLine = "";
                continue;
            }

            if (glyph == " " || glyph == "\t") {
                if (!appendVectorWord(lines, lineCount, currentLine, currentWord, pendingSpace)) {
                    break;
                }

                currentWord = "";
                pendingSpace = !currentLine.isEmpty();
                continue;
            }

            currentWord += glyph;
        }

        if (lineCount < maxLines) {
            appendVectorWord(lines, lineCount, currentLine, currentWord, pendingSpace);
            if (!currentLine.isEmpty() || lineCount == 0) {
                lcdPushWrappedLine(lines, currentLine, lineCount, maxLines);
            }
        }

        if (clearBg) {
            const auto heightPixels = static_cast<int16_t>(static_cast<int>(lineCount) * static_cast<int>(lineHeight));
            target->fillRect(startX, startY, static_cast<int16_t>(screenW - startX),
                             static_cast<int16_t>(std::max<int16_t>(heightPixels, lineHeight)), bgColor);
        }

        for (int li = 0; li < lineCount; ++li) {
            const int16_t topY = static_cast<int16_t>(startY + static_cast<int16_t>(li * lineHeight));
            lcdDrawVectorUiTextLine(target, startX, topY, lines[li], textSize, fgColor, bgColor);
        }
        return;
    }

    lcdConfigureTextRenderer(target, textSize, fgColor, bgColor);

    int16_t baselineOffset = 0;
    int16_t lineHeight = 0;
    lcdMeasureTextMetrics(target, textSize, baselineOffset, lineHeight);
    if (lineHeight <= 0) {
        lcdResetTextRenderer(target);
        Logger::warn("Invalid character dimensions", "DisplayManager");

        return;
    }

    const int16_t maxWidth = static_cast<int16_t>(screenW - startX);
    int maxLines = (screenH - startY) / lineHeight;
    if (maxWidth <= 0 || maxLines <= 0) {
        lcdResetTextRenderer(target);
        Logger::warn("No space for text", "DisplayManager");

        return;
    }

    if (maxLines > WRAP_MAX_LINE_SLOTS) {
        maxLines = WRAP_MAX_LINE_SLOTS;
    }

    std::array<String, WRAP_MAX_LINE_SLOTS> lines{};
    int lineCount = 0;
    String currentLine;
    String currentWord;
    bool pendingSpace = false;
    const char* rawText = text.c_str();
    const size_t textLength = text.length();

    for (size_t index = 0; index < textLength; ) {
        const String glyph = lcdReadUtf8Char(rawText, textLength, index);

        if (glyph == "\r") {
            continue;
        }

        if (glyph == "\n") {
            if (!lcdAppendWordWrapped(lines, lineCount, maxLines, currentLine, currentWord, maxWidth, pendingSpace,
                                      target)) {
                break;
            }

            currentWord = "";
            pendingSpace = false;

            if (!lcdPushWrappedLine(lines, currentLine, lineCount, maxLines)) {
                break;
            }

            currentLine = "";
            continue;
        }

        if (glyph == " " || glyph == "\t") {
            if (!lcdAppendWordWrapped(lines, lineCount, maxLines, currentLine, currentWord, maxWidth, pendingSpace,
                                      target)) {
                break;
            }

            currentWord = "";
            pendingSpace = !currentLine.isEmpty();
            continue;
        }

        currentWord += glyph;
    }

    if (lineCount < maxLines) {
        lcdAppendWordWrapped(lines, lineCount, maxLines, currentLine, currentWord, maxWidth, pendingSpace, target);
        if (!currentLine.isEmpty() || lineCount == 0) {
            lcdPushWrappedLine(lines, currentLine, lineCount, maxLines);
        }
    }

    if (clearBg) {
        const auto heightPixels = static_cast<int16_t>(static_cast<int>(lineCount) * static_cast<int>(lineHeight));
        target->fillRect(startX, startY, static_cast<int16_t>(screenW - startX), static_cast<int16_t>(heightPixels),
                         bgColor);
    }

    for (int li = 0; li < lineCount; ++li) {
        const int16_t baselineY =
            static_cast<int16_t>(startY + baselineOffset + static_cast<int16_t>(li * lineHeight));
        target->setCursor(startX, baselineY);
        target->print(lines[li]);
    }

    lcdResetTextRenderer(target);
}

/**
 * @brief Initialize the DisplayManager and LCD
 *
 * Ensures the LCD is initialized and ready for drawing
 *
 * @return void
 */
auto DisplayManager::begin() -> void {
    lcdEnsureInit();
    lcdEnsureClockTimeCanvas();
}

/**
 * @brief Apply a new display rotation at runtime
 *
 * @param rotation Rotation value in range [0, 7]
 *
 * @return void
 */
auto DisplayManager::setRotation(uint8_t rotation, String currentIP) -> void {
    g_lcd.setRotation(rotation);
    g_lastClockDrawnSecond = 0;
    g_lastClockStaticMinute = -1;
    lcdResetClockLayoutCache();
    DisplayManager::drawStartup(currentIP);

    Logger::info(("Rotation set to " + String(rotation)).c_str(), "DisplayManager");
}

/**
 * @brief Draw the startup screen on the LCD
 *
 * @return void
 */
auto DisplayManager::drawStartup(String currentIP) -> void {
    int constexpr rgbDelayMs = 1000;

    g_lcd.fillScreen(LCD_RED);
    delay(rgbDelayMs);
    g_lcd.fillScreen(LCD_GREEN);
    delay(rgbDelayMs);
    g_lcd.fillScreen(LCD_BLUE);
    delay(rgbDelayMs);

    g_lcd.fillScreen(LCD_BLACK);

    int constexpr titleY = 10;
    int constexpr fontSize = 2;

    DisplayManager::drawTextWrapped(DISPLAY_PADDING, titleY, F("SmallTV-Ultra Korean Custom Firmware"), fontSize, LCD_WHITE,
                                    LCD_BLACK, false);
    DisplayManager::drawTextWrapped(DISPLAY_PADDING, titleY + THREE_LINES_SPACE, String(PROJECT_VER_STR), fontSize,
                                    LCD_WHITE, LCD_BLACK, false);
    DisplayManager::drawTextWrapped(DISPLAY_PADDING, (titleY + THREE_LINES_SPACE + TWO_LINES_SPACE),
                                    String(F("IP: ")) + currentIP, fontSize, LCD_WHITE, LCD_BLACK, false);

    const int16_t box = 40;
    const int16_t gap = 20;
    const int16_t boxY = titleY + (THREE_LINES_SPACE * 2) + ONE_LINE_SPACE;

    g_lcd.fillRect(DISPLAY_PADDING, boxY, box, box, LCD_RED);
    g_lcd.fillRect((int16_t)(DISPLAY_PADDING + box + gap), boxY, box, box, LCD_GREEN);
    g_lcd.fillRect((int16_t)(DISPLAY_PADDING + (box + gap) * 2), boxY, box, box, LCD_BLUE);

    DisplayManager::pauseClock(STARTUP_CLOCK_PAUSE_MS);

    yield();

    Logger::info("Startup screen drawn", "DisplayManager");
}

auto DisplayManager::drawClock() -> void {
    if (!configManager.isClockEnabled() && !configManager.isWeatherEnabled()) {
        return;
    }

    lcdDrawClockInner();
}

auto DisplayManager::invalidateWeather() -> void {
    lcdResetWeatherCache();
    g_lastClockDrawnSecond = 0;
    g_lastClockStaticMinute = -1;
}

auto DisplayManager::pauseClock(uint32_t durationMs) -> void {
    const unsigned long nowMs = millis();
    g_clockPausedUntilMs = nowMs + durationMs;
    g_lastClockDrawnSecond = 0;
    g_lastClockStaticMinute = -1;
    lcdResetClockLayoutCache();
}

/**
 * @brief Draw text on the display with simple word-wrapping
 *
 * @param x Starting X coordinate in pixels
 * @param y Starting Y coordinate in pixels
 * @param text The text to draw (can contain newlines)
 * @param textSize Font size multiplier (integer)
 * @param fg Foreground color (16-bit RGB565)
 * @param bg Background color (16-bit RGB565)
 * @param clearBg If true, clears the background rectangle before drawing
 *
 * @return void
 */
void DisplayManager::drawTextWrapped(int16_t xPos, int16_t yPos, const String& text, uint8_t textSize, uint16_t fgColor,
                                     uint16_t bgColor, bool clearBg) {
    lcdDrawTextWrapped(&g_lcd, xPos, yPos, text, textSize, fgColor, bgColor, clearBg);
}

/**
 * @brief Draw a loading bar on the display
 *
 * @param progress Progress value between 0.0 (empty) and 1.0 (full)
 * @param yPos Y coordinate of the top of the loading bar
 * @param barWidth Width of the loading bar in pixels
 * @param barHeight Height of the loading bar in pixels
 * @param fgColor Foreground color (16-bit RGB565)
 * @param bgColor Background color (16-bit RGB565)
 */
void DisplayManager::drawLoadingBar(float progress, int yPos, int barWidth, int barHeight, uint16_t fgColor,
                                    uint16_t bgColor) {
    auto barXPos = (static_cast<int32_t>(LCD_W) - static_cast<int32_t>(barWidth)) / 2;
    auto barXPos16 = static_cast<int16_t>(barXPos);
    auto yPos16 = static_cast<int16_t>(yPos);
    auto barWidth16 = static_cast<int16_t>(barWidth);
    auto barHeight16 = static_cast<int16_t>(barHeight);

    g_lcd.fillRect(barXPos16, yPos16, barWidth16, barHeight16, bgColor);

    auto fillWidthF = static_cast<float>(barWidth) * progress;
    auto fillWidth16 = static_cast<int16_t>(fillWidthF);
    if (fillWidth16 > 0) {
        g_lcd.fillRect(barXPos16, yPos16, fillWidth16, barHeight16, fgColor);
    }

    yield();
}

/**
 * @brief Play a single GIF file in full screen mode (blocking)
 *
 * @param path Path to the GIF file on LittleFS
 * @param timeMs Duration to play the GIF in milliseconds (0 = play full GIF)
 * @return true if played successfully, false on error
 */
auto DisplayManager::playGifFullScreen(const String& path, uint32_t timeMs) -> bool {
    if (g_gif == nullptr) {
        g_gif = new Gif();
        if (g_gif == nullptr) {
            Logger::error("Failed to allocate GIF decoder", "DisplayManager");
            return false;
        }
    }

    g_gif->stop();
    DisplayManager::pauseClock(TRANSIENT_CLOCK_PAUSE_MS);

    if (!g_gif->begin()) {
        return false;
    }

    DisplayManager::clearScreen();

    g_gif->setLoopEnabled(timeMs == 0);

    const bool started = g_gif->playOne(path);
    if (!started) {
        return false;
    }

    if (timeMs == 0) {
        return true;
    }

    const uint32_t startMs = millis();
    const uint32_t endMs = startMs + timeMs;

    while (g_gif->isPlaying() && static_cast<int32_t>(millis() - endMs) < 0) {
        g_gif->update();
        yield();
    }

    if (g_gif->isPlaying()) {
        g_gif->stop();
    }

    return true;
}

/**
 * @brief Stop GIF playback if playing
 *
 * @return true
 */
auto DisplayManager::stopGif() -> bool {
    if (g_gif != nullptr) {
        g_gif->stop();
    }

    DisplayManager::clearScreen();
    DisplayManager::pauseClock(1000);

    return true;
}
/**
 * @brief Update the GIF decoder (should be called regularly in loop)
 *
 * @return void
 */
auto DisplayManager::update() -> void {
    const unsigned long nowMs = millis();
    if (static_cast<long>(nowMs - g_nextBrightnessScheduleCheckMs) >= 0) {
        DisplayManager::applyConfiguredBrightness();
        g_nextBrightnessScheduleCheckMs = millis() + lcdBrightnessScheduleDelayMs();
    }

    if (g_gif != nullptr) {
        g_gif->update();
        if (g_gif->isPlaying()) {
            return;
        }
    }

    if (!configManager.isClockEnabled() && !configManager.isWeatherEnabled()) {
        return;
    }

    if (static_cast<long>(nowMs - g_clockPausedUntilMs) < 0) {
        return;
    }

    lcdDrawClockInner();
}

/**
 * @brief Clear the entire display to black
 *
 * @return void
 */
auto DisplayManager::clearScreen() -> void {
    g_lcd.fillScreen(LCD_BLACK);
    lcdResetClockLayoutCache();
}
