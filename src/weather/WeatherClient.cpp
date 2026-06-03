// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * SmallTV-Ultra Korean Custom Firmware
 * Copyright (C) 2026 Times-Z
 */

#include "weather/WeatherClient.h"

#include <Client.h>
#include <Logger.h>
#include <WiFiClient.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "config/ConfigManager.h"
#include "display/DisplayManager.h"
#include "web/Webserver.h"
#include "wireless/WiFiManager.h"

extern ConfigManager configManager;
extern Webserver* webserver;

static constexpr const char* TAG = "WeatherClient";
static constexpr double LOCAL_PI = 3.14159265358979323846;
static constexpr unsigned long WEATHER_REFRESH_INTERVAL_MS = 15UL * 60UL * 1000UL;
static constexpr unsigned long WEATHER_INITIAL_RETRY_MS = 30UL * 1000UL;
static constexpr unsigned long WEATHER_STALE_RETRY_MS = 5UL * 60UL * 1000UL;
static constexpr unsigned long WEATHER_TIME_SYNC_RETRY_MS = 5UL * 1000UL;
static constexpr unsigned long KMA_STABLE_REFRESH_MINUTE = 50UL;
static constexpr size_t WEATHER_FORECAST_COUNT = 4;
static constexpr const char* KMA_API_HOST = "apihub.kma.go.kr";
static constexpr uint16_t KMA_API_PORT = 80;
static constexpr unsigned long DISPLAY_PUMP_INTERVAL_MS = 100UL;
static constexpr unsigned long KMA_DNS_CACHE_TTL_MS = 6UL * 60UL * 60UL * 1000UL;
static constexpr uint32_t KMA_DNS_TIMEOUT_MS = 3000;

static void serviceWatchdog() {
    static unsigned long nextDisplayPumpMs = 0;
    static unsigned long nextWebPumpMs = 0;
    const unsigned long now = millis();
    EspClass::wdtFeed();  // NOLINT(readability-static-accessed-through-instance)
    if (webserver != nullptr && static_cast<long>(now - nextWebPumpMs) >= 0) {
        webserver->handleClient();
        nextWebPumpMs = now + 40UL;
    }
    if (static_cast<long>(now - nextDisplayPumpMs) >= 0) {
        DisplayManager::update();
        nextDisplayPumpMs = now + DISPLAY_PUMP_INTERVAL_MS;
    }
    yield();
}

static auto readHttpStatusLine(Client& client) -> String {
    String line;
    line.reserve(32);
    const unsigned long startedMs = millis();
    while (millis() - startedMs < 2500UL && (client.connected() || client.available())) {
        while (client.available()) {
            const char c = static_cast<char>(client.read());
            if (c == '\n') {
                line.trim();
                return line;
            }
            line += c;
        }
        delay(1);
        serviceWatchdog();
    }
    line.trim();
    return line;
}

static int readHttpHeadersContentLength(Client& client) {
    int contentLength = -1;
    String line;
    line.reserve(64);
    const unsigned long startedMs = millis();
    while (client.connected() || client.available()) {
        if (millis() - startedMs > 2000UL) {
            return contentLength;
        }
        if (!client.available()) {
            delay(1);
            serviceWatchdog();
            continue;
        }

        const char c = static_cast<char>(client.read());
        if (c != '\n') {
            line += c;
            continue;
        }

        line.trim();
        if (line.length() == 0) {
            return contentLength;
        }
        String lower = line;
        lower.toLowerCase();
        if (lower.startsWith("content-length:")) {
            contentLength = line.substring(line.indexOf(':') + 1).toInt();
        }
        line = "";
        serviceWatchdog();
    }

    return contentLength;
}

static auto httpOk(const String& statusLine) -> bool {
    return statusLine.startsWith("HTTP/1.1 200") || statusLine.startsWith("HTTP/1.0 200");
}

static auto parseOptionalFloat(const char* value, float fallback = 0.0F) -> float {
    if (value == nullptr || value[0] == '\0' || value[0] == '-') {
        return fallback;
    }
    return strtof(value, nullptr);
}

