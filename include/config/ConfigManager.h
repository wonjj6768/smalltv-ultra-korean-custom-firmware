// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * GeekMagic Open Firmware
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

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <ArduinoJson.h>
#include "config/SecureStorage.h"
#include <string>
#include <cstdint>
#include <SPI.h>

// LCD configuration defaults for SmallTV-Ultra
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
    ConfigManager(const char* filename = "/config.json");
    bool load();
    bool save();
    void setWiFi(const char* newSsid, const char* newPassword);
    const char* getSSID() const;
    const char* getPassword() const;
    uint8_t getLCDRotation() const;
    void setLCDRotation(uint8_t newRotation);
    bool isClockEnabled() const;
    void setClockEnabled(bool enabled);
    bool isClockUse24Hour() const;
    void setClockUse24Hour(bool enabled);
    bool isWeatherEnabled() const;
    void setWeatherEnabled(bool enabled);
    float getWeatherLatitude() const;
    void setWeatherLatitude(float latitude);
    float getWeatherLongitude() const;
    void setWeatherLongitude(float longitude);
    const char* getWeatherTimezone() const;
    void setWeatherTimezone(const char* timezone);
    const char* getWeatherLocationName() const;
    void setWeatherLocationName(const char* locationName);
    const char* getTimezoneRegion() const;
    void setTimezoneRegion(const char* region);
    int getTimezoneOffsetMinutes() const;
    void setTimezoneOffsetMinutes(int offsetMinutes);
    uint32_t getLCDSpiHz() const;

   public:
    uint8_t getLCDRotationSafe() const { return lcd_rotation; }
    std::string ssid;
    std::string password;
    std::string filename;
    SecureStorage secure;
    uint8_t lcd_rotation = 0;
    bool clock_enabled = true;
    bool clock_use_24h = true;
    bool weather_enabled = true;
    float weather_latitude = 37.566F;
    float weather_longitude = 126.9784F;
    std::string weather_timezone = "Asia/Seoul";
    std::string weather_location_name = "서울특별시";
    std::string timezone_region = "Asia/Seoul";
    int timezone_offset_minutes = 540;
    std::string ntp_server;

    const char* getNtpServer() const { return ntp_server.c_str(); }
    void setNtpServer(const char* s) {
        if (s) ntp_server = s;
    }
};

#endif  // CONFIG_MANAGER_H
