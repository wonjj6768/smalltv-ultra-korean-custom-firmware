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
#include <Logger.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <Updater.h>
#include <WiFiClient.h>
#include <time.h>

#include "web/Webserver.h"
#include "web/Api.h"
#include "project_version.h"
#include "display/DisplayManager.h"

#include "config/ConfigManager.h"
#include "wireless/WiFiManager.h"
#include "ntp/NTPClient.h"
#include "weather/WeatherClient.h"

extern ConfigManager configManager;
extern WiFiManager* wifiManager;
extern NTPClient* ntpClient;
extern WeatherClient* weatherClient;

static bool otaError = false;
static size_t otaSize = 0;
static String otaStatus;
static volatile bool otaInProgress = false;
static volatile bool otaCancelRequested = false;
static size_t otaTotal = 0;
static bool otaFsUnmounted = false;
static int otaMode = U_FLASH;
static unsigned long otaLastProgressDrawMs = 0;
static int otaLastProgressPercent = -1;

static constexpr int OTA_TEXT_X_OFFSET = 50;
static constexpr int OTA_TEXT_Y_OFFSET = 80;
static constexpr int OTA_LOADING_Y_OFFSET = 110;

static void otaHandleStart(HTTPUpload& upload, int mode);
static void otaHandleWrite(HTTPUpload& upload);
static void otaHandleEnd(HTTPUpload& upload, int mode);
static void otaHandleAborted(HTTPUpload& upload);
void handleDeleteGif(Webserver* webserver);

static constexpr int WIFI_CONNECT_TIMEOUT_MS = 15000;
static constexpr size_t NTP_CONFIG_DOC_SIZE = 512;
static constexpr const char* DEFAULT_NTP_SERVER = "pool.ntp.org";
static constexpr const char* KMA_VALIDATE_HOST = "apihub.kma.go.kr";
static constexpr uint16_t KMA_VALIDATE_PORT = 80;
static constexpr int KMA_VALIDATE_SEOUL_X = 60;
static constexpr int KMA_VALIDATE_SEOUL_Y = 127;

static void sendJsonContent(Webserver* webserver, const char* content) { webserver->raw().sendContent(content); }

static void sendJsonEscapedString(Webserver* webserver, const String& value) {
    sendJsonContent(webserver, "\"");
    char buffer[2] = {};
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value.charAt(i);
        switch (c) {
            case '\"':
                sendJsonContent(webserver, "\\\"");
                break;
            case '\\':
                sendJsonContent(webserver, "\\\\");
                break;
            case '\n':
                sendJsonContent(webserver, "\\n");
                break;
            case '\r':
                sendJsonContent(webserver, "\\r");
                break;
            case '\t':
                sendJsonContent(webserver, "\\t");
                break;
            default:
                buffer[0] = c;
                webserver->raw().sendContent(buffer, 1);
                break;
        }
    }
    sendJsonContent(webserver, "\"");
}

static void sendJsonFloat(Webserver* webserver, float value, uint8_t decimals = 1) {
    char buffer[24] = {};
    dtostrf(value, 0, decimals, buffer);
    sendJsonContent(webserver, buffer);
}

static void sendJsonInt(Webserver* webserver, long value) {
    char buffer[24] = {};
    snprintf(buffer, sizeof(buffer), "%ld", value);
    sendJsonContent(webserver, buffer);
}

static auto timezoneOffsetForRegion(const String& region) -> int {
    if (region == "Asia/Seoul" || region == "Asia/Tokyo") {
        return 540;
    }
    if (region == "Asia/Shanghai" || region == "Asia/Singapore") {
        return 480;
    }
    if (region == "Asia/Bangkok") {
        return 420;
    }
    if (region == "Europe/Berlin") {
        return 60;
    }
    if (region == "Europe/London" || region == "UTC") {
        return 0;
    }
    if (region == "America/New_York") {
        return -300;
    }
    if (region == "America/Chicago") {
        return -360;
    }
    if (region == "America/Denver") {
        return -420;
    }
    if (region == "America/Los_Angeles") {
        return -480;
    }
    if (region == "Australia/Sydney") {
        return 600;
    }

    return 540;
}

/**
 * @brief Register API endpoints for the webserver
 * @param webserver Pointer to the Webserver instance
 *
 * @return void
 */