static auto copyJsonField(const char* json, const char* key, char* out, size_t outSize) -> bool {
    if (out == nullptr || outSize == 0) {
        return false;
    }
    out[0] = '\0';

    char pattern[40] = {};
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char* cursor = strstr(json, pattern);
    if (cursor == nullptr) {
        return false;
    }

    cursor += strlen(pattern);
    while (*cursor == ' ' || *cursor == '\t') {
        ++cursor;
    }

    bool quoted = false;
    if (*cursor == '"') {
        quoted = true;
        ++cursor;
    }

    size_t written = 0;
    while (*cursor != '\0' && written + 1 < outSize) {
        if (quoted) {
            if (*cursor == '"') {
                break;
            }
            if (*cursor == '\\' && cursor[1] != '\0') {
                ++cursor;
            }
        } else if (*cursor == ',' || *cursor == '}') {
            break;
        }

        out[written++] = *cursor++;
    }

    out[written] = '\0';
    return written > 0;
}

static auto daysFromCivil(int year, unsigned month, unsigned day) -> long {
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + static_cast<long>(doe) - 719468L;
}

static auto makeDisplayTimestamp(const String& ymd, const String& hour) -> time_t {
    if (ymd.length() < 8 || hour.length() < 2) {
        return 0;
    }

    const int year = ymd.substring(0, 4).toInt();
    const unsigned month = static_cast<unsigned>(ymd.substring(4, 6).toInt());
    const unsigned day = static_cast<unsigned>(ymd.substring(6, 8).toInt());
    const long days = daysFromCivil(year, month, day);
    return static_cast<time_t>((days * 24L * 60L * 60L) + (hour.substring(0, 2).toInt() * 60L * 60L));
}

static auto makeDisplayTimestamp(const char* ymd, const char* hour) -> time_t {
    if (ymd == nullptr || hour == nullptr || strlen(ymd) < 8 || strlen(hour) < 2) {
        return 0;
    }

    char part[5] = {};
    memcpy(part, ymd, 4);
    const int year = atoi(part);
    part[0] = ymd[4];
    part[1] = ymd[5];
    part[2] = '\0';
    const unsigned month = static_cast<unsigned>(atoi(part));
    part[0] = ymd[6];
    part[1] = ymd[7];
    const unsigned day = static_cast<unsigned>(atoi(part));
    part[0] = hour[0];
    part[1] = hour[1];
    const long days = daysFromCivil(year, month, day);
    return static_cast<time_t>((days * 24L * 60L * 60L) + (atoi(part) * 60L * 60L));
}

static auto kstNowTm() -> tm {
    const time_t now = time(nullptr) + (9L * 60L * 60L);
    tm out{};
    tm* timeInfo = gmtime(&now);
    if (timeInfo != nullptr) {
        out = *timeInfo;
    }
    return out;
}

static auto nextKmaStableRefreshDelayMs() -> unsigned long {
    const time_t nowUtc = time(nullptr);
    if (nowUtc < 1700000000L) {
        return WEATHER_REFRESH_INTERVAL_MS;
    }

    const time_t nowKst = nowUtc + (9L * 60L * 60L);
    tm* timeInfo = gmtime(&nowKst);
    if (timeInfo == nullptr) {
        return WEATHER_REFRESH_INTERVAL_MS;
    }

    const unsigned long secondInHour =
        (static_cast<unsigned long>(timeInfo->tm_min) * 60UL) + static_cast<unsigned long>(timeInfo->tm_sec);
    const unsigned long targetSecond = KMA_STABLE_REFRESH_MINUTE * 60UL;
    const unsigned long delaySeconds =
        secondInHour < targetSecond ? (targetSecond - secondInHour) : ((60UL * 60UL) - secondInHour + targetSecond);

    return delaySeconds * 1000UL;
}

static auto formatKstDate(const tm& timeInfo) -> String {
    String value = String(timeInfo.tm_year + 1900);
    if (timeInfo.tm_mon + 1 < 10) {
        value += '0';
    }
    value += String(timeInfo.tm_mon + 1);
    if (timeInfo.tm_mday < 10) {
        value += '0';
    }
    value += String(timeInfo.tm_mday);
    return value;
}

static auto formatHourMinute(const tm& timeInfo, int minute) -> String {
    char buffer[8] = {};
    snprintf(buffer, sizeof(buffer), "%02d%02d", timeInfo.tm_hour, minute);
    return buffer;
}

