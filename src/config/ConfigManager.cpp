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

#include <ArduinoJson.h>
#include <LittleFS.h>

#include <algorithm>
#include <Logger.h>
#include "config/ConfigManager.h"
#include "config/SecureStorage.h"

ConfigManager::ConfigManager(const char* filename) : filename(filename), secure() {}

static auto normalizeWeatherTimezone(const String& value) -> String {
    String normalized = value;
    normalized.trim();
    if (normalized.length() == 0) {
        return "auto";
    }
    return normalized;
}

static auto normalizeWeatherLocationName(const String& value) -> String {
    String normalized = value;
    normalized.trim();
    normalized.replace("특별자치시", "시");
    normalized.replace("특별시", "시");
    normalized.replace("광역시", "시");
    normalized.replace("특별자치도", "도");

    String displayName;
    int start = 0;
    while (start < static_cast<int>(normalized.length())) {
        int space = normalized.indexOf(' ', start);
        if (space < 0) {
            space = normalized.length();
        }

        String part = normalized.substring(start, space);
        part.trim();
        if (part.length() > 0 && !part.endsWith("도")) {
            displayName = part;
        }

        start = space + 1;
    }

    return displayName.length() > 0 ? displayName : normalized;
}

static auto clampDisplayBrightness(int value) -> uint8_t {
    if (value < 5) {
        return 5;
    }
    if (value > 100) {
        return 100;
    }
    return static_cast<uint8_t>(value);
}

static auto clampHourOfDay(int value) -> uint8_t {
    if (value < 0) {
        return 0;
    }
    if (value > 23) {
        return 23;
    }
    return static_cast<uint8_t>(value);
}

static auto minuteOfDayToHour(int value) -> uint8_t {
    if (value < 0) {
        return 0;
    }
    if (value > 1439) {
        value = 1439;
    }
    return static_cast<uint8_t>(value / 60);
}

static auto timezoneRegionFromOffsetMinutes(int offsetMinutes) -> String {
    switch (offsetMinutes) {
        case 540:
            return "Asia/Seoul";
        case 480:
            return "Asia/Shanghai";
        case 420:
            return "Asia/Bangkok";
        case 60:
            return "Europe/Berlin";
        case 0:
            return "UTC";
        case -300:
            return "America/New_York";
        case -360:
            return "America/Chicago";
        case -420:
            return "America/Denver";
        case -480:
            return "America/Los_Angeles";
        case 600:
            return "Australia/Sydney";
        default:
            return "Asia/Seoul";
    }
}

static auto normalizeTimezoneRegion(const String& value) -> String {
    String normalized = value;
    normalized.trim();

    if (normalized == "Asia/Seoul" || normalized == "Asia/Tokyo" || normalized == "Asia/Shanghai" ||
        normalized == "Asia/Singapore" || normalized == "Asia/Bangkok" || normalized == "Europe/London" ||
        normalized == "Europe/Berlin" || normalized == "UTC" || normalized == "America/New_York" ||
        normalized == "America/Chicago" || normalized == "America/Denver" ||
        normalized == "America/Los_Angeles" || normalized == "Australia/Sydney") {
        return normalized;
    }

    return "Asia/Seoul";
}

/**
 * @brief Loads the configuration from a file stored in SPIFFS
 *
 * @return true if the configuration was successfully loaded and parsed false otherwise
 */
