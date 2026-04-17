// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * GeekMagic Open Firmware
 * Copyright (C) 2026 Times-Z
 */

#include "weather/WeatherClient.h"

#include <ArduinoJson.h>
#include <Logger.h>
#include <WiFiClient.h>
#include <array>

#include "config/ConfigManager.h"
#include "wireless/WiFiManager.h"

extern ConfigManager configManager;

static constexpr const char* TAG = "WeatherClient";
static constexpr unsigned long WEATHER_REFRESH_INTERVAL_MS = 15UL * 60UL * 1000UL;
static constexpr unsigned long WEATHER_INITIAL_RETRY_MS = 30UL * 1000UL;
static constexpr size_t WEATHER_FORECAST_COUNT = 4;
static constexpr size_t WEATHER_FORECAST_FETCH_HOURS = 8;
static constexpr const char* WEATHER_HOST = "api.open-meteo.com";
static constexpr uint16_t WEATHER_PORT = 80;
static constexpr const char* AIR_QUALITY_HOST = "air-quality-api.open-meteo.com";
static constexpr uint16_t AIR_QUALITY_PORT = 80;

static void resetAirQualitySnapshot(WeatherClient::Snapshot& snapshot) {
    snapshot.hasAirQuality = false;
    snapshot.currentPm25 = 0.0F;
    snapshot.currentOzone = 0.0F;
    snapshot.currentPm25Aqi = 0.0F;
    snapshot.currentOzoneAqi = 0.0F;
}

static auto readHttpStatusLine(WiFiClient& client) -> String {
    String statusLine = client.readStringUntil('\n');
    statusLine.trim();
    return statusLine;
}

static bool skipHttpHeaders(WiFiClient& client) {
    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line.length() == 0) {
            return true;
        }
    }

    return false;
}

static auto urlEncode(String value) -> String {
    String encoded;
    encoded.reserve(value.length() * 3);
    static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";

    for (size_t i = 0; i < value.length(); ++i) {
        const uint8_t c = static_cast<uint8_t>(value.charAt(i));
        const bool isSafe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                            c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
        if (isSafe) {
            encoded += static_cast<char>(c);
            continue;
        }

        encoded += '%';
        encoded += HEX_DIGITS[(c >> 4) & 0x0F];
        encoded += HEX_DIGITS[c & 0x0F];
    }

    return encoded;
}

static auto buildAirQualityUrl(float latitude, float longitude, const String& timezone) -> String {
    String path = F("/v1/air-quality?current=pm2_5,ozone,european_aqi_pm2_5,european_aqi_ozone");
    path += F("&timezone=");
    path += urlEncode(timezone);
    path += F("&latitude=");
    path += String(latitude, 6);
    path += F("&longitude=");
    path += String(longitude, 6);
    return path;
}

WeatherClient::WeatherClient() = default;

void WeatherClient::begin() {
    _snapshot = Snapshot{};
    _snapshot.status = "weather idle";
    _nextRefreshMs = millis();
}

void WeatherClient::loop() {
    if (!configManager.isWeatherEnabled()) {
        return;
    }

    if (!WiFiManager::isConnected()) {
        _snapshot.fetching = false;
        _snapshot.status = "network unavailable";
        return;
    }

    const unsigned long nowMs = millis();
    if (static_cast<long>(nowMs - _nextRefreshMs) >= 0) {
        fetchForecast();
    }
}

bool WeatherClient::refreshNow() { return fetchForecast(); }

auto WeatherClient::getSnapshot() const -> const Snapshot& { return _snapshot; }

auto WeatherClient::buildForecastUrl(float latitude, float longitude, const String& timezone) -> String {
    String path = F("/v1/forecast?timeformat=unixtime");
    path += F("&timezone=");
    path += urlEncode(timezone);
    path += F("&current=temperature_2m,rain,precipitation,precipitation_probability,weather_code,is_day,cloud_cover,visibility");
    path += F("&hourly=temperature_2m,rain,precipitation,precipitation_probability,weather_code");
    path += F("&forecast_hours=");
    path += String(static_cast<int>(WEATHER_FORECAST_FETCH_HOURS));
    path += F("&latitude=");
    path += String(latitude, 6);
    path += F("&longitude=");
    path += String(longitude, 6);
    return path;
}