void registerApiEndpoints(Webserver* webserver) {
    Logger::info("Registering API endpoints", "API");

    // @openapi {get} /wifi/scan version=v1 group=WiFi summary="Scan available WiFi networks" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/wifi/scan", HTTP_GET, [webserver]() { handleWifiScan(webserver); });

    // @openapi {post} /wifi/connect version=v1 group=WiFi summary="Connect to a WiFi network" requiresAuth=true
    // requestBody=application/json requestBodySchema=ssid:string,password:string
    // example={"ssid":"MyNetwork","password":"password123"}
    // responses=200:application/json,400:application/json,401:application/json
    webserver->raw().on("/api/v1/wifi/connect", HTTP_POST, [webserver]() { handleWifiConnect(webserver); });

    // @openapi {get} /wifi/status version=v1 group=WiFi summary="Get WiFi connection status" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/wifi/status", HTTP_GET, [webserver]() { handleWifiStatus(webserver); });

    // @openapi {post} /ntp/sync version=v1 group=NTP summary="Trigger NTP sync" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/ntp/sync", HTTP_POST, [webserver]() { handleNtpSync(webserver); });

    // @openapi {get} /ntp/status version=v1 group=NTP summary="Get NTP status" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/ntp/status", HTTP_GET, [webserver]() { handleNtpStatus(webserver); });

    // @openapi {get} /ntp/config version=v1 group=NTP summary="Get NTP configuration" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/ntp/config", HTTP_GET, [webserver]() { handleNtpConfigGet(webserver); });

    // @openapi {post} /ntp/config version=v1 group=NTP summary="Set NTP configuration" requiresAuth=true
    // requestBody=application/json requestBodySchema=ntp_server:string example={"ntp_server":"pool.ntp.org"}
    // responses=200:application/json,400:application/json,401:application/json
    webserver->raw().on("/api/v1/ntp/config", HTTP_POST, [webserver]() { handleNtpConfigSet(webserver); });

    // @openapi {get} /weather/config version=v1 group=Weather summary="Get weather configuration" requiresAuth=true
    webserver->raw().on("/api/v1/weather/config", HTTP_GET, [webserver]() { handleWeatherConfigGet(webserver); });

    // @openapi {post} /weather/config version=v1 group=Weather summary="Set weather configuration" requiresAuth=true
    webserver->raw().on("/api/v1/weather/config", HTTP_POST, [webserver]() { handleWeatherConfigSet(webserver); });

    // @openapi {get} /weather/status version=v1 group=Weather summary="Get cached weather data" requiresAuth=true
    webserver->raw().on("/api/v1/weather/status", HTTP_GET, [webserver]() { handleWeatherStatusGet(webserver); });

    // @openapi {post} /weather/refresh version=v1 group=Weather summary="Refresh weather data now" requiresAuth=true
    webserver->raw().on("/api/v1/weather/refresh", HTTP_POST, [webserver]() { handleWeatherRefresh(webserver); });

    webserver->raw().on("/api/v1/weather/validate-key", HTTP_POST,
                        [webserver]() { handleWeatherValidateKey(webserver); });
    // @openapi {get} /display/rotation version=v1 group=Display summary="Get display rotation" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/display/rotation", HTTP_GET, [webserver]() { handleDisplayRotationGet(webserver); });

    // @openapi {get} /display/clock version=v1 group=Display summary="Get clock display settings" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/display/clock", HTTP_GET, [webserver]() { handleDisplayClockGet(webserver); });

    // @openapi {post} /display/clock version=v1 group=Display summary="Set clock display settings" requiresAuth=true
    // requestBody=application/json requestBodySchema=enabled:boolean,use24h:boolean
    // responses=200:application/json,400:application/json,401:application/json
    webserver->raw().on("/api/v1/display/clock", HTTP_POST, [webserver]() { handleDisplayClockSet(webserver); });

    // @openapi {post} /display/rotation version=v1 group=Display summary="Set display rotation" requiresAuth=true
    // requestBody=application/json requestBodySchema=rotation:integer example={"rotation":4}
    // responses=200:application/json,400:application/json,401:application/json
    webserver->raw().on("/api/v1/display/rotation", HTTP_POST, [webserver]() { handleDisplayRotationSet(webserver); });

    webserver->raw().on("/api/v1/display/brightness", HTTP_GET,
                        [webserver]() { handleDisplayBrightnessGet(webserver); });
    webserver->raw().on("/api/v1/display/brightness", HTTP_POST,
                        [webserver]() { handleDisplayBrightnessSet(webserver); });

    // @openapi {post} /display/startup version=v1 group=Display summary="Redraw the startup screen" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/display/startup", HTTP_POST, [webserver]() { handleDisplayStartup(webserver); });

    // @openapi {post} /reboot version=v1 group=System summary="Reboot the device" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/reboot", HTTP_POST, [webserver]() { handleReboot(webserver); });

    // @openapi {post} /factory-reset version=v1 group=System summary="Reset user settings and reboot" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/factory-reset", HTTP_POST, [webserver]() { handleFactoryReset(webserver); });

    // @openapi {get} /system/version version=v1 group=System summary="Get firmware version" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/system/version", HTTP_GET, [webserver]() { handleSystemVersion(webserver); });

    webserver->raw().on("/api/v1/system/update-available", HTTP_POST,
                        [webserver]() { handleSystemUpdateAvailableSet(webserver); });

    // @openapi {post} /ota/fw version=v1 group=OTA summary="Upload firmware (OTA)" requiresAuth=true
    // requestBody=multipart/form-data responses=200:application/json,401:application/json
    webserver->raw().on(
        "/api/v1/ota/fw", HTTP_POST, [webserver]() { handleOtaFinished(webserver); },
        [webserver]() { handleOtaUpload(webserver, U_FLASH); });

    // @openapi {post} /ota/fs version=v1 group=OTA summary="Upload filesystem (OTA)" requiresAuth=true
    // requestBody=multipart/form-data responses=200:application/json,401:application/json
    webserver->raw().on(
        "/api/v1/ota/fs", HTTP_POST, [webserver]() { handleOtaFinished(webserver); },
        [webserver]() { handleOtaUpload(webserver, U_FS); });

    // @openapi {get} /ota/status version=v1 group=OTA summary="Get OTA status" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/ota/status", HTTP_GET, [webserver]() { handleOtaStatus(webserver); });

    // @openapi {post} /ota/cancel version=v1 group=OTA summary="Cancel OTA" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/ota/cancel", HTTP_POST, [webserver]() { handleOtaCancel(webserver); });

    // @openapi {post} /gif version=v1 group=GIF summary="Upload a GIF" requiresAuth=true
    // requestBody=multipart/form-data responses=200:application/json,401:application/json
    webserver->raw().on(
        "/api/v1/gif", HTTP_POST, [webserver]() { handleGifUpload(webserver); },
        [webserver]() { handleGifUpload(webserver); });

    // @openapi {post} /gif/play version=v1 group=GIF summary="Play a GIF by name" requiresAuth=true
    // requestBody=application/json requestBodySchema=name:string example={"name":"animation.gif"}
    // responses=200:application/json,400:application/json,401:application/json,404:application/json
    webserver->raw().on("/api/v1/gif/play", HTTP_POST, [webserver]() { handlePlayGif(webserver); });

    // @openapi {post} /gif/stop version=v1 group=GIF summary="Stop GIF playback" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/gif/stop", HTTP_POST, [webserver]() { handleStopGif(webserver); });

    // @openapi {delete} /gif version=v1 group=GIF summary="Delete a GIF by name" requiresAuth=true
    // requestBody=application/json requestBodySchema=name:string example={"name":"animation.gif"}
    // responses=200:application/json,400:application/json,401:application/json,404:application/json
    webserver->raw().on("/api/v1/gif", HTTP_DELETE, [webserver]() { handleDeleteGif(webserver); });

    // @openapi {get} /gif version=v1 group=GIF summary="List GIFs" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/gif", HTTP_GET, [webserver]() { handleListGifs(webserver); });

    // @openapi {get} /logs version=v1 group=System summary="Get recent logs" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/logs", HTTP_GET, [webserver]() { handleLogsGet(webserver); });

    // @openapi {get} /logs/download version=v1 group=System summary="Download logs as text file" requiresAuth=true
    // responses=200:text/plain,401:application/json
    webserver->raw().on("/api/v1/logs/download", HTTP_GET, [webserver]() { handleLogsDownload(webserver); });

    // @openapi {post} /logs/clear version=v1 group=System summary="Clear log buffer" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/logs/clear", HTTP_POST, [webserver]() { handleLogsClear(webserver); });

    webserver->raw().onNotFound([webserver]() {
        if (webserver->raw().method() == HTTP_OPTIONS) {
            setCorsHeaders(webserver);
            webserver->raw().send(HTTP_CODE_OK);
        }
    });
}

/**
 * @brief Set CORS headers for API responses
 * @param webserver Pointer to the Webserver instance
 *
 * @return void
 */
void setCorsHeaders(Webserver* webserver) {
    webserver->raw().sendHeader("Access-Control-Allow-Origin", "*");
    webserver->raw().sendHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    webserver->raw().sendHeader("Access-Control-Allow-Headers", "Content-Type");
    webserver->raw().sendHeader("Access-Control-Max-Age", "3600");
}

/**
 * @brief Authentication is disabled, always allow API access
 * @param webserver Pointer to the Webserver instance
 *
 * @return true always
 */
static auto validateBearerToken(Webserver* webserver) -> bool {
    (void)webserver;
    return true;
}

/**
 * @brief Authentication is disabled, always allow API access
 * @param webserver Pointer to the Webserver instance
 *
 * @return true always
 */
static auto requireBearerToken(Webserver* webserver) -> bool {
    (void)webserver;
    return true;
}

/**
 * @brief OTA status endpoint
 */
void handleOtaStatus(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    JsonDocument doc;
    doc["inProgress"] = otaInProgress;
    doc["bytesWritten"] = otaSize;
    doc["totalBytes"] = otaTotal;
    doc["error"] = otaError;
    doc["message"] = otaStatus;

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);
}

/**
 * @brief OTA cancel endpoint
 */
void handleOtaCancel(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    otaCancelRequested = true;
    otaStatus = "Cancel requested";

    JsonDocument doc;
    doc["status"] = "cancelling";
    doc["message"] = "Cancel request received";

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);
}

/**
 * @brief List GIF files and FS info
 * @param webserver Pointer to the Webserver instance
 *
 * @return void
 */
void handleListGifs(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    JsonDocument doc;
    JsonArray files = doc["files"].to<JsonArray>();

    size_t usedBytes = 0;
    size_t totalBytes = 0;

    if (LittleFS.begin()) {
        Dir dir = LittleFS.openDir("/gif");

        while (dir.next()) {
            String name = dir.fileName();
            if (name.endsWith(".gif") || name.endsWith(".GIF")) {
                JsonObject fileObj = files.add<JsonObject>();

                fileObj["name"] = name;            // NOLINT(readability-misplaced-array-index)
                fileObj["size"] = dir.fileSize();  // NOLINT(readability-misplaced-array-index)
                usedBytes += dir.fileSize();
            }
        }

        FSInfo fs_info;

        if (LittleFS.info(fs_info)) {
            totalBytes = fs_info.totalBytes;
            usedBytes = fs_info.usedBytes;
        }
    }

    doc["usedBytes"] = usedBytes;
    doc["totalBytes"] = totalBytes;
    doc["freeBytes"] = totalBytes > usedBytes ? totalBytes - usedBytes : 0;

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);
}

/**
 * @brief Handle GIF upload start
 * @param currentFilename The current filename being uploaded
 * @param gifFile Reference to the File object for the GIF
 * @param uploadError Reference to the upload error flag
 *
 * @return void
 */