static auto mapKmaWeatherCode(int sky, int pty) -> int {
    switch (pty) {
        case 1:
        case 5:
            return 61;
        case 2:
        case 3:
        case 6:
        case 7:
            return 71;
        default:
            break;
    }

    if (sky <= 1) {
        return 0;
    }
    return 3;
}

static bool applyKmaForecastObject(const char* objectJson,
                                   std::array<WeatherClient::ForecastEntry, WEATHER_FORECAST_COUNT>& forecast,
                                   size_t& forecastCount) {
    char category[8] = {};
    char fcstDate[12] = {};
    char fcstTime[8] = {};
    char fcstValue[24] = {};
    if (!copyJsonField(objectJson, "category", category, sizeof(category)) ||
        !copyJsonField(objectJson, "fcstDate", fcstDate, sizeof(fcstDate)) ||
        !copyJsonField(objectJson, "fcstTime", fcstTime, sizeof(fcstTime)) ||
        !copyJsonField(objectJson, "fcstValue", fcstValue, sizeof(fcstValue))) {
        return false;
    }

    if (strcmp(category, "T1H") != 0 && strcmp(category, "RN1") != 0 && strcmp(category, "PTY") != 0 &&
        strcmp(category, "SKY") != 0 && strcmp(category, "REH") != 0) {
        return false;
    }

    const time_t timestamp = makeDisplayTimestamp(fcstDate, fcstTime);
    size_t index = 0;
    while (index < forecastCount && forecast[index].timestamp != timestamp) {
        ++index;
    }
    if (index >= WEATHER_FORECAST_COUNT) {
        return false;
    }
    if (index == forecastCount) {
        forecast[index].timestamp = timestamp;
        ++forecastCount;
    }

    WeatherClient::ForecastEntry& entry = forecast[index];
    if (strcmp(category, "T1H") == 0) {
        entry.temperature = parseOptionalFloat(fcstValue, 0.0F);
    } else if (strcmp(category, "RN1") == 0) {
        entry.rain = parseOptionalFloat(fcstValue, 0.0F);
        entry.precipitation = entry.rain;
    } else if (strcmp(category, "REH") == 0) {
        entry.humidity = parseOptionalFloat(fcstValue, -1.0F);
    } else if (strcmp(category, "PTY") == 0) {
        const int pty = atoi(fcstValue);
        if (pty != 0) {
            entry.weatherCode = mapKmaWeatherCode(3, pty);
        }
    } else if (strcmp(category, "SKY") == 0 && entry.weatherCode < 0) {
        entry.weatherCode = mapKmaWeatherCode(atoi(fcstValue), 0);
    }

    return true;
}

static bool parseKmaForecastStream(Client& client,
                                   int expectedBytes,
                                   std::array<WeatherClient::ForecastEntry, WEATHER_FORECAST_COUNT>& forecast) {
    for (auto& entry : forecast) {
        entry = WeatherClient::ForecastEntry{};
    }

    size_t forecastCount = 0;
    int readCount = 0;
    unsigned long lastDataMs = millis();
    bool recording = false;
    char objectJson[320] = {};
    size_t objectLength = 0;

    while (expectedBytes <= 0 || readCount < expectedBytes) {
        if (!client.available()) {
            if (millis() - lastDataMs > 1500UL) {
                break;
            }
            delay(1);
            serviceWatchdog();
            continue;
        }

        const char c = static_cast<char>(client.read());
        ++readCount;
        lastDataMs = millis();

        if (c == '{') {
            recording = true;
            objectLength = 0;
        }
        if (recording && objectLength + 1 < sizeof(objectJson)) {
            objectJson[objectLength++] = c;
            objectJson[objectLength] = '\0';
        }
        if (recording && c == '}') {
            applyKmaForecastObject(objectJson, forecast, forecastCount);
            recording = false;
        }

        serviceWatchdog();
    }

    return forecastCount > 0;
}