auto ConfigManager::load() -> bool {
    if (!LittleFS.begin()) {
        Logger::error("Failed to mount LittleFS", "ConfigManager");
        return false;
    }

    File file = LittleFS.open(filename.c_str(), "r");
    if (!file) {
        this->ssid = secure.get("wifi_ssid").c_str();
        this->password = secure.get("wifi_password").c_str();
        return save();
    }

    size_t size = file.size();
    if (size == 0) {
        file.close();
        this->ssid = secure.get("wifi_ssid").c_str();
        this->password = secure.get("wifi_password").c_str();
        return save();
    }

    std::unique_ptr<char[]> buf(new char[size + 1]);
    file.readBytes(buf.get(), size);
    buf[size] = '\0';
    file.close();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, buf.get());
    if (error) {
        Logger::error(("Failed to parse config file : " + String(error.c_str())).c_str(), "ConfigManager");
        return false;
    }

    String ssid = doc["wifi_ssid"] | "";
    String password = doc["wifi_password"] | "";
    String ntp_server_cfg = doc["ntp_server"] | "";
    bool clock_enabled_cfg = doc["clock_enabled"] | clock_enabled;
    bool clock_use_24h_cfg = doc["clock_use_24h"] | clock_use_24h;
    bool weather_enabled_cfg = doc["weather_enabled"] | weather_enabled;
    float weather_latitude_cfg = doc["weather_latitude"] | weather_latitude;
    float weather_longitude_cfg = doc["weather_longitude"] | weather_longitude;
    int weather_kma_grid_x_cfg = doc["weather_kma_grid_x"] | weather_kma_grid_x;
    int weather_kma_grid_y_cfg = doc["weather_kma_grid_y"] | weather_kma_grid_y;
    String weather_timezone_cfg = doc["weather_timezone"] | weather_timezone.c_str();
    String weather_location_name_cfg = doc["weather_location_name"] | weather_location_name.c_str();
    String weather_kma_api_key_cfg = doc["weather_kma_api_key"] | weather_kma_api_key.c_str();
    String timezone_region_cfg = doc["timezone_region"] | "";
    int timezone_offset_minutes_cfg = doc["timezone_offset_minutes"] | timezone_offset_minutes;

    this->lcd_rotation = doc["lcd_rotation"] | lcd_rotation;
    this->display_brightness = clampDisplayBrightness(doc["display_brightness"] | display_brightness);
    this->display_night_brightness_enabled =
        doc["display_night_brightness_enabled"] | display_night_brightness_enabled;
    this->display_night_brightness =
        clampDisplayBrightness(doc["display_night_brightness"] | display_night_brightness);
    this->display_night_start_hour = doc["display_night_start_hour"].is<int>()
                                         ? clampHourOfDay(doc["display_night_start_hour"])
                                         : minuteOfDayToHour(doc["display_night_start_minute"] |
                                                             (display_night_start_hour * 60));
    this->display_night_end_hour = doc["display_night_end_hour"].is<int>()
                                       ? clampHourOfDay(doc["display_night_end_hour"])
                                       : minuteOfDayToHour(doc["display_night_end_minute"] |
                                                           (display_night_end_hour * 60));
    this->clock_enabled = clock_enabled_cfg;
    this->clock_use_24h = clock_use_24h_cfg;
    this->weather_enabled = weather_enabled_cfg;
    this->weather_latitude = weather_latitude_cfg;
    this->weather_longitude = weather_longitude_cfg;
    this->weather_kma_grid_x = weather_kma_grid_x_cfg;
    this->weather_kma_grid_y = weather_kma_grid_y_cfg;
    this->weather_timezone = normalizeWeatherTimezone(weather_timezone_cfg).c_str();
    this->weather_location_name = normalizeWeatherLocationName(weather_location_name_cfg).c_str();
    weather_kma_api_key_cfg.trim();
    this->weather_kma_api_key = weather_kma_api_key_cfg.c_str();
    this->timezone_region =
        normalizeTimezoneRegion(timezone_region_cfg.length() == 0 ? timezoneRegionFromOffsetMinutes(timezone_offset_minutes_cfg)
                                                                  : timezone_region_cfg)
            .c_str();
    this->timezone_offset_minutes = timezone_offset_minutes_cfg;

    String nvs_ssid = secure.get("wifi_ssid", "");
    String nvs_password = secure.get("wifi_password", "");
    if ((ssid.length() != 0 && nvs_ssid.length() == 0) || (password.length() != 0 && nvs_password.length() == 0)) {
        secure.put("wifi_ssid", ssid.c_str());
        secure.put("wifi_password", password.c_str());

        this->ssid = secure.get("wifi_ssid").c_str();
        this->password = secure.get("wifi_password").c_str();

        if (ntp_server_cfg.length() != 0) {
            this->ntp_server = ntp_server_cfg.c_str();
        }

        // Ensure we delete the wifi credentials from the json config after migrating
        ConfigManager::save();

        Logger::info("WiFi credentials migrated to SecureStorage", "ConfigManager");
    } else {
        this->ssid = secure.get("wifi_ssid").c_str();
        this->password = secure.get("wifi_password").c_str();
    }

    return true;
}

