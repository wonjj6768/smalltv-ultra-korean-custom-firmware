#pragma once

#include <cstdint>

#include "Arduino.h"
#include "SPI.h"

static constexpr int16_t LCD_W = 240;
static constexpr int16_t LCD_H = 240;
static constexpr uint8_t LCD_ROTATION = 0;
static constexpr int8_t LCD_MOSI_GPIO = 13;
static constexpr int8_t LCD_SCK_GPIO = 14;
static constexpr int8_t LCD_DC_GPIO = 0;
static constexpr int8_t LCD_RST_GPIO = 2;
static constexpr uint8_t LCD_SPI_MODE = SPI_MODE3;
static constexpr uint32_t LCD_SPI_HZ = 40000000;
static constexpr int8_t LCD_BACKLIGHT_GPIO = 5;
static constexpr bool LCD_BACKLIGHT_ACTIVE_LOW = true;

class ConfigManager {
   public:
    auto load() -> bool { return true; }
    auto save() -> bool { return true; }
    auto getLCDRotation() const -> uint8_t { return lcd_rotation; }
    auto getLCDRotationSafe() const -> uint8_t { return lcd_rotation; }
    auto setLCDRotation(uint8_t rotation) -> void { lcd_rotation = rotation; }
    auto getDisplayBrightness() const -> uint8_t { return display_brightness; }
    auto setDisplayBrightness(uint8_t brightness) -> void { display_brightness = brightness; }
    auto isDisplayNightBrightnessEnabled() const -> bool { return display_night_brightness_enabled; }
    auto setDisplayNightBrightnessEnabled(bool enabled) -> void { display_night_brightness_enabled = enabled; }
    auto getDisplayNightBrightness() const -> uint8_t { return display_night_brightness; }
    auto setDisplayNightBrightness(uint8_t brightness) -> void { display_night_brightness = brightness; }
    auto getDisplayNightStartMinute() const -> uint16_t { return display_night_start_minute; }
    auto setDisplayNightStartMinute(uint16_t minute) -> void { display_night_start_minute = minute; }
    auto getDisplayNightEndMinute() const -> uint16_t { return display_night_end_minute; }
    auto setDisplayNightEndMinute(uint16_t minute) -> void { display_night_end_minute = minute; }
    auto isClockEnabled() const -> bool { return clock_enabled; }
    auto setClockEnabled(bool enabled) -> void { clock_enabled = enabled; }
    auto isClockUse24Hour() const -> bool { return clock_use_24h; }
    auto setClockUse24Hour(bool enabled) -> void { clock_use_24h = enabled; }
    auto isWeatherEnabled() const -> bool { return weather_enabled; }
    auto setWeatherEnabled(bool enabled) -> void { weather_enabled = enabled; }
    auto getWeatherLatitude() const -> float { return weather_latitude; }
    auto getWeatherLongitude() const -> float { return weather_longitude; }
    auto getWeatherKmaGridX() const -> int { return weather_kma_grid_x; }
    auto getWeatherKmaGridY() const -> int { return weather_kma_grid_y; }
    auto getWeatherLocationName() const -> const char* { return weather_location_name.c_str(); }
    auto getWeatherTimezone() const -> const char* { return weather_timezone.c_str(); }
    auto setWeatherTimezone(const char* value) -> void { weather_timezone = value == nullptr ? "" : value; }
    auto getWeatherKmaApiKey() const -> const char* { return weather_kma_api_key.c_str(); }
    auto setWeatherKmaApiKey(const char* value) -> void { weather_kma_api_key = value == nullptr ? "" : value; }
    auto getTimezoneRegion() const -> const char* { return timezone_region.c_str(); }
    auto setTimezoneRegion(const char* value) -> void { timezone_region = value == nullptr ? "" : value; }

    uint8_t lcd_rotation = 0;
    uint8_t display_brightness = 100;
    bool display_night_brightness_enabled = false;
    uint8_t display_night_brightness = 20;
    uint16_t display_night_start_minute = 22 * 60;
    uint16_t display_night_end_minute = 7 * 60;
    bool clock_enabled = true;
    bool clock_use_24h = true;
    bool weather_enabled = true;
    float weather_latitude = 37.566F;
    float weather_longitude = 126.9784F;
    int weather_kma_grid_x = 60;
    int weather_kma_grid_y = 127;
    String weather_location_name = "서울시";
    String weather_timezone = "Asia/Seoul";
    String weather_kma_api_key;
    String timezone_region = "Asia/Seoul";
};