static bool parseKmaCurrentStream(Client& client,
                                  int expectedBytes,
                                  float& currentTemperature,
                                  float& currentRain,
                                  float& currentHumidity,
                                  int& currentPty) {
    currentTemperature = 0.0F;
    currentRain = 0.0F;
    currentHumidity = -1.0F;
    currentPty = 0;

    int readCount = 0;
    unsigned long lastDataMs = millis();
    bool recording = false;
    char objectJson[240] = {};
    size_t objectLength = 0;
    bool sawValue = false;

    while (expectedBytes <= 0 || readCount < expectedBytes) {
        if (!client.available()) {
            if (millis() - lastDataMs > 1500UL) {
                break;
            }
            delay(1);
            serviceWatchdog();
            continue;
        }

        const char c = static_cast<char>(client.read());
        ++readCount;
        lastDataMs = millis();

        if (c == '{') {
            recording = true;
            objectLength = 0;
        }
        if (recording && objectLength + 1 < sizeof(objectJson)) {
            objectJson[objectLength++] = c;
            objectJson[objectLength] = '\0';
        }
        if (recording && c == '}') {
            char category[8] = {};
            char value[24] = {};
            if (copyJsonField(objectJson, "category", category, sizeof(category)) &&
                copyJsonField(objectJson, "obsrValue", value, sizeof(value))) {
                if (strcmp(category, "T1H") == 0) {
                    currentTemperature = parseOptionalFloat(value, 0.0F);
                    sawValue = true;
                } else if (strcmp(category, "RN1") == 0) {
                    currentRain = parseOptionalFloat(value, 0.0F);
                    sawValue = true;
                } else if (strcmp(category, "REH") == 0) {
                    currentHumidity = parseOptionalFloat(value, -1.0F);
                    sawValue = true;
                } else if (strcmp(category, "PTY") == 0) {
                    currentPty = atoi(value);
                    sawValue = true;
                }
            }
            recording = false;
        }

        serviceWatchdog();
    }

    return sawValue;
}

static void latLonToKmaGrid(float latitude, float longitude, int& nx, int& ny) {
    static constexpr double RE = 6371.00877;
    static constexpr double GRID = 5.0;
    static constexpr double SLAT1 = 30.0;
    static constexpr double SLAT2 = 60.0;
    static constexpr double OLON = 126.0;
    static constexpr double OLAT = 38.0;
    static constexpr double XO = 43.0;
    static constexpr double YO = 136.0;
    static constexpr double DEGRAD = LOCAL_PI / 180.0;

    const double re = RE / GRID;
    const double slat1 = SLAT1 * DEGRAD;
    const double slat2 = SLAT2 * DEGRAD;
    const double olon = OLON * DEGRAD;
    const double olat = OLAT * DEGRAD;
    double sn = tan(LOCAL_PI * 0.25 + slat2 * 0.5) / tan(LOCAL_PI * 0.25 + slat1 * 0.5);
    sn = log(cos(slat1) / cos(slat2)) / log(sn);
    double sf = tan(LOCAL_PI * 0.25 + slat1 * 0.5);
    sf = pow(sf, sn) * cos(slat1) / sn;
    double ro = tan(LOCAL_PI * 0.25 + olat * 0.5);
    ro = re * sf / pow(ro, sn);
    double ra = tan(LOCAL_PI * 0.25 + latitude * DEGRAD * 0.5);
    ra = re * sf / pow(ra, sn);
    double theta = longitude * DEGRAD - olon;
    if (theta > LOCAL_PI) {
        theta -= 2.0 * LOCAL_PI;
    }
    if (theta < -LOCAL_PI) {
        theta += 2.0 * LOCAL_PI;
    }
    theta *= sn;
    nx = static_cast<int>(floor(ra * sin(theta) + XO + 0.5));
    ny = static_cast<int>(floor(ro - ra * cos(theta) + YO + 0.5));
}

WeatherClient::WeatherClient() = default;

void WeatherClient::begin() {
    _snapshot = Snapshot{};
    _snapshot.status = "weather idle";
    _nextRefreshMs = millis() + WEATHER_INITIAL_RETRY_MS;
}

void WeatherClient::loop() {
    if (!configManager.isWeatherEnabled()) {
        _snapshot.fetching = false;
        _refreshPhase = RefreshPhase::Idle;
        return;
    }

    if (!WiFiManager::isConnected()) {
        _snapshot.fetching = false;
        _refreshPhase = RefreshPhase::Idle;
        _nextRefreshMs = millis() + WEATHER_INITIAL_RETRY_MS;
        _snapshot.status = "network unavailable";
        return;
    }

    if (_refreshPhase != RefreshPhase::Idle) {
        runKmaRefreshStep();
        return;
    }

    const unsigned long nowMs = millis();
    if (static_cast<long>(nowMs - _nextRefreshMs) >= 0) {
        fetchForecast();
    }
}