/**
 * @brief Retrieves the current Wi-Fi SSID
 *
 * @return The SSID as a c style string
 */
auto ConfigManager::getSSID() const -> const char* { return ssid.c_str(); }

/**
 * @brief Retrieves the current Wi-Fi password
 *
 * @return The password as a c style string
 */
auto ConfigManager::getPassword() const -> const char* { return password.c_str(); }

auto ConfigManager::isClockEnabled() const -> bool { return clock_enabled; }

auto ConfigManager::setClockEnabled(bool enabled) -> void { clock_enabled = enabled; }

auto ConfigManager::isClockUse24Hour() const -> bool { return clock_use_24h; }

auto ConfigManager::setClockUse24Hour(bool enabled) -> void { clock_use_24h = enabled; }

auto ConfigManager::isWeatherEnabled() const -> bool { return weather_enabled; }

auto ConfigManager::setWeatherEnabled(bool enabled) -> void { weather_enabled = enabled; }

auto ConfigManager::getWeatherLatitude() const -> float { return weather_latitude; }

auto ConfigManager::setWeatherLatitude(float latitude) -> void { weather_latitude = latitude; }

auto ConfigManager::getWeatherLongitude() const -> float { return weather_longitude; }

auto ConfigManager::setWeatherLongitude(float longitude) -> void { weather_longitude = longitude; }

auto ConfigManager::getWeatherKmaGridX() const -> int { return weather_kma_grid_x; }

auto ConfigManager::setWeatherKmaGridX(int gridX) -> void { weather_kma_grid_x = gridX; }

auto ConfigManager::getWeatherKmaGridY() const -> int { return weather_kma_grid_y; }

auto ConfigManager::setWeatherKmaGridY(int gridY) -> void { weather_kma_grid_y = gridY; }

auto ConfigManager::getWeatherTimezone() const -> const char* { return weather_timezone.c_str(); }

auto ConfigManager::setWeatherTimezone(const char* timezone) -> void {
    if (timezone != nullptr) {
        weather_timezone = normalizeWeatherTimezone(timezone).c_str();
    }
}

auto ConfigManager::getWeatherLocationName() const -> const char* { return weather_location_name.c_str(); }

auto ConfigManager::setWeatherLocationName(const char* locationName) -> void {
    if (locationName != nullptr) {
        weather_location_name = normalizeWeatherLocationName(locationName).c_str();
    }
}

auto ConfigManager::getWeatherKmaApiKey() const -> const char* { return weather_kma_api_key.c_str(); }

auto ConfigManager::setWeatherKmaApiKey(const char* apiKey) -> void {
    if (apiKey != nullptr) {
        String normalized = apiKey;
        normalized.trim();
        weather_kma_api_key = normalized.c_str();
    }
}

auto ConfigManager::getTimezoneRegion() const -> const char* { return timezone_region.c_str(); }

auto ConfigManager::setTimezoneRegion(const char* region) -> void {
    if (region != nullptr) {
        timezone_region = normalizeTimezoneRegion(region).c_str();
    }
}

auto ConfigManager::getTimezoneOffsetMinutes() const -> int { return timezone_offset_minutes; }

auto ConfigManager::setTimezoneOffsetMinutes(int offsetMinutes) -> void { timezone_offset_minutes = offsetMinutes; }

/**
 * @brief Retrieves the LCD rotation setting
 *
 * @return The rotation of the LCD
 */
auto ConfigManager::getLCDRotation() const -> uint8_t { return lcd_rotation; }

/**
 * @brief Set LCD rotation in memory
 *
 * @param newRotation Rotation value in range [0, 7]
 *
 * @return void
 */