bool WeatherClient::fetchForecast() {
    const bool hadAirQuality = _snapshot.hasAirQuality;
    const float previousPm25 = _snapshot.currentPm25;
    const float previousOzone = _snapshot.currentOzone;
    const float previousPm25Aqi = _snapshot.currentPm25Aqi;
    const float previousOzoneAqi = _snapshot.currentOzoneAqi;
    const float latitude = configManager.getWeatherLatitude();
    const float longitude = configManager.getWeatherLongitude();
    const String timezone = configManager.getWeatherTimezone();

    if (latitude < -90.0F || latitude > 90.0F || longitude < -180.0F || longitude > 180.0F ||
        (latitude == 0.0F && longitude == 0.0F)) {
        _snapshot.status = "invalid weather coordinates";
        _snapshot.fetching = false;
        _nextRefreshMs = millis() + WEATHER_INITIAL_RETRY_MS;
        return false;
    }

    WiFiClient client;
    const String path = buildForecastUrl(latitude, longitude, timezone);
    _snapshot.fetching = true;

    if (!client.connect(WEATHER_HOST, WEATHER_PORT)) {
        _snapshot.fetching = false;
        _snapshot.status = "weather connect failed";
        _nextRefreshMs = millis() + WEATHER_INITIAL_RETRY_MS;
        Logger::error("Failed to connect to weather host", TAG);
        return false;
    }

    client.setTimeout(10000);
    client.print(String("GET ") + path + " HTTP/1.0\r\nHost: " + WEATHER_HOST +
                 "\r\nUser-Agent: GeekMagicFirmware/1.0\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n");

    String statusLine = readHttpStatusLine(client);
    if (!statusLine.startsWith("HTTP/1.1 200") && !statusLine.startsWith("HTTP/1.0 200")) {
        _snapshot.fetching = false;
        _snapshot.status = "weather http error";
        _nextRefreshMs = millis() + WEATHER_INITIAL_RETRY_MS;
        Logger::error(statusLine.c_str(), TAG);
        client.stop();
        return false;
    }

    skipHttpHeaders(client);

    String payload;
    while (client.available() || client.connected()) {
        String chunk = client.readString();
        if (chunk.length() == 0) {
            delay(10);
            continue;
        }
        payload += chunk;
    }
    client.stop();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);

    if (err) {
        _snapshot.fetching = false;
        _snapshot.status = "weather json parse failed";
        _nextRefreshMs = millis() + WEATHER_INITIAL_RETRY_MS;
        Logger::error("Failed to parse weather JSON", TAG);
        return false;
    }

    JsonObject current = doc["current"];
    JsonObject hourly = doc["hourly"];
    const long utcOffsetSeconds = doc["utc_offset_seconds"] | 0L;
    const String responseTimezone = doc["timezone"] | timezone;

    _snapshot.currentTime = static_cast<time_t>((current["time"] | 0L) + utcOffsetSeconds);
    _snapshot.currentTemperature = current["temperature_2m"] | 0.0F;
    _snapshot.currentRain = current["rain"] | 0.0F;
    _snapshot.currentPrecipitation = current["precipitation"] | 0.0F;
    _snapshot.currentPrecipitationProbability = current["precipitation_probability"] | -1.0F;
    _snapshot.currentCloudCover = current["cloud_cover"] | 0.0F;
    _snapshot.currentVisibility = current["visibility"] | 0.0F;
    _snapshot.currentWeatherCode = current["weather_code"] | -1;
    _snapshot.isRaining = (_snapshot.currentRain > 0.0F) || (_snapshot.currentPrecipitation > 0.0F);
    _snapshot.utcOffsetSeconds = utcOffsetSeconds;
    _snapshot.timezone = responseTimezone;
    _snapshot.hasData = true;
    _snapshot.fetching = false;
    _snapshot.lastUpdated = time(nullptr);
    _snapshot.status = "ok";

    JsonArray times = hourly["time"];
    JsonArray temperatures = hourly["temperature_2m"];
    JsonArray rainValues = hourly["rain"];
    JsonArray precipitationValues = hourly["precipitation"];
    JsonArray precipitationProbabilityValues = hourly["precipitation_probability"];
    JsonArray weatherCodes = hourly["weather_code"];

    for (auto& entry : _snapshot.forecast) {
        entry = ForecastEntry{};
    }

    size_t forecastIndex = 0;
    const size_t availableCount = std::min({times.size(), temperatures.size(), rainValues.size(), precipitationValues.size(),
                                            weatherCodes.size()});
    for (size_t i = 0; i < availableCount && forecastIndex < WEATHER_FORECAST_COUNT; ++i) {
        ForecastEntry entry;
        entry.timestamp = static_cast<time_t>((times[i] | 0L) + utcOffsetSeconds);
        if (entry.timestamp <= _snapshot.currentTime) {
            continue;
        }

        entry.temperature = temperatures[i] | 0.0F;
        entry.rain = rainValues[i] | 0.0F;
        entry.precipitation = precipitationValues[i] | 0.0F;
        if (i < precipitationProbabilityValues.size()) {
            entry.precipitationProbability = precipitationProbabilityValues[i] | -1.0F;
        }
        entry.weatherCode = weatherCodes[i] | -1;
        _snapshot.forecast[forecastIndex++] = entry;
    }

    resetAirQualitySnapshot(_snapshot);
    bool retryAirQualitySoon = false;

    WiFiClient airClient;
    if (airClient.connect(AIR_QUALITY_HOST, AIR_QUALITY_PORT)) {
        airClient.setTimeout(10000);
        const String airPath = buildAirQualityUrl(latitude, longitude, timezone);
        airClient.print(String("GET ") + airPath + " HTTP/1.0\r\nHost: " + AIR_QUALITY_HOST +
                        "\r\nUser-Agent: GeekMagicFirmware/1.0\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n");

        String airStatusLine = readHttpStatusLine(airClient);
        if (airStatusLine.startsWith("HTTP/1.1 200") || airStatusLine.startsWith("HTTP/1.0 200")) {
            skipHttpHeaders(airClient);

            JsonDocument airFilter;
            airFilter["current"]["pm2_5"] = true;
            airFilter["current"]["ozone"] = true;
            airFilter["current"]["european_aqi_pm2_5"] = true;
            airFilter["current"]["european_aqi_ozone"] = true;

            JsonDocument airDoc;
            const DeserializationError airErr =
                deserializeJson(airDoc, airClient, DeserializationOption::Filter(airFilter));
            if (!airErr) {
                JsonObject airCurrent = airDoc["current"];
                const float pm25 = airCurrent["pm2_5"] | 0.0F;
                const float ozone = airCurrent["ozone"] | 0.0F;
                const float pm25Aqi = airCurrent["european_aqi_pm2_5"] | 0.0F;
                const float ozoneAqi = airCurrent["european_aqi_ozone"] | 0.0F;

                if (pm25 <= 0.0F && ozone <= 0.0F) {
                    retryAirQualitySoon = true;
                    Logger::warn("Air quality API returned 0/0 values; keeping previous AQ snapshot", TAG);
                } else {
                    _snapshot.currentPm25 = pm25;
                    _snapshot.currentOzone = ozone;
                    _snapshot.currentPm25Aqi = pm25Aqi;
                    _snapshot.currentOzoneAqi = ozoneAqi;
                    _snapshot.hasAirQuality = true;
                }
            } else {
                const String warningMessage = String("Failed to parse air quality JSON: ") + airErr.c_str();
                Logger::warn(warningMessage.c_str(), TAG);
            }
        } else {
            const String warningMessage = String("Air quality HTTP error: ") + airStatusLine;
            Logger::warn(warningMessage.c_str(), TAG);
        }
        airClient.stop();
    } else {
        Logger::warn("Failed to connect to air quality host", TAG);
    }

    if (!_snapshot.hasAirQuality && hadAirQuality) {
        _snapshot.currentPm25 = previousPm25;
        _snapshot.currentOzone = previousOzone;
        _snapshot.currentPm25Aqi = previousPm25Aqi;
        _snapshot.currentOzoneAqi = previousOzoneAqi;
        _snapshot.hasAirQuality = true;
    }

    _nextRefreshMs = millis() + (retryAirQualitySoon ? WEATHER_INITIAL_RETRY_MS : WEATHER_REFRESH_INTERVAL_MS);
    Logger::info("Weather updated", TAG);

    return true;
}