void handleGifUploadStart(const String& currentFilename, File& gifFile, bool& uploadError) {
    uploadError = false;
    Logger::info((String("UPLOAD_FILE_START for: ") + currentFilename).c_str(), "API::GIF");

    if (!LittleFS.exists("/gif")) {
        Logger::info("/gif directory does not exist, creating...", "API::GIF");
        if (!LittleFS.mkdir("/gif")) {
            Logger::error("Failed to create /gif directory!", "API::GIF");
        }
    }

    gifFile = LittleFS.open(currentFilename, "w");
    if (!gifFile) {
        uploadError = true;
        Logger::error((String("Impossible to open file: ") + currentFilename).c_str(), "API::GIF");
        Logger::error("GIF UPLOAD Failed to open file", "API::GIF");
    } else {
        Logger::info("File opened successfully for writing.", "API::GIF");
    }
}

/**
 * @brief Handle GIF upload write
 * @param upload Reference to the HTTPUpload object
 * @param gifFile Reference to the File object for the GIF
 * @param uploadError Reference to the upload error flag
 *
 * @return void
 */
void handleGifUploadWrite(HTTPUpload& upload, File& gifFile, bool& uploadError) {
    if (!uploadError && gifFile) {
        size_t total = 0;

        while (total < upload.currentSize) {
            size_t remaining = upload.currentSize - total;
            int toWrite = static_cast<int>(remaining > static_cast<size_t>(INT_MAX) ? INT_MAX : remaining);
            size_t written = gifFile.write(upload.buf + total, toWrite);

            if (written == 0) {
                Logger::error("Write returned 0 bytes!", "API::GIF");
                uploadError = true;
                break;
            }

            total += written;
        }
    } else {
        Logger::error("Cannot write, file not open or previous error", "API::GIF");
    }
}

/**
 * @brief Handle GIF upload end
 * @param currentFilename The current filename being uploaded
 * @param gifFile Reference to the File object for the GIF
 *
 * @return void
 */
void handleGifUploadEnd(const String& currentFilename, File& gifFile) {
    if (gifFile) {
        gifFile.close();
    }

    Logger::info((String("Gif upload end: ") + currentFilename).c_str(), "API::GIF");
}

/**
 * @brief Handle GIF upload aborted
 * @param currentFilename The current filename being uploaded
 * @param gifFile Reference to the File object for the GIF
 * @param uploadError Reference to the upload error flag
 *
 * @return void
 */
void handleGifUploadAborted(const String& currentFilename, File& gifFile, bool& uploadError) {
    Logger::warn("UPLOAD_FILE_ABORTED", "API::GIF");

    if (gifFile) {
        gifFile.close();

        Logger::warn("File closed after abort", "API::GIF");
    }

    if (!currentFilename.isEmpty()) {
        if (LittleFS.remove(currentFilename)) {
            Logger::warn((String("Removed incomplete file: ") + currentFilename).c_str(), "API::GIF");
        } else {
            Logger::error((String("Failed to remove incomplete file: ") + currentFilename).c_str(), "API::GIF");
        }
    }

    uploadError = true;
}

/**
 * @brief Send GIF upload result
 * @param webserver Pointer to the Webserver instance
 * @param currentFilename The current filename being uploaded
 * @param uploadError The upload error flag
 *
 * @return void
 */
void sendGifUploadResult(Webserver* webserver, const String& currentFilename, bool uploadError) {
    JsonDocument doc;

    if (uploadError) {
        doc["status"] = "error";
        doc["message"] = "Error during GIF upload";

        Logger::error("GIF UPLOAD Error during upload", "API::GIF");
    } else {
        doc["status"] = "success";
        doc["message"] = "GIF uploaded successfully";
        doc["filename"] = currentFilename;

        Logger::info((String("Gif upload success, filename: ") + currentFilename).c_str(), "API::GIF");
    }

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);
}

/**
 * @brief Handle GIF upload
 * @param webserver Pointer to the Webserver instance
 *
 * @return void
 */
void handleGifUpload(Webserver* webserver) {
    HTTPUpload& upload = webserver->raw().upload();
    static File gifFile;
    static bool uploadError = false;

    if (upload.status == UPLOAD_FILE_START && !validateBearerToken(webserver)) {
        uploadError = true;
        JsonDocument doc;

        doc["status"] = "error";
        doc["message"] = "Invalid or missing token";

        String json;
        serializeJson(doc, json);
        setCorsHeaders(webserver);

        webserver->raw().send(HTTP_CODE_UNAUTHORIZED, "application/json", json);

        return;
    }

    String filename = upload.filename;
    filename.replace("\\", "/");
    filename = filename.substring(filename.lastIndexOf('/') + 1);
    String currentFilename = "/gif/" + filename;

    switch (upload.status) {
        case UPLOAD_FILE_START:
            handleGifUploadStart(currentFilename, gifFile, uploadError);
            break;
        case UPLOAD_FILE_WRITE:
            handleGifUploadWrite(upload, gifFile, uploadError);
            break;
        case UPLOAD_FILE_END:
            handleGifUploadEnd(currentFilename, gifFile);
            break;
        case UPLOAD_FILE_ABORTED:
            handleGifUploadAborted(currentFilename, gifFile, uploadError);
            break;
        default:
            Logger::warn("Unknown upload status.", "API::GIF");
            break;
    }

    if (upload.status == UPLOAD_FILE_END || upload.status == UPLOAD_FILE_ABORTED) {
        sendGifUploadResult(webserver, currentFilename, uploadError);
    }
}

/**
 * @brief Reboot endpoint
 * @param webserver Pointer to the Webserver instance
 *
 * @return void
 */
void handleReboot(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    JsonDocument doc;
    int constexpr rebootDelayMs = 1000;

    doc["status"] = "rebooting";
    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);

    delay(rebootDelayMs);
    ESP.restart();  // NOLINT(readability-static-accessed-through-instance)
}

void handleFactoryReset(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    const bool configRemoved = !LittleFS.exists(configManager.filename.c_str()) ||
                               LittleFS.remove(configManager.filename.c_str());
    const bool secureCleared = configManager.secure.clear();

    JsonDocument doc;
    doc["status"] = "resetting";
    doc["configRemoved"] = configRemoved;
    doc["secureStorageCleared"] = secureCleared;
    doc["message"] = "User settings cleared. Device will reboot into setup mode.";

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);

    DisplayManager::pauseClock(3000);
    DisplayManager::clearScreen();
    DisplayManager::drawTextWrapped(20, 80, F("Factory reset..."), 2, LCD_WHITE, LCD_BLACK, true);

    delay(1000);
    WiFi.disconnect(true);
    ESP.eraseConfig();  // NOLINT(readability-static-accessed-through-instance)
    delay(300);
    ESP.restart();  // NOLINT(readability-static-accessed-through-instance)
}

void handleSystemVersion(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    JsonDocument doc;
    doc["version"] = PROJECT_VER_STR;
    doc["repo"] = "wonjj6768/smalltv-ultra-korean-custom-firmware";
    doc["latest_release_api"] =
        "https://api.github.com/repos/wonjj6768/smalltv-ultra-korean-custom-firmware/releases/latest";

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);
}

void handleSystemUpdateAvailableSet(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    String body = webserver->raw().arg("plain");
    JsonDocument request;
    DeserializationError error = deserializeJson(request, body);
    const bool available = !error && (request["available"] | false);
    DisplayManager::setUpdateAvailable(available);

    JsonDocument doc;
    doc["status"] = "ok";
    doc["available"] = available;

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);
}

/**
 * @brief Manual NTP sync trigger endpoint
 */
void handleNtpSync(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    JsonDocument doc;

    if (ntpClient == nullptr) {
        doc["status"] = "error";
        doc["message"] = "NTP client not initialized";

        String json;
        serializeJson(doc, json);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_INTERNAL_ERROR, "application/json", json);

        return;
    }

    bool syncOk = ntpClient->syncNow();
    doc["status"] = syncOk ? "ok" : "error";
    doc["lastStatus"] = ntpClient->lastStatus();
    doc["lastSyncTime"] = ntpClient->lastSyncTime();

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);
}

/**
 * @brief Return NTP status
 */
void handleNtpStatus(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    JsonDocument doc;

    if (ntpClient == nullptr) {
        doc["status"] = "error";
        doc["message"] = "NTP client not initialized";

        String json;
        serializeJson(doc, json);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_INTERNAL_ERROR, "application/json", json);
        return;
    }

    doc["lastOk"] = ntpClient->lastSyncOk();
    doc["lastStatus"] = ntpClient->lastStatus();
    doc["lastSyncTime"] = ntpClient->lastSyncTime();

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);
}

