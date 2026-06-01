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

ConfigManager configManager;
const char* AP_SSID = "GeekMagic";
const char* AP_PASSWORD = "";
WiFiManager* wifiManager = nullptr;
ESP8266HTTPUpdateServer httpUpdater;
bool g_legacyUpdateModeEnabled = false;
static constexpr const char* KV_SALT_STR = "GeekMagicOpenFirmwareIsAwesome";

static constexpr uint32_t SERIAL_BAUD_RATE = 115200;
static constexpr uint32_t BOOT_DELAY_MS = 200;
static constexpr unsigned long SERVICE_POLL_INTERVAL_MS = 1000UL;
static constexpr unsigned long DISPLAY_UPDATE_INTERVAL_MS = 100UL;
static constexpr unsigned long LOOP_IDLE_DELAY_MS = 1UL;

Webserver* webserver = nullptr;
NTPClient* ntpClient = nullptr;
WeatherClient* weatherClient = nullptr;

static unsigned long g_nextDisplayUpdateMs = 0;
static unsigned long g_nextServicePollMs = 0;

static void runBootDashboardFlow() {
    DisplayManager::pauseClock(0);
    DisplayManager::drawClock();
}

static void pumpDisplayDue(bool force = false) {
    const unsigned long now = millis();
    if (force || static_cast<long>(now - g_nextDisplayUpdateMs) >= 0) {
        DisplayManager::update();
        g_nextDisplayUpdateMs = millis() + DISPLAY_UPDATE_INTERVAL_MS;
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
    Logger::info(("SmallTV-Ultra Korean Custom Firmware " + String(PROJECT_VER_STR)).c_str());

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

    registerApiEndpoints(webserver);
    httpUpdater.setup(&webserver->raw(), "/legacyupdate");

    if (!littleFsReadyForStatic) {
        g_legacyUpdateModeEnabled = true;
        Logger::warn("Enabled legacy OTA route because LittleFS is unavailable or empty", "Global");
    } else {
        g_legacyUpdateModeEnabled = false;
        webserver->serveStaticC("/", "/web/index.html", "text/html");
        webserver->registerGenericStaticFallback("/web", true);
    }

    // enable watchdog before going to loop()
    // Network reads can briefly block on ESP8266 WiFi internals.
    EspClass::wdtEnable(WDTO_8S);
}

void loop() {
    pumpDisplayDue();

    if (webserver != nullptr) {
        webserver->handleClient();
        pumpDisplayDue();
    }

    if (static_cast<long>(millis() - g_nextServicePollMs) >= 0) {
        if (ntpClient != nullptr) {
            ntpClient->loop();
            pumpDisplayDue();
        }

        if (weatherClient != nullptr) {
            weatherClient->loop();
            pumpDisplayDue(true);
        }

        g_nextServicePollMs = millis() + SERVICE_POLL_INTERVAL_MS;
    }

    EspClass::wdtFeed();  // kick watchdog
    delay(LOOP_IDLE_DELAY_MS);
}