bool WeatherClient::refreshNow() {
    requestRefresh();
    return true;
}

void WeatherClient::requestRefresh(unsigned long delayMs) {
    _snapshot.status = "refresh queued";
    _nextRefreshMs = millis() + delayMs;
}

auto WeatherClient::getSnapshot() const -> const Snapshot& { return _snapshot; }

bool WeatherClient::startKmaRefresh() {
    if (_refreshPhase != RefreshPhase::Idle) {
        return true;
    }

    _snapshot.status = "kma current queued";
    _snapshot.fetching = true;
    serviceWatchdog();

    const String apiKey = configManager.getWeatherKmaApiKey();
    if (apiKey.length() == 0) {
        _snapshot.status = "kma api key missing";
        _snapshot.fetching = false;
        return false;
    }

    const float latitude = configManager.getWeatherLatitude();
    const float longitude = configManager.getWeatherLongitude();
    if (latitude < -90.0F || latitude > 90.0F || longitude < -180.0F || longitude > 180.0F ||
        (latitude == 0.0F && longitude == 0.0F)) {
        _snapshot.status = "invalid weather coordinates";
        _snapshot.fetching = false;
        return false;
    }

    _pendingNx = configManager.getWeatherKmaGridX();
    _pendingNy = configManager.getWeatherKmaGridY();
    if (_pendingNx <= 0 || _pendingNy <= 0) {
        latLonToKmaGrid(latitude, longitude, _pendingNx, _pendingNy);
    }

    tm nowKst = kstNowTm();
    if (nowKst.tm_year + 1900 < 2024) {
        _snapshot.status = "time not synced";
        _snapshot.fetching = false;
        _nextRefreshMs = millis() + WEATHER_TIME_SYNC_RETRY_MS;
        return false;
    }

    tm ncstBase = nowKst;
    if (ncstBase.tm_min < 40) {
        time_t adjusted = time(nullptr) + (8L * 60L * 60L);
        tm* adjustedInfo = gmtime(&adjusted);
        if (adjustedInfo != nullptr) {
            ncstBase = *adjustedInfo;
        }
    }
    tm ultraBase = nowKst;
    if (ultraBase.tm_min < 45) {
        time_t adjusted = time(nullptr) + (8L * 60L * 60L);
        tm* adjustedInfo = gmtime(&adjusted);
        if (adjustedInfo != nullptr) {
            ultraBase = *adjustedInfo;
        }
    }

    _pendingNcstDate = formatKstDate(ncstBase);
    _pendingNcstTime = formatHourMinute(ncstBase, 0);
    _pendingUltraDate = formatKstDate(ultraBase);
    _pendingUltraTime = formatHourMinute(ultraBase, 30);
    _pendingCurrentTemperature = 0.0F;
    _pendingCurrentRain = 0.0F;
    _pendingCurrentHumidity = -1.0F;
    _pendingCurrentPty = 0;
    _forecastAttempt = 0;
    _refreshPhase = RefreshPhase::Resolve;
    return true;
}

bool WeatherClient::runKmaRefreshStep() {
    switch (_refreshPhase) {
        case RefreshPhase::Resolve:
            if (!resolveKmaAddressStep()) {
                finishKmaRefresh(false);
                return false;
            }
            _refreshPhase = RefreshPhase::Current;
            _snapshot.status = "kma current queued";
            return true;
        case RefreshPhase::Current:
            if (!fetchKmaCurrentStep()) {
                finishKmaRefresh(false);
                return false;
            }
            _refreshPhase = RefreshPhase::Forecast;
            _snapshot.status = "kma forecast queued";
            return true;
        case RefreshPhase::Forecast:
            switch (fetchKmaForecastStep()) {
                case StepResult::Success:
                    finishKmaRefresh(true);
                    return true;
                case StepResult::Continue:
                    return true;
                case StepResult::Failed:
                default:
                    finishKmaRefresh(false);
                    return false;
            }
        case RefreshPhase::Idle:
        default:
            return true;
    }
}