/**
 * @brief Get NTP configuration
 */
void handleNtpConfigGet(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    JsonDocument doc;
    doc["ntp_server"] =
        strlen(configManager.getNtpServer()) == 0 ? DEFAULT_NTP_SERVER : configManager.getNtpServer();
    doc["timezone_region"] = configManager.getTimezoneRegion();
    doc["timezone_offset_minutes"] = configManager.getTimezoneOffsetMinutes();

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);
}

/**
 * @brief Set NTP configuration
 */
void handleNtpConfigSet(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    if (!webserver->raw().hasArg("plain") || webserver->raw().arg("plain").length() == 0) {
        JsonDocument doc;
        doc["status"] = "error";
        doc["message"] = "Missing JSON body";

        String json;

        serializeJson(doc, json);
        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_BAD_REQUEST, "application/json", json);

        return;
    }

    String body = webserver->raw().arg("plain");
    JsonDocument ddoc;
    DeserializationError err = deserializeJson(ddoc, body);

    if (err) {
        JsonDocument doc;
        doc["status"] = "error";
        doc["message"] = "Invalid JSON";

        String json;
        serializeJson(doc, json);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_BAD_REQUEST, "application/json", json);

        return;
    }

    String server = ddoc["ntp_server"] | configManager.getNtpServer();
    server.trim();
    if (server.length() == 0) {
        server = DEFAULT_NTP_SERVER;
    }

    String timezoneRegion = ddoc["timezone_region"] | configManager.getTimezoneRegion();
    timezoneRegion.trim();
    if (timezoneRegion.length() == 0) {
        timezoneRegion = configManager.getTimezoneRegion();
    }
    const int timezoneOffsetMinutes = timezoneOffsetForRegion(timezoneRegion);

    configManager.setNtpServer(server.c_str());
    configManager.setTimezoneRegion(timezoneRegion.c_str());
    configManager.setTimezoneOffsetMinutes(timezoneOffsetMinutes);

    if (!configManager.save()) {
        JsonDocument doc;
        doc["status"] = "error";
        doc["message"] = "Failed to save config";

        String json;

        serializeJson(doc, json);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_INTERNAL_ERROR, "application/json", json);

        return;
    }

    JsonDocument doc;
    doc["status"] = "ok";
    doc["ntp_server"] = configManager.getNtpServer();
    doc["timezone_region"] = configManager.getTimezoneRegion();
    doc["timezone_offset_minutes"] = configManager.getTimezoneOffsetMinutes();
    doc["message"] = "Config saved";
    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);

    if (ntpClient != nullptr) {
        ntpClient->applyConfiguration();
        ntpClient->syncNow();
    }

    DisplayManager::pauseClock(0);
    DisplayManager::drawClock();
}

/**
 * @brief Get display rotation configuration
 */
void handleDisplayRotationGet(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    JsonDocument doc;
    doc["rotation"] = configManager.getLCDRotationSafe();

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);
}

/**
 * @brief Set display rotation configuration
 */
void handleDisplayRotationSet(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    if (!webserver->raw().hasArg("plain") || webserver->raw().arg("plain").length() == 0) {
        JsonDocument doc;
        doc["status"] = "error";
        doc["message"] = "Missing JSON body";

        String json;
        serializeJson(doc, json);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_BAD_REQUEST, "application/json", json);

        return;
    }

    String body = webserver->raw().arg("plain");
    JsonDocument ddoc;
    DeserializationError err = deserializeJson(ddoc, body);

    if (err || !ddoc["rotation"].is<int>()) {
        JsonDocument doc;
        doc["status"] = "error";
        doc["message"] = "Invalid JSON or missing rotation";

        String json;
        serializeJson(doc, json);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_BAD_REQUEST, "application/json", json);

        return;
    }

    int rotation = ddoc["rotation"].as<int>();
    const int rotation_range_min = 0;
    const int rotation_range_max = 7;

    if (rotation < rotation_range_min || rotation > rotation_range_max) {
        JsonDocument doc;
        doc["status"] = "error";
        doc["message"] =
            "rotation must be between " + String(rotation_range_min) + " and " + String(rotation_range_max);

        String json;
        serializeJson(doc, json);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_BAD_REQUEST, "application/json", json);

        return;
    }

    auto newRotation = static_cast<uint8_t>(rotation);
    configManager.setLCDRotation(newRotation);
    if (!configManager.save()) {
        JsonDocument doc;
        doc["status"] = "error";
        doc["message"] = "Failed to save config";

        String json;
        serializeJson(doc, json);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_INTERNAL_ERROR, "application/json", json);

        return;
    }

    JsonDocument doc;
    doc["status"] = "ok";
    doc["rotation"] = newRotation;

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);

    String currentIP = "unknown";
    if (wifiManager != nullptr) {
        currentIP = wifiManager->getIP().toString();
    }
    DisplayManager::setRotation(newRotation, currentIP);

    Logger::info(("Display rotation updated to " + String(newRotation)).c_str(), "API");
}

void handleDisplayBrightnessGet(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    JsonDocument doc;
    doc["brightness"] = configManager.getDisplayBrightness();
    doc["min"] = 5;
    doc["max"] = 100;
    doc["night_enabled"] = configManager.isDisplayNightBrightnessEnabled();
    doc["night_brightness"] = configManager.getDisplayNightBrightness();
    doc["night_start_hour"] = configManager.getDisplayNightStartHour();
    doc["night_end_hour"] = configManager.getDisplayNightEndHour();

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);
}

void handleDisplayBrightnessSet(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    if (!webserver->raw().hasArg("plain") || webserver->raw().arg("plain").length() == 0) {
        JsonDocument doc;
        doc["status"] = "error";
        doc["message"] = "Missing JSON body";

        String json;
        serializeJson(doc, json);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_BAD_REQUEST, "application/json", json);

        return;
    }

    String body = webserver->raw().arg("plain");
    JsonDocument ddoc;
    DeserializationError err = deserializeJson(ddoc, body);

    if (err) {
        JsonDocument doc;
        doc["status"] = "error";
        doc["message"] = "Invalid JSON";

        String json;
        serializeJson(doc, json);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_BAD_REQUEST, "application/json", json);

        return;
    }

    int brightness = ddoc["brightness"] | configManager.getDisplayBrightness();
    if (brightness < 5) {
        brightness = 5;
    }
    if (brightness > 100) {
        brightness = 100;
    }

    int nightBrightness = ddoc["night_brightness"] | configManager.getDisplayNightBrightness();
    if (nightBrightness < 5) {
        nightBrightness = 5;
    }
    if (nightBrightness > 100) {
        nightBrightness = 100;
    }

    int nightStartHour = ddoc["night_start_hour"] | configManager.getDisplayNightStartHour();
    if (nightStartHour < 0) {
        nightStartHour = 0;
    }
    if (nightStartHour > 23) {
        nightStartHour = 23;
    }

    int nightEndHour = ddoc["night_end_hour"] | configManager.getDisplayNightEndHour();
    if (nightEndHour < 0) {
        nightEndHour = 0;
    }
    if (nightEndHour > 23) {
        nightEndHour = 23;
    }

    configManager.setDisplayBrightness(static_cast<uint8_t>(brightness));
    configManager.setDisplayNightBrightnessEnabled(ddoc["night_enabled"] | configManager.isDisplayNightBrightnessEnabled());
    configManager.setDisplayNightBrightness(static_cast<uint8_t>(nightBrightness));
    configManager.setDisplayNightStartHour(static_cast<uint8_t>(nightStartHour));
    configManager.setDisplayNightEndHour(static_cast<uint8_t>(nightEndHour));
    DisplayManager::applyConfiguredBrightness();

    if (!configManager.save()) {
        JsonDocument doc;
        doc["status"] = "error";
        doc["message"] = "Failed to save config";

        String json;
        serializeJson(doc, json);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_INTERNAL_ERROR, "application/json", json);

        return;
    }

    JsonDocument doc;
    doc["status"] = "ok";
    doc["brightness"] = configManager.getDisplayBrightness();
    doc["night_enabled"] = configManager.isDisplayNightBrightnessEnabled();
    doc["night_brightness"] = configManager.getDisplayNightBrightness();
    doc["night_start_hour"] = configManager.getDisplayNightStartHour();
    doc["night_end_hour"] = configManager.getDisplayNightEndHour();

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);

    Logger::info(("Display brightness updated to " + String(configManager.getDisplayBrightness())).c_str(), "API");
}