auto ConfigManager::setLCDRotation(uint8_t newRotation) -> void { lcd_rotation = newRotation; }

auto ConfigManager::getDisplayBrightness() const -> uint8_t { return display_brightness; }

auto ConfigManager::setDisplayBrightness(uint8_t brightnessPercent) -> void {
    display_brightness = clampDisplayBrightness(brightnessPercent);
}

auto ConfigManager::isDisplayNightBrightnessEnabled() const -> bool { return display_night_brightness_enabled; }

auto ConfigManager::setDisplayNightBrightnessEnabled(bool enabled) -> void {
    display_night_brightness_enabled = enabled;
}

auto ConfigManager::getDisplayNightBrightness() const -> uint8_t { return display_night_brightness; }

auto ConfigManager::setDisplayNightBrightness(uint8_t brightnessPercent) -> void {
    display_night_brightness = clampDisplayBrightness(brightnessPercent);
}

auto ConfigManager::getDisplayNightStartHour() const -> uint8_t { return display_night_start_hour; }

auto ConfigManager::setDisplayNightStartHour(uint8_t hourOfDay) -> void {
    display_night_start_hour = clampHourOfDay(hourOfDay);
}

auto ConfigManager::getDisplayNightEndHour() const -> uint8_t { return display_night_end_hour; }

auto ConfigManager::setDisplayNightEndHour(uint8_t hourOfDay) -> void {
    display_night_end_hour = clampHourOfDay(hourOfDay);
}

/**
 * @brief Set WiFi credentials in memory
 * @param newSsid The SSID
 * @param newPassword The password
 *
 * @return void
 */
auto ConfigManager::setWiFi(const char* newSsid, const char* newPassword) -> void {
    if (newSsid != nullptr) {
        ssid = newSsid;
    }
    if (newPassword != nullptr) {
        password = newPassword;
    }
}
/**
 * @brief Save the current configuration to the file
 *
 * @param clearWifiCreds If true wifi credentials will be cleared from json config
 *
 * @return true if the configuration was successfully saved false otherwise
 */
auto ConfigManager::save() -> bool {
    if (!LittleFS.begin()) {
        Logger::error("Failed to mount LittleFS", "ConfigManager");

        return false;
    }

    File file = LittleFS.open(filename.c_str(), "w");

    if (!file) {
        Logger::error("Failed to open config file for writing", "ConfigManager");

        return false;
    }

    JsonDocument doc;

    secure.put("wifi_ssid", this->getSSID());
    secure.put("wifi_password", this->getPassword());

    doc["lcd_rotation"] = lcd_rotation;
    doc["display_brightness"] = display_brightness;
    doc["display_night_brightness_enabled"] = display_night_brightness_enabled;
    doc["display_night_brightness"] = display_night_brightness;
    doc["display_night_start_hour"] = display_night_start_hour;
    doc["display_night_end_hour"] = display_night_end_hour;
    doc["clock_enabled"] = clock_enabled;
    doc["clock_use_24h"] = clock_use_24h;
    doc["weather_enabled"] = weather_enabled;
    doc["weather_latitude"] = weather_latitude;
    doc["weather_longitude"] = weather_longitude;
    doc["weather_kma_grid_x"] = weather_kma_grid_x;
    doc["weather_kma_grid_y"] = weather_kma_grid_y;
    doc["weather_timezone"] = weather_timezone.c_str();
    doc["weather_location_name"] = weather_location_name.c_str();
    doc["weather_kma_api_key"] = weather_kma_api_key.c_str();
    doc["timezone_region"] = timezone_region.c_str();
    doc["timezone_offset_minutes"] = timezone_offset_minutes;
    if (!this->ntp_server.empty()) {
        doc["ntp_server"] = this->ntp_server.c_str();
    }

    if (serializeJson(doc, file) == 0) {
        Logger::error("Failed to write config file", "ConfigManager");
        file.close();

        return false;
    }

    file.close();
    Logger::info("Configuration saved", "ConfigManager");

    return true;
}
