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

#include <Arduino.h>
#include <LittleFS.h>
#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include <ESP8266HTTPUpdateServer.h>

#include <Logger.h>
#include "project_version.h"
#include "config/ConfigManager.h"
#include "wireless/WiFiManager.h"
#include "display/DisplayManager.h"
#include "web/Webserver.h"
#include "web/Api.h"
#include "ntp/NTPClient.h"
#include "weather/WeatherClient.h"
#include <array>

ConfigManager configManager;
const char* AP_SSID = "GeekMagic";
const char* AP_PASSWORD = "";
WiFiManager* wifiManager = nullptr;
ESP8266HTTPUpdateServer httpUpdater;
bool g_legacyUpdateModeEnabled = false;
static constexpr const char* KV_SALT_STR = "GeekMagicOpenFirmwareIsAwesome";
static size_t initial_free_heap = 0;
static constexpr size_t FREE_BUF_SIZE = 32;
static constexpr size_t MSG_BUF_SIZE = 96;

static constexpr uint32_t SERIAL_BAUD_RATE = 115200;
static constexpr uint32_t BOOT_DELAY_MS = 200;
static constexpr unsigned long SERVICE_POLL_INTERVAL_MS = 1000UL;
static constexpr unsigned long DISPLAY_UPDATE_INTERVAL_MS = 100UL;
static constexpr unsigned long LOOP_IDLE_DELAY_MS = 5UL;

Webserver* webserver = nullptr;
NTPClient* ntpClient = nullptr;
WeatherClient* weatherClient = nullptr;

static void runBootDashboardFlow() {
    DisplayManager::pauseClock(0);
    DisplayManager::drawClock();
}

/**
 * @brief Formats bytes into a human-readable string
 *
 * @param value Size in bytes
 * @return Formatted string
 */
static void formatBytes(size_t value, char* outBuf, size_t outBufSize) {
    constexpr std::array<const char*, 5> UNITS = {"B", "KB", "MB", "GB", "TB"};
    constexpr size_t THRESHOLD = 1024U;

    size_t scaledTenths = value * 10U;
    int unit = 0;
    while (scaledTenths >= (THRESHOLD * 10U) && unit < static_cast<int>(UNITS.size()) - 1) {
        scaledTenths = (scaledTenths + (THRESHOLD / 2U)) / THRESHOLD;
        ++unit;
    }

    if (unit == 0) {
        snprintf(outBuf, outBufSize, "%u %s", static_cast<unsigned int>(value), UNITS[unit]);
    } else {
        snprintf(outBuf, outBufSize, "%u.%u %s", static_cast<unsigned int>(scaledTenths / 10U),
                 static_cast<unsigned int>(scaledTenths % 10U), UNITS[unit]);
    }
}

/**
 * @brief Check whether LittleFS contains at least one entry
 *
 * @return true if filesystem root has any file/dir entry
 */
static auto littleFsHasEntries() -> bool {
    Dir dir = LittleFS.openDir("/");
    return dir.next();
}

/**
 * @brief Initializes the system
 *
 */
void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(BOOT_DELAY_MS);
    Serial.println("");
    Logger::info(("GeekMagic Open Firmware " + String(PROJECT_VER_STR)).c_str());

    const bool littleFsMounted = LittleFS.begin();
    bool littleFsReadyForStatic = littleFsMounted;

    if (!littleFsMounted) {
        Logger::error("Failed to mount LittleFS");
        Logger::warn("LittleFS unavailable, static web UI disabled", "Global");
    } else if (!littleFsHasEntries()) {
        littleFsReadyForStatic = false;
        Logger::warn("LittleFS mounted but empty, static web UI disabled", "Global");
    }

    SecureStorage::setSalt(KV_SALT_STR);

    if (configManager.secure.begin()) {
        Logger::info("SecureStorage initialized successfully", "ConfigManager");
    }

    if (configManager.load()) {
        Logger::info("Configuration loaded successfully");
    }

    if (!configManager.isClockEnabled() && !configManager.isWeatherEnabled()) {
        configManager.setClockEnabled(true);
        configManager.setWeatherEnabled(true);
        configManager.save();
        Logger::info("Enabled dashboard defaults for boot", "Global");
    }

    DisplayManager::begin();

    wifiManager = new WiFiManager(configManager.getSSID(), configManager.getPassword(), AP_SSID, AP_PASSWORD);
    wifiManager->begin();

    ntpClient = new NTPClient();
    ntpClient->begin();

    weatherClient = new WeatherClient();
    weatherClient->begin();
    runBootDashboardFlow();

    webserver = new Webserver();
    webserver->begin();

    initial_free_heap = ESP.getFreeHeap();  // NOLINT(readability-static-accessed-through-instance)

    registerApiEndpoints(webserver);

    if (!littleFsReadyForStatic) {
        g_legacyUpdateModeEnabled = true;
        httpUpdater.setup(&webserver->raw(), "/legacyupdate");
        Logger::warn("Enabled legacy OTA route because LittleFS is unavailable or empty", "Global");
    } else {
        g_legacyUpdateModeEnabled = false;
        webserver->serveStaticC("/", "/web/index.html", "text/html");
        webserver->serveStaticC("/config.json", "/config.json", "application/json");
        webserver->registerGenericStaticFallback("/web", true);
    }

    // enable watchdog before going to loop()
    // 2 seconds should be way more than the main loop needs to do stuff
    EspClass::wdtEnable(WDTO_2S);
}

void loop() {
    const unsigned long now = millis();

    if (webserver != nullptr) {
        webserver->handleClient();
    }

    static unsigned long last_service_poll_ms = 0;
    if (static_cast<long>(now - last_service_poll_ms) >= 0) {
        if (ntpClient != nullptr) {
            ntpClient->loop();
        }

        if (weatherClient != nullptr) {
            weatherClient->loop();
        }

        last_service_poll_ms = now + SERVICE_POLL_INTERVAL_MS;
    }

    static unsigned long last_display_update_ms = 0;
    if (static_cast<long>(now - last_display_update_ms) >= 0) {
        DisplayManager::update();
        last_display_update_ms = now + DISPLAY_UPDATE_INTERVAL_MS;
    }

    static unsigned long last_free_heap_log = 0;
    static constexpr unsigned long FREE_HEAP_LOG_INTERVAL_MS = 10000UL;

    if (now - last_free_heap_log >= FREE_HEAP_LOG_INTERVAL_MS) {
        last_free_heap_log = now;
        char freeBuf[FREE_BUF_SIZE];
        char initBuf[FREE_BUF_SIZE];
        char msgBuf[MSG_BUF_SIZE];

        formatBytes(ESP.getFreeHeap(), freeBuf,  // NOLINT(readability-static-accessed-through-instance)
                    sizeof(freeBuf));
        formatBytes(initial_free_heap, initBuf, sizeof(initBuf));

        snprintf(msgBuf, sizeof(msgBuf), "Free heap: %s (initial: %s)", freeBuf, initBuf);
        Logger::info(msgBuf);
    }

    EspClass::wdtFeed();  // kick watchdog
    delay(LOOP_IDLE_DELAY_MS);
}