static auto kmaValidateDateTime(String& baseDate, String& baseTime) -> bool {
    time_t now = time(nullptr);
    if (now < 1700000000L) {
        return false;
    }

    now += 9L * 60L * 60L;
    tm* timeInfo = gmtime(&now);
    if (timeInfo == nullptr) {
        return false;
    }

    tm base = *timeInfo;
    if (base.tm_min < 40) {
        now -= 60L * 60L;
        timeInfo = gmtime(&now);
        if (timeInfo == nullptr) {
            return false;
        }
        base = *timeInfo;
    }

    char dateBuffer[40] = {};
    char timeBuffer[16] = {};
    snprintf(dateBuffer, sizeof(dateBuffer), "%04d%02d%02d", base.tm_year + 1900, base.tm_mon + 1, base.tm_mday);
    snprintf(timeBuffer, sizeof(timeBuffer), "%02d00", base.tm_hour);
    baseDate = dateBuffer;
    baseTime = timeBuffer;
    return true;
}

static auto readKmaValidateLine(WiFiClient& client, unsigned long timeoutMs = 1800UL) -> String {
    String line;
    line.reserve(96);
    const unsigned long startedMs = millis();
    while (millis() - startedMs < timeoutMs && (client.connected() || client.available())) {
        while (client.available()) {
            const char c = static_cast<char>(client.read());
            if (c == '\n') {
                line.trim();
                return line;
            }
            if (line.length() < 120) {
                line += c;
            }
        }
        delay(1);
        yield();
    }
    line.trim();
    return line;
}

static void skipKmaValidateHeaders(WiFiClient& client) {
    const unsigned long startedMs = millis();
    String line;
    line.reserve(64);
    while (millis() - startedMs < 1800UL && (client.connected() || client.available())) {
        if (!client.available()) {
            delay(1);
            yield();
            continue;
        }
        const char c = static_cast<char>(client.read());
        if (c != '\n') {
            if (line.length() < 96) {
                line += c;
            }
            continue;
        }
        line.trim();
        if (line.length() == 0) {
            return;
        }
        line = "";
    }
}

static auto readKmaValidateBody(WiFiClient& client) -> String {
    String body;
    body.reserve(512);
    const unsigned long startedMs = millis();
    while (millis() - startedMs < 3000UL && (client.connected() || client.available())) {
        while (client.available()) {
            const char c = static_cast<char>(client.read());
            if (body.length() < 900) {
                body += c;
            }
        }
        delay(1);
        yield();
    }
    return body;
}

static auto extractKmaJsonString(const String& body, const char* key) -> String {
    const String pattern = String("\"") + key + "\":\"";
    const int start = body.indexOf(pattern);
    if (start < 0) {
        return "";
    }
    const int valueStart = start + pattern.length();
    const int end = body.indexOf('"', valueStart);
    if (end < 0) {
        return "";
    }
    return body.substring(valueStart, end);
}

void handleWeatherConfigGet(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    JsonDocument doc;
    doc["enabled"] = configManager.isWeatherEnabled();
    doc["latitude"] = configManager.getWeatherLatitude();
    doc["longitude"] = configManager.getWeatherLongitude();
    doc["kma_grid_x"] = configManager.getWeatherKmaGridX();
    doc["kma_grid_y"] = configManager.getWeatherKmaGridY();
    doc["timezone"] = configManager.getWeatherTimezone();
    doc["location_name"] = configManager.getWeatherLocationName();
    doc["kma_api_key"] = configManager.getWeatherKmaApiKey();
    doc["kma_api_key_set"] = strlen(configManager.getWeatherKmaApiKey()) > 0;

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);
}

void handleWeatherConfigSet(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    if (!webserver->raw().hasArg("plain") || webserver->raw().arg("plain").length() == 0) {
        JsonDocument doc;
        doc["status"] = "error";
        doc["message"] = "Missing JSON body";

        String json;
        serializeJson(doc, json);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_BAD_REQUEST, "application/json", json);
        return;
    }

    String body = webserver->raw().arg("plain");
    JsonDocument ddoc;
    DeserializationError err = deserializeJson(ddoc, body);

    if (err) {
        JsonDocument doc;
        doc["status"] = "error";
        doc["message"] = "Invalid JSON";

        String json;
        serializeJson(doc, json);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_BAD_REQUEST, "application/json", json);
        return;
    }

    const bool enabled = ddoc["enabled"] | configManager.isWeatherEnabled();
    const float latitude = ddoc["latitude"] | configManager.getWeatherLatitude();
    const float longitude = ddoc["longitude"] | configManager.getWeatherLongitude();
    const int kmaGridX = ddoc["kma_grid_x"] | configManager.getWeatherKmaGridX();
    const int kmaGridY = ddoc["kma_grid_y"] | configManager.getWeatherKmaGridY();
    const char* timezone = ddoc["timezone"] | configManager.getWeatherTimezone();
    const char* locationName = ddoc["location_name"] | configManager.getWeatherLocationName();

    if (enabled &&
        (latitude < -90.0F || latitude > 90.0F || longitude < -180.0F || longitude > 180.0F ||
         (latitude == 0.0F && longitude == 0.0F))) {
        JsonDocument doc;
        doc["status"] = "error";
        doc["message"] = "latitude/longitude are required";

        String json;
        serializeJson(doc, json);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_BAD_REQUEST, "application/json", json);
        return;
    }

    configManager.setWeatherEnabled(enabled);
    configManager.setWeatherLatitude(latitude);
    configManager.setWeatherLongitude(longitude);
    configManager.setWeatherKmaGridX(kmaGridX);
    configManager.setWeatherKmaGridY(kmaGridY);
    configManager.setWeatherTimezone(timezone);
    configManager.setWeatherLocationName(locationName);
    if (ddoc["kma_api_key"].is<const char*>()) {
        configManager.setWeatherKmaApiKey(ddoc["kma_api_key"] | "");
    }

    if (!configManager.save()) {
        JsonDocument doc;
        doc["status"] = "error";
        doc["message"] = "Failed to save config";

        String json;
        serializeJson(doc, json);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_INTERNAL_ERROR, "application/json", json);
        return;
    }

    JsonDocument doc;
    doc["status"] = "ok";
    doc["enabled"] = configManager.isWeatherEnabled();
    doc["latitude"] = configManager.getWeatherLatitude();
    doc["longitude"] = configManager.getWeatherLongitude();
    doc["kma_grid_x"] = configManager.getWeatherKmaGridX();
    doc["kma_grid_y"] = configManager.getWeatherKmaGridY();
    doc["timezone"] = configManager.getWeatherTimezone();
    doc["location_name"] = configManager.getWeatherLocationName();
    doc["kma_api_key"] = configManager.getWeatherKmaApiKey();
    doc["kma_api_key_set"] = strlen(configManager.getWeatherKmaApiKey()) > 0;

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);

    if (weatherClient != nullptr && enabled) {
        weatherClient->requestRefresh();
    }

    if (configManager.isClockEnabled() || configManager.isWeatherEnabled()) {
        DisplayManager::pauseClock(250);
        DisplayManager::drawClock();
    }
}