bool WeatherClient::resolveKmaAddressStep() {
    const unsigned long nowMs = millis();
    if (_kmaAddressResolvedMs != 0 &&
        static_cast<long>(nowMs - (_kmaAddressResolvedMs + KMA_DNS_CACHE_TTL_MS)) < 0) {
        return true;
    }

    _snapshot.status = "kma dns refresh";
    serviceWatchdog();

    IPAddress resolved;
    if (WiFi.hostByName(KMA_API_HOST, resolved, KMA_DNS_TIMEOUT_MS) != 1) {
        _snapshot.status = "kma dns failed";
        Logger::warn("KMA DNS lookup failed", TAG);
        return false;
    }

    _kmaAddress = resolved;
    _kmaAddressResolvedMs = millis();
    serviceWatchdog();
    return true;
}

bool WeatherClient::fetchKmaCurrentStep() {
    _snapshot.status = "kma current refresh";
    _snapshot.fetching = true;
    const String apiKey = configManager.getWeatherKmaApiKey();
    if (apiKey.length() == 0) {
        _snapshot.status = "kma api key missing";
        return false;
    }

    const String basePath = "/api/typ02/openApi/VilageFcstInfoService_2.0";

    WiFiClient client;
    client.setTimeout(800);

    if (!client.connect(_kmaAddress, KMA_API_PORT)) {
        _snapshot.status = "kma connect failed";
        _kmaAddressResolvedMs = 0;
        return false;
    }
    serviceWatchdog();

    const String path = basePath + "/getUltraSrtNcst?pageNo=1&numOfRows=10&dataType=JSON&base_date=" +
                        _pendingNcstDate + "&base_time=" + _pendingNcstTime + "&nx=" + String(_pendingNx) + "&ny=" +
                        String(_pendingNy) + "&authKey=" + apiKey;
    client.setTimeout(1600);
    client.print(F("GET "));
    client.print(path);
    client.print(F(" HTTP/1.0\r\nHost: "));
    client.print(KMA_API_HOST);
    client.print(F("\r\nUser-Agent: SmallTVUltraKoreanCustomFirmware/1.0\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n"));

    String statusLine = readHttpStatusLine(client);
    serviceWatchdog();
    if (!httpOk(statusLine)) {
        _snapshot.status = "kma current http error";
        client.stop();
        return false;
    }
    const int ncstContentLength = readHttpHeadersContentLength(client);
    serviceWatchdog();

    const bool currentParsed =
        parseKmaCurrentStream(client, ncstContentLength, _pendingCurrentTemperature, _pendingCurrentRain,
                              _pendingCurrentHumidity, _pendingCurrentPty);
    client.stop();
    serviceWatchdog();

    if (!currentParsed) {
        _snapshot.status = "kma current parse failed";
        Logger::warn("KMA current stream parse failed", TAG);
        return false;
    }

    return true;
}