void handleWeatherStatusGet(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }
    setCorsHeaders(webserver);

    if (!webserver->raw().chunkedResponseModeStart(HTTP_CODE_OK, "application/json")) {
        webserver->raw().send(HTTP_CODE_INTERNAL_ERROR, "application/json", "{\"status\":\"error\",\"message\":\"chunked unavailable\"}");
        return;
    }

    if (weatherClient == nullptr) {
        sendJsonContent(webserver, "{\"status\":\"error\",\"message\":\"weather client not initialized\"}");
        webserver->raw().chunkedResponseFinalize();
        return;
    }

    const auto& weather = weatherClient->getSnapshot();
    sendJsonContent(webserver, "{\"status\":");
    sendJsonEscapedString(webserver, weather.status);
    sendJsonContent(webserver, ",\"source\":");
    sendJsonEscapedString(webserver, weather.source);
    sendJsonContent(webserver, ",\"hasData\":");
    sendJsonContent(webserver, weather.hasData ? "true" : "false");
    sendJsonContent(webserver, ",\"fetching\":");
    sendJsonContent(webserver, weather.fetching ? "true" : "false");
    sendJsonContent(webserver, ",\"currentTime\":");
    sendJsonInt(webserver, static_cast<long>(weather.currentTime));
    sendJsonContent(webserver, ",\"lastUpdated\":");
    sendJsonInt(webserver, static_cast<long>(weather.lastUpdated));
    sendJsonContent(webserver, ",\"timezone\":");
    sendJsonEscapedString(webserver, weather.timezone);
    sendJsonContent(webserver, ",\"utcOffsetSeconds\":");
    sendJsonInt(webserver, weather.utcOffsetSeconds);
    sendJsonContent(webserver, ",\"currentTemperature\":");
    sendJsonFloat(webserver, weather.currentTemperature);
    sendJsonContent(webserver, ",\"currentRain\":");
    sendJsonFloat(webserver, weather.currentRain);
    sendJsonContent(webserver, ",\"currentPrecipitation\":");
    sendJsonFloat(webserver, weather.currentPrecipitation);
    sendJsonContent(webserver, ",\"currentPrecipitationProbability\":");
    sendJsonFloat(webserver, weather.currentPrecipitationProbability);
    sendJsonContent(webserver, ",\"currentHumidity\":");
    sendJsonFloat(webserver, weather.currentHumidity);
    sendJsonContent(webserver, ",\"currentCloudCover\":");
    sendJsonFloat(webserver, weather.currentCloudCover);
    sendJsonContent(webserver, ",\"currentVisibility\":");
    sendJsonFloat(webserver, weather.currentVisibility);
    sendJsonContent(webserver, ",\"currentWeatherCode\":");
    sendJsonInt(webserver, weather.currentWeatherCode);
    sendJsonContent(webserver, ",\"isRaining\":");
    sendJsonContent(webserver, weather.isRaining ? "true" : "false");
    sendJsonContent(webserver, ",\"hasAirQuality\":");
    sendJsonContent(webserver, weather.hasAirQuality ? "true" : "false");
    sendJsonContent(webserver, ",\"currentPm25\":");
    sendJsonFloat(webserver, weather.currentPm25);
    sendJsonContent(webserver, ",\"currentOzone\":");
    sendJsonFloat(webserver, weather.currentOzone);
    sendJsonContent(webserver, ",\"currentPm25Aqi\":");
    sendJsonFloat(webserver, weather.currentPm25Aqi);
    sendJsonContent(webserver, ",\"currentOzoneAqi\":");
    sendJsonFloat(webserver, weather.currentOzoneAqi);
    sendJsonContent(webserver, ",\"forecast\":[");

    bool first = true;
    for (const auto& entry : weather.forecast) {
        if (!first) {
            sendJsonContent(webserver, ",");
        }
        first = false;
        sendJsonContent(webserver, "{\"time\":");
        sendJsonInt(webserver, static_cast<long>(entry.timestamp));
        sendJsonContent(webserver, ",\"temperature\":");
        sendJsonFloat(webserver, entry.temperature);
        sendJsonContent(webserver, ",\"rain\":");
        sendJsonFloat(webserver, entry.rain);
        sendJsonContent(webserver, ",\"precipitation\":");
        sendJsonFloat(webserver, entry.precipitation);
        sendJsonContent(webserver, ",\"precipitationProbability\":");
        sendJsonFloat(webserver, entry.precipitationProbability);
        sendJsonContent(webserver, ",\"humidity\":");
        sendJsonFloat(webserver, entry.humidity);
        sendJsonContent(webserver, ",\"weatherCode\":");
        sendJsonInt(webserver, entry.weatherCode);
        sendJsonContent(webserver, "}");
    }

    sendJsonContent(webserver, "]}");
    webserver->raw().chunkedResponseFinalize();
}

void handleWeatherRefresh(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    JsonDocument doc;
    const bool ok = weatherClient != nullptr;
    doc["status"] = ok ? "ok" : "error";

    if (weatherClient != nullptr) {
        weatherClient->requestRefresh();
        const auto weather = weatherClient->getSnapshot();
        doc["message"] = "refresh queued";
        doc["previousStatus"] = weather.status;
    } else {
        doc["message"] = "weather client not initialized";
    }

    String json;
    serializeJson(doc, json);
    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);
}

void handleWeatherValidateKey(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    JsonDocument doc;
    const String apiKey = configManager.getWeatherKmaApiKey();
    if (apiKey.length() == 0) {
        doc["status"] = "error";
        doc["valid"] = false;
        doc["message"] = "KMA APIHub key is empty";

        String json;
        serializeJson(doc, json);
        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_OK, "application/json", json);
        return;
    }

    String baseDate;
    String baseTime;
    if (!kmaValidateDateTime(baseDate, baseTime)) {
        doc["status"] = "error";
        doc["valid"] = false;
        doc["message"] = "time not synced";

        String json;
        serializeJson(doc, json);
        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_OK, "application/json", json);
        return;
    }

    WiFiClient client;
    client.setTimeout(3000);
    if (!client.connect(KMA_VALIDATE_HOST, KMA_VALIDATE_PORT)) {
        doc["status"] = "error";
        doc["valid"] = false;
        doc["message"] = "KMA connect failed";

        String json;
        serializeJson(doc, json);
        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_OK, "application/json", json);
        return;
    }

    const String path = String("/api/typ02/openApi/VilageFcstInfoService_2.0/getUltraSrtNcst") +
                        "?pageNo=1&numOfRows=10&dataType=JSON&base_date=" + baseDate + "&base_time=" + baseTime +
                        "&nx=" + String(KMA_VALIDATE_SEOUL_X) + "&ny=" + String(KMA_VALIDATE_SEOUL_Y) +
                        "&authKey=" + apiKey;
    client.print(F("GET "));
    client.print(path);
    client.print(F(" HTTP/1.0\r\nHost: "));
    client.print(KMA_VALIDATE_HOST);
    client.print(F("\r\nUser-Agent: SmallTVUltraKoreanCustomFirmware/1.0\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n"));

    const String statusLine = readKmaValidateLine(client);
    skipKmaValidateHeaders(client);
    const String body = readKmaValidateBody(client);
    client.stop();

    const String resultCode = extractKmaJsonString(body, "resultCode");
    const String resultMsg = extractKmaJsonString(body, "resultMsg");
    const bool httpOk = statusLine.startsWith("HTTP/1.1 200") || statusLine.startsWith("HTTP/1.0 200");
    const bool valid = httpOk && resultCode == "00";

    doc["status"] = valid ? "ok" : "error";
    doc["valid"] = valid;
    doc["message"] = valid ? "KMA APIHub key is valid" : "KMA APIHub key validation failed";
    doc["region"] = "서울시";
    doc["nx"] = KMA_VALIDATE_SEOUL_X;
    doc["ny"] = KMA_VALIDATE_SEOUL_Y;
    doc["base_date"] = baseDate;
    doc["base_time"] = baseTime;
    doc["http_status"] = statusLine;
    doc["result_code"] = resultCode;
    doc["result_msg"] = resultMsg;

    String json;
    serializeJson(doc, json);
    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);
}

void handleDisplayClockGet(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    JsonDocument doc;
    doc["enabled"] = configManager.isClockEnabled();
    doc["showSeconds"] = false;
    doc["use24h"] = configManager.isClockUse24Hour();

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);
}

void handleDisplayClockSet(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    if (!webserver->raw().hasArg("plain") || webserver->raw().arg("plain").length() == 0) {
        JsonDocument doc;
        doc["status"] = "error";
        doc["message"] = "Missing JSON body";

        String json;
        serializeJson(doc, json);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_BAD_REQUEST, "application/json", json);

        return;
    }

    String body = webserver->raw().arg("plain");
    JsonDocument ddoc;
    DeserializationError err = deserializeJson(ddoc, body);

    if (err) {
        JsonDocument doc;
        doc["status"] = "error";
        doc["message"] = "Invalid JSON";

        String json;
        serializeJson(doc, json);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_BAD_REQUEST, "application/json", json);

        return;
    }

    configManager.setClockEnabled(ddoc["enabled"] | configManager.isClockEnabled());
    configManager.setClockUse24Hour(ddoc["use24h"] | configManager.isClockUse24Hour());

    if (!configManager.save()) {
        JsonDocument doc;
        doc["status"] = "error";
        doc["message"] = "Failed to save config";

        String json;
        serializeJson(doc, json);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_INTERNAL_ERROR, "application/json", json);

        return;
    }

    JsonDocument doc;
    doc["status"] = "ok";
    doc["enabled"] = configManager.isClockEnabled();
    doc["showSeconds"] = false;
    doc["use24h"] = configManager.isClockUse24Hour();

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);

    if (configManager.isClockEnabled()) {
        DisplayManager::pauseClock(250);
        DisplayManager::drawClock();
    } else {
        const String currentIP = (wifiManager != nullptr) ? wifiManager->getIP().toString() : String("N/A");
        DisplayManager::pauseClock(5000);
        DisplayManager::drawStartup(currentIP);
    }

    Logger::info("Display clock settings updated", "API");
}

void handleDisplayStartup(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    const String currentIP = (wifiManager != nullptr) ? wifiManager->getIP().toString() : String("N/A");
    DisplayManager::pauseClock(5000);
    DisplayManager::drawStartup(currentIP);

    JsonDocument doc;
    doc["status"] = "ok";
    doc["message"] = "startup screen redrawn";

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);
}

/**
 * @brief Handle OTA upload
 * @param webserver Pointer to the Webserver instance
 * @param mode Update mode U_FLASH U_FS
 *
 * @return void
 */
void handleOtaUpload(Webserver* webserver, int mode) {
    HTTPUpload& upload = webserver->raw().upload();

    if (upload.status == UPLOAD_FILE_START && !validateBearerToken(webserver)) {
        otaError = true;
        otaStatus = "Unauthorized";

        JsonDocument doc;
        doc["status"] = "error";
        doc["message"] = "Invalid or missing token";

        String json;

        serializeJson(doc, json);
        setCorsHeaders(webserver);

        webserver->raw().send(HTTP_CODE_UNAUTHORIZED, "application/json", json);

        return;
    }

    switch (upload.status) {
        case UPLOAD_FILE_START:
            otaHandleStart(upload, mode);
            break;
        case UPLOAD_FILE_WRITE:
            otaHandleWrite(upload);
            break;
        case UPLOAD_FILE_END:
            otaHandleEnd(upload, mode);
            break;
        case UPLOAD_FILE_ABORTED:
            otaHandleAborted(upload);
            break;
        default:
            break;
    }
}

/**
 * @brief Handle OTA finished
 * @param webserver Pointer to the Webserver instance
 *
 * @return void
 */
void handleOtaFinished(Webserver* webserver) {
    if (!validateBearerToken(webserver)) {
        JsonDocument doc;
        doc["status"] = "error";
        doc["message"] = "Invalid or missing token";

        String json;
        serializeJson(doc, json);
        setCorsHeaders(webserver);

        webserver->raw().send(HTTP_CODE_UNAUTHORIZED, "application/json", json);

        return;
    }

    JsonDocument doc;
    int constexpr rebootDelayMs = 5000;

    doc["status"] = "Upload successful";
    doc["message"] = otaStatus;

    if (otaError) {
        doc["status"] = "Error";
    }

    otaInProgress = false;
    otaCancelRequested = false;

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);

    if (!otaError) {
        if (otaMode == U_FS) {
            configManager.save();
        }
        delay(rebootDelayMs);
        ESP.restart();  // NOLINT(readability-static-accessed-through-instance)
    }
}

/**
 * @brief Play a GIF from LittleFS full screen
 *
 * @param webserver Pointer to the Webserver instance
 *
 * @return void
 */
void handlePlayGif(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    String body = webserver->raw().arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        JsonDocument resp;

        resp["status"] = "error";
        resp["message"] = "invalid json";

        String jsonOut;

        serializeJson(resp, jsonOut);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_INTERNAL_ERROR, "application/json", jsonOut);

        return;
    }

    const char* name = doc["name"];
    if (name == nullptr || strlen(name) == 0) {
        JsonDocument resp;
        resp["status"] = "error";
        resp["message"] = "missing name";

        String jsonOut;
        serializeJson(resp, jsonOut);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_INTERNAL_ERROR, "application/json", jsonOut);

        return;
    }

    String filename(name);
    filename.replace("\\", "/");
    filename = filename.substring(filename.lastIndexOf('/') + 1);

    String path1 = String("/gifs/") + filename;
    String path2 = String("/gif/") + filename;
    String foundPath;

    if (LittleFS.exists(path1)) {
        foundPath = path1;
    } else if (LittleFS.exists(path2)) {
        foundPath = path2;
    } else {
        JsonDocument resp;

        resp["status"] = "error";
        resp["message"] = "file not found";

        String jsonOut;
        serializeJson(resp, jsonOut);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_NOT_FOUND, "application/json", jsonOut);

        return;
    }

    bool playOk = DisplayManager::playGifFullScreen(foundPath);

    JsonDocument resp;

    resp["status"] = playOk ? "playing" : "error";
    resp["file"] = foundPath;

    String jsonOut;

    serializeJson(resp, jsonOut);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", jsonOut);
}

/**
 * @brief Stop currently playing GIF
 */
void handleStopGif(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    JsonDocument resp;

    const bool stopped = DisplayManager::stopGif();

    resp["status"] = stopped ? "stopped" : "error";

    String jsonOut;
    serializeJson(resp, jsonOut);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", jsonOut);
}

/**
 * @brief Delete a GIF file from storage
 */
void handleDeleteGif(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    String body = webserver->raw().arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        JsonDocument resp;
        resp["status"] = "error";
        resp["message"] = "invalid json";

        String jsonOut;

        serializeJson(resp, jsonOut);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_INTERNAL_ERROR, "application/json", jsonOut);

        return;
    }

    const char* name = doc["name"];
    if (name == nullptr || strlen(name) == 0) {
        JsonDocument resp;
        resp["status"] = "error";
        resp["message"] = "missing name";

        String jsonOut;

        serializeJson(resp, jsonOut);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_INTERNAL_ERROR, "application/json", jsonOut);

        return;
    }

    String filename(name);
    filename.replace("\\", "/");
    filename = filename.substring(filename.lastIndexOf('/') + 1);
    String path = String("/gif/") + filename;

    if (!LittleFS.exists(path)) {
        JsonDocument resp;
        resp["status"] = "error";
        resp["message"] = "file not found";

        String jsonOut;

        serializeJson(resp, jsonOut);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_NOT_FOUND, "application/json", jsonOut);

        return;
    }

    if (LittleFS.remove(path)) {
        JsonDocument resp;
        resp["status"] = "success";
        resp["message"] = "file removed";
        resp["file"] = path;

        String jsonOut;
        serializeJson(resp, jsonOut);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_OK, "application/json", jsonOut);

        Logger::info((String("Removed file: ") + path).c_str(), "API::GIF");
    } else {
        JsonDocument resp;
        resp["status"] = "error";
        resp["message"] = "failed to remove file";

        String jsonOut;
        serializeJson(resp, jsonOut);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_INTERNAL_ERROR, "application/json", jsonOut);

        Logger::error((String("Failed to remove file: ") + path).c_str(), "API::GIF");
    }
}

/**
 * @brief Handle WiFi scan
 */
void handleWifiScan(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();

    if (wifiManager != nullptr) {
        WiFiManager::scanNetworks(networks);
    }

    String out;
    serializeJson(doc["networks"], out);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", out);
}

/**
 * @brief Handle WiFi connect request
 */