WeatherClient::StepResult WeatherClient::fetchKmaForecastStep() {
    _snapshot.status = "kma forecast refresh";
    _snapshot.fetching = true;
    const String apiKey = configManager.getWeatherKmaApiKey();
    if (apiKey.length() == 0) {
        _snapshot.status = "kma api key missing";
        return StepResult::Failed;
    }

    const String basePath = "/api/typ02/openApi/VilageFcstInfoService_2.0";
    const String path = basePath + "/getUltraSrtFcst?pageNo=1&numOfRows=40&dataType=JSON&base_date=" +
                        _pendingUltraDate + "&base_time=" + _pendingUltraTime + "&nx=" + String(_pendingNx) +
                        "&ny=" + String(_pendingNy) + "&authKey=" + apiKey;

    int fcstContentLength = -1;
    WiFiClient forecastClient;
    forecastClient.setTimeout(800);
    if (!forecastClient.connect(_kmaAddress, KMA_API_PORT)) {
        _snapshot.status = "kma forecast connect failed";
        _kmaAddressResolvedMs = 0;
        ++_forecastAttempt;
        serviceWatchdog();
        if (_forecastAttempt < 2) {
            _snapshot.status = "kma forecast retry queued";
            return StepResult::Continue;
        }
        return StepResult::Failed;
    }
    serviceWatchdog();

    forecastClient.setTimeout(2500);
    forecastClient.print(F("GET "));
    forecastClient.print(path);
    forecastClient.print(F(" HTTP/1.0\r\nHost: "));
    forecastClient.print(KMA_API_HOST);
    forecastClient.print(F("\r\nUser-Agent: SmallTVUltraKoreanCustomFirmware/1.0\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n"));
    const String statusLine = readHttpStatusLine(forecastClient);
    serviceWatchdog();
    if (!httpOk(statusLine)) {
        _snapshot.status = "kma forecast http error";
        Logger::warn((String("KMA forecast HTTP retry ") + String(_forecastAttempt + 1) + ": " + statusLine).c_str(),
                     TAG);
        forecastClient.stop();
        ++_forecastAttempt;
        serviceWatchdog();
        if (_forecastAttempt < 2) {
            _snapshot.status = "kma forecast retry queued";
            return StepResult::Continue;
        }
        return StepResult::Failed;
    }

    fcstContentLength = readHttpHeadersContentLength(forecastClient);
    serviceWatchdog();
    const bool forecastParsed = parseKmaForecastStream(forecastClient, fcstContentLength, _snapshot.forecast);
    forecastClient.stop();
    serviceWatchdog();

    if (!forecastParsed) {
        if (fcstContentLength >= 0) {
            _snapshot.status = String("kma forecast parse failed len ") + String(fcstContentLength);
        }
        Logger::warn("KMA forecast stream parse failed", TAG);
        ++_forecastAttempt;
        if (_forecastAttempt < 2) {
            _snapshot.status = "kma forecast retry queued";
            return StepResult::Continue;
        }
        return StepResult::Failed;
    }

    _snapshot.currentTime = makeDisplayTimestamp(_pendingNcstDate, _pendingNcstTime.substring(0, 2));
    _snapshot.currentTemperature = _pendingCurrentTemperature;
    _snapshot.currentRain = _pendingCurrentRain;
    _snapshot.currentPrecipitation = _pendingCurrentRain;
    _snapshot.currentPrecipitationProbability = -1.0F;
    _snapshot.currentHumidity = _pendingCurrentHumidity;
    _snapshot.currentCloudCover = 0.0F;
    _snapshot.currentVisibility = 0.0F;
    _snapshot.currentWeatherCode =
        _pendingCurrentPty != 0
            ? mapKmaWeatherCode(3, _pendingCurrentPty)
            : (_snapshot.forecast[0].weatherCode >= 0 ? _snapshot.forecast[0].weatherCode : mapKmaWeatherCode(3, 0));
    _snapshot.isRaining = _pendingCurrentRain > 0.0F || _pendingCurrentPty != 0;
    _snapshot.utcOffsetSeconds = 9L * 60L * 60L;
    _snapshot.timezone = "Asia/Seoul";
    _snapshot.hasData = true;
    _snapshot.lastUpdated = time(nullptr);
    _snapshot.status = "ok";
    _snapshot.source = "KMA APIHub";

    Logger::info("Weather updated from KMA APIHub", TAG);
    return StepResult::Success;
}

void WeatherClient::finishKmaRefresh(bool ok) {
    _snapshot.fetching = false;
    _refreshPhase = RefreshPhase::Idle;
    const unsigned long retryDelayMs = _snapshot.hasData ? WEATHER_STALE_RETRY_MS : WEATHER_INITIAL_RETRY_MS;
    _nextRefreshMs = millis() + (ok ? nextKmaStableRefreshDelayMs() : retryDelayMs);
    if (ok && (configManager.isClockEnabled() || configManager.isWeatherEnabled())) {
        DisplayManager::pauseClock(0);
        DisplayManager::drawClock();
    }
}

bool WeatherClient::fetchForecast() {
    if (startKmaRefresh()) {
        return true;
    }

    _refreshPhase = RefreshPhase::Idle;
    _snapshot.fetching = false;
    const unsigned long nowMs = millis();
    if (static_cast<long>(_nextRefreshMs - nowMs) <= 0) {
        _nextRefreshMs = nowMs + WEATHER_INITIAL_RETRY_MS;
    }
    return false;
}