void handleWifiConnect(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    String body = webserver->raw().arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        JsonDocument resp;

        resp["status"] = "error";
        resp["message"] = "invalid json";

        String jsonOut;
        serializeJson(resp, jsonOut);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_INTERNAL_ERROR, "application/json", jsonOut);

        return;
    }

    const char* ssid = doc["ssid"] | "";
    const char* password = doc["password"] | "";

    if (strlen(ssid) == 0) {
        JsonDocument resp;

        resp["status"] = "error";
        resp["message"] = "missing ssid";

        String jsonOut;

        serializeJson(resp, jsonOut);

        setCorsHeaders(webserver);
        webserver->raw().send(HTTP_CODE_INTERNAL_ERROR, "application/json", jsonOut);

        return;
    }

    bool connectOk = false;
    if (wifiManager != nullptr) {
        connectOk = wifiManager->connectToNetwork(ssid, password, WIFI_CONNECT_TIMEOUT_MS);
    }

    JsonDocument resp;

    resp["status"] = connectOk ? "connected" : "error";
    resp["ssid"] = ssid;

    if (connectOk) {
        resp["ip"] = wifiManager->getIP().toString();
        configManager.setWiFi(ssid, password);
        configManager.save();
    }

    if (!connectOk) {
        resp["message"] = "failed to connect";
    }

    String jsonOut;
    serializeJson(resp, jsonOut);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", jsonOut);
}

/**
 * @brief WiFi status
 */
void handleWifiStatus(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    JsonDocument resp;

    bool connected = (wifiManager != nullptr) && WiFiManager::isConnected();

    resp["connected"] = connected;
    resp["ssid"] = connected ? WiFiManager::getConnectedSSID() : "";
    resp["ip"] = connected ? wifiManager->getIP().toString() : "";

    String jsonOut;
    serializeJson(resp, jsonOut);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", jsonOut);
}

/**
 * @brief Handle OTA start
 *
 * @param upload Reference to the HTTPUpload object
 * @param mode Update mode U_FLASH or U_FS
 *
 * @return void
 */
static void otaHandleStart(HTTPUpload& upload, int mode) {
    Logger::info((String("OTA start: ") + upload.filename).c_str(), "API::OTA");

    otaError = false;
    otaSize = 0;
    otaStatus = "";
    otaInProgress = true;
    otaCancelRequested = false;
    otaMode = mode;
    otaTotal = static_cast<size_t>(upload.contentLength);
    otaLastProgressDrawMs = 0;
    otaLastProgressPercent = -1;

    DisplayManager::pauseClock(60000);
    DisplayManager::clearScreen();
    DisplayManager::drawTextWrapped(OTA_TEXT_X_OFFSET, OTA_TEXT_Y_OFFSET, F("업로드 중..."), 2, LCD_WHITE,
                                    LCD_BLACK, true);
    DisplayManager::drawLoadingBar(0.0F, OTA_LOADING_Y_OFFSET);

    int constexpr security_space = 0x1000;
    u_int constexpr bin_mask = 0xFFFFF000;

    FSInfo fs_info;
    LittleFS.info(fs_info);
    size_t fsSize = fs_info.totalBytes;
    size_t maxSketchSpace =
        (ESP.getFreeSketchSpace() - security_space) &  // NOLINT(readability-static-accessed-through-instance)
        bin_mask;
    size_t place = (mode == U_FS) ? fsSize : maxSketchSpace;

    otaFsUnmounted = false;
    if (mode == U_FS) {
        LittleFS.end();
        otaFsUnmounted = true;
        delay(50);
    }

    if (!Update.begin(place, mode)) {
        otaError = true;
        otaStatus = Update.getErrorString();
        Logger::error((String("Update.begin failed: ") + otaStatus).c_str(), "API::OTA");
        if (otaFsUnmounted) {
            LittleFS.begin();
            otaFsUnmounted = false;
        }
    }
}

/**
 * @brief Handle OTA write
 *
 * @param upload Reference to the HTTPUpload object
 *
 * @return void
 */
static void otaHandleWrite(HTTPUpload& upload) {
    if (!otaError) {
        if (otaCancelRequested) {
            Update.end();
            otaError = true;
            otaStatus = "Update canceled";
            otaInProgress = false;
            Logger::warn("OTA canceled by user", "API::OTA");

            DisplayManager::drawTextWrapped(OTA_TEXT_X_OFFSET, OTA_TEXT_Y_OFFSET, F("취소됨"), 2, LCD_WHITE,
                                            LCD_BLACK, true);
            DisplayManager::drawLoadingBar(0.0F, OTA_LOADING_Y_OFFSET);

            return;
        }

        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            otaError = true;
            otaStatus = Update.getErrorString();
            Logger::error((String("Write failed: ") + otaStatus).c_str(), "API::OTA");
        }

        otaSize += upload.currentSize;

        float progress = 0.0F;
        if (otaTotal > 0) {
            progress = static_cast<float>(otaSize) / static_cast<float>(otaTotal);
        }
        const int percent = static_cast<int>(progress * 100.0F);
        const unsigned long nowMs = millis();
        const bool shouldRedraw =
            otaLastProgressPercent < 0 || percent >= 100 || percent != otaLastProgressPercent ||
            (nowMs - otaLastProgressDrawMs) >= 250UL;
        if (shouldRedraw) {
            DisplayManager::drawLoadingBar(progress, OTA_LOADING_Y_OFFSET);
            otaLastProgressDrawMs = nowMs;
            otaLastProgressPercent = percent;
        }

        delay(0);
    }
}

/**
 * @brief Handle OTA end
 *
 * @param upload Reference to the HTTPUpload object
 * @param mode Update mode U_FLASH or U_FS
 *
 * @return void
 */
static void otaHandleEnd(HTTPUpload& /*upload*/, int mode) {
    if (!otaError) {
        if (Update.end(true)) {
            if (mode == U_FS) {
                Logger::info("OTA FS update complete, mounting file system...", "API::OTA");
                LittleFS.begin();
                otaFsUnmounted = false;
            }

            otaStatus = String("Update OK (") + String(otaSize) + " bytes)";
            Logger::info(otaStatus.c_str(), "API::OTA");

            DisplayManager::drawLoadingBar(1.0F, OTA_LOADING_Y_OFFSET);
            DisplayManager::drawTextWrapped(OTA_TEXT_X_OFFSET, OTA_TEXT_Y_OFFSET, F("완료!"), 2, LCD_WHITE,
                                            LCD_BLACK, true);
        } else {
            otaError = true;
            otaStatus = Update.getErrorString();
            if (mode == U_FS && otaFsUnmounted) {
                LittleFS.begin();
                otaFsUnmounted = false;
            }
        }
    }

    if (mode == U_FS && otaError && otaFsUnmounted) {
        LittleFS.begin();
        otaFsUnmounted = false;
    }
}

/**
 * @brief Handle OTA aborted
 *
 * @param upload Reference to the HTTPUpload object
 *
 * @return void
 */
static void otaHandleAborted(HTTPUpload& /*upload*/) {
    Update.end();
    otaError = true;
    otaStatus = "Update aborted";
    otaInProgress = false;
    otaCancelRequested = false;
    if (otaFsUnmounted) {
        LittleFS.begin();
        otaFsUnmounted = false;
    }

    DisplayManager::drawTextWrapped(OTA_TEXT_X_OFFSET, OTA_TEXT_Y_OFFSET, F("중단됨"), 2, LCD_WHITE, LCD_BLACK, true);
    DisplayManager::drawLoadingBar(0.0F, OTA_LOADING_Y_OFFSET);
}

/**
 * @brief Get recent logs
 * @param webserver Pointer to the Webserver instance
 */
void handleLogsGet(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    JsonDocument doc;
    JsonArray logsArray = doc["logs"].to<JsonArray>();

    size_t count = Logger::getLogCount();
    for (size_t i = 0; i < count; i++) {
        const char* entry = Logger::getLogEntry(i);
        if (entry != nullptr) {
            logsArray.add(entry);
        }
    }

    doc["count"] = count;

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);
}

/**
 * @brief Download logs as a text file
 * @param webserver Pointer to the Webserver instance
 */
void handleLogsDownload(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    String logs = Logger::getLogsAsString();

    setCorsHeaders(webserver);
    webserver->raw().sendHeader("Content-Disposition", "attachment; filename=\"logs.log\"");
    webserver->raw().send(HTTP_CODE_OK, "text/plain", logs);
}

/**
 * @brief Clear log buffer
 * @param webserver Pointer to the Webserver instance
 */
void handleLogsClear(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }

    Logger::clearLogs();

    JsonDocument doc;
    doc["status"] = "ok";
    doc["message"] = "Logs cleared";

    String json;
    serializeJson(doc, json);

    setCorsHeaders(webserver);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);
}
