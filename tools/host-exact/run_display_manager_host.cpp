#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "Arduino_GFX_Library.h"
#include "LittleFS.h"
#include "config/ConfigManager.h"
#include "display/DisplayManager.h"
#include "weather/WeatherClient.h"
#include "wireless/WiFiManager.h"

ConfigManager configManager;
WeatherClient* weatherClient = nullptr;
WiFiManager* wifiManager = nullptr;
bool g_legacyUpdateModeEnabled = false;
const char* AP_SSID = "GeekMagic";
std::time_t g_hostNow = 0;

WiFiManager::WiFiManager(const char* staSsid, const char* staPass, const char* apSsid, const char* apPass)
    : _staSsid(staSsid), _staPass(staPass), _apSsid(apSsid), _apPass(apPass) {}

void WiFiManager::begin() {}

auto WiFiManager::startStationMode() -> bool { return true; }

auto WiFiManager::startAccessPointMode() -> bool {
    _apMode = true;
    return true;
}

auto WiFiManager::isApMode() const -> bool { return _apMode; }

auto WiFiManager::getIP() const -> IPAddress {
    return _apMode ? IPAddress(192, 168, 4, 1) : IPAddress(192, 168, 0, 42);
}

void WiFiManager::scanNetworks(JsonArray&) {}

auto WiFiManager::connectToNetwork(const char*, const char*, uint32_t) -> bool {
    _apMode = false;
    return true;
}

auto WiFiManager::isConnected() -> bool { return true; }

auto WiFiManager::getConnectedSSID() -> String { return String("Host WiFi"); }

static auto daysFromCivil(int year, unsigned month, unsigned day) -> long {
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + static_cast<long>(doe) - 719468L;
}

static auto displayTimestampFromLocalTime(const std::tm& timeInfo) -> std::time_t {
    const int year = timeInfo.tm_year + 1900;
    const unsigned month = static_cast<unsigned>(timeInfo.tm_mon + 1);
    const unsigned day = static_cast<unsigned>(timeInfo.tm_mday);
    const long days = daysFromCivil(year, month, day);
    return static_cast<std::time_t>((days * 24L * 60L * 60L) + (static_cast<long>(timeInfo.tm_hour) * 60L * 60L));
}

static auto forecastDisplayTimestamp(std::time_t now, int hoursAhead) -> std::time_t {
    std::tm localTime = {};
    if (std::tm* timeInfo = std::localtime(&now); timeInfo != nullptr) {
        localTime = *timeInfo;
    }
    localTime.tm_min = 0;
    localTime.tm_sec = 0;
    localTime.tm_hour += hoursAhead;
    std::mktime(&localTime);
    return displayTimestampFromLocalTime(localTime);
}

static auto applyWeatherPreset(WeatherClient::Snapshot& snapshot, const std::string& preset, std::time_t now) -> void {
    const std::string scenario = preset == "aq-korea" ? "air" : preset;
    snapshot = {};
    snapshot.hasData = true;
    snapshot.hasAirQuality = true;
    snapshot.currentTime = forecastDisplayTimestamp(now, 0);
    snapshot.lastUpdated = now;
    snapshot.utcOffsetSeconds = 9 * 60 * 60;
    snapshot.timezone = "Asia/Seoul";
    snapshot.status = "host";
    snapshot.currentPm25 = 18.0F;
    snapshot.currentOzone = 61.0F;
    snapshot.currentPm25Aqi = 42.0F;
    snapshot.currentOzoneAqi = 27.0F;
    snapshot.currentTemperature = 23.0F;
    snapshot.currentVisibility = 10000.0F;
    snapshot.currentCloudCover = 8.0F;
    snapshot.currentWeatherCode = 0;
    snapshot.currentPrecipitation = 0.0F;
    snapshot.currentPrecipitationProbability = 0.0F;
    snapshot.currentHumidity = 55.0F;
    snapshot.currentRain = 0.0F;
    snapshot.isRaining = false;

    if (scenario == "rain") {
        snapshot.currentTemperature = 19.0F;
        snapshot.currentWeatherCode = 61;
        snapshot.currentPrecipitation = 2.4F;
        snapshot.currentPrecipitationProbability = 80.0F;
        snapshot.currentRain = 2.4F;
        snapshot.currentCloudCover = 96.0F;
        snapshot.currentVisibility = 5000.0F;
        snapshot.isRaining = true;
    } else if (scenario == "fog") {
        snapshot.currentTemperature = 14.0F;
        snapshot.currentWeatherCode = 45;
        snapshot.currentCloudCover = 88.0F;
        snapshot.currentVisibility = 900.0F;
    } else if (scenario == "air") {
        snapshot.currentTemperature = 21.0F;
        snapshot.currentWeatherCode = 3;
        snapshot.currentCloudCover = 46.0F;
        snapshot.currentVisibility = 8000.0F;
        snapshot.currentPm25 = 41.0F;
        snapshot.currentOzone = 196.0F;
    } else if (scenario == "clear") {
        snapshot.currentTemperature = 26.0F;
        snapshot.currentWeatherCode = 0;
        snapshot.currentCloudCover = 3.0F;
        snapshot.currentVisibility = 12000.0F;
    }

    const int weatherCodes[5] = {
        snapshot.currentWeatherCode,
        scenario == "clear" ? 1 : (scenario == "fog" ? 48 : (scenario == "air" ? 2 : 63)),
        scenario == "rain" ? 80 : 2,
        scenario == "clear" ? 3 : (scenario == "fog" ? 45 : (scenario == "air" ? 3 : 81)),
        scenario == "rain" ? 63 : (scenario == "fog" ? 45 : 2),
    };

    const float temperatures[5] = {
        snapshot.currentTemperature,
        static_cast<float>(snapshot.currentTemperature - 1.0F),
        static_cast<float>(snapshot.currentTemperature - 2.0F),
        static_cast<float>(snapshot.currentTemperature - 3.0F),
        static_cast<float>(snapshot.currentTemperature - 4.0F),
    };

    const float precipitation[5] = {
        snapshot.currentPrecipitation,
        scenario == "rain" ? 1.6F : 0.0F,
        0.0F,
        scenario == "rain" ? 0.4F : 0.0F,
        0.0F,
    };

    const float probability[5] = {
        snapshot.currentPrecipitationProbability,
        scenario == "clear" ? 0.0F : 45.0F,
        scenario == "clear" ? 0.0F : 35.0F,
        scenario == "clear" ? 0.0F : 25.0F,
        scenario == "clear" ? 0.0F : 15.0F,
    };

    const float humidity[5] = {
        scenario == "rain" ? 88.0F : 55.0F,
        scenario == "rain" ? 91.0F : 52.0F,
        scenario == "rain" ? 86.0F : 48.0F,
        scenario == "rain" ? 82.0F : 45.0F,
        scenario == "rain" ? 78.0F : 42.0F,
    };

    for (size_t index = 0; index < snapshot.forecast.size(); ++index) {
        auto& entry = snapshot.forecast[index];
        entry.timestamp = forecastDisplayTimestamp(now, static_cast<int>(index) + 1);
        entry.temperature = temperatures[index];
        entry.rain = precipitation[index];
        entry.precipitation = precipitation[index];
        entry.precipitationProbability = probability[index];
        entry.humidity = humidity[index];
        entry.weatherCode = weatherCodes[index];
    }
}

static auto renderTextPreset(Arduino_GFX* gfx) -> void {
    if (gfx == nullptr) {
        return;
    }

    gfx->fillScreen(LCD_BLACK);
    gfx->setTextWrap(false);
    gfx->setFont(nullptr);
    gfx->setTextColor(LCD_WHITE, LCD_BLACK);
    gfx->setTextSize(1);

    gfx->setCursor(8, 18);
    gfx->print("ASCII fallback path");

    gfx->setCursor(8, 34);
    gfx->print("startup text check");

    gfx->setTextWrap(true);
    gfx->setCursor(8, 56);
    gfx->print("This line intentionally wraps across the host framebuffer for pixel matching.");
    gfx->setTextWrap(false);

    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    const String centered = "getTextBounds center";
    gfx->getTextBounds(centered, 0, 0, &x1, &y1, &w, &h);
    const int16_t centeredX = static_cast<int16_t>((gfx->width() - static_cast<int16_t>(w)) / 2);
    const int16_t centeredBaseline = 94;
    gfx->setCursor(centeredX, centeredBaseline);
    gfx->print(centered);

    gfx->drawFastHLine(8, 108, 224, LCD_WHITE);
    gfx->setCursor(8, 126);
    gfx->print("0123456789 !@#$%^&*()");
}

static auto presetTimestamp(const std::string& preset) -> std::time_t {
    const std::string scenario = preset == "aq-korea" ? "air" : preset;
    std::tm localTime = {};
    localTime.tm_year = 2026 - 1900;
    localTime.tm_mon = 3;
    localTime.tm_mday = 17;
    localTime.tm_hour = 7;
    localTime.tm_min = 56;
    localTime.tm_sec = 20;

    if (scenario == "rain") {
        localTime.tm_hour = 21;
        localTime.tm_min = 8;
        localTime.tm_sec = 35;
    } else if (scenario == "fog") {
        localTime.tm_hour = 6;
        localTime.tm_min = 42;
        localTime.tm_sec = 10;
    } else if (scenario == "air") {
        localTime.tm_hour = 15;
        localTime.tm_min = 24;
        localTime.tm_sec = 48;
    }

    return std::mktime(&localTime);
}

static auto parseArg(int argc, char** argv, const std::string& key, const std::string& fallback) -> std::string {
    for (int index = 1; index < argc - 1; ++index) {
        if (std::string(argv[index]) == key) {
            return argv[index + 1];
        }
    }
    return fallback;
}

static auto envValue(const char* name) -> const char* {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' ? value : nullptr;
}

static auto envFloat(const char* name, float fallback) -> float {
    const char* value = envValue(name);
    return value == nullptr ? fallback : std::strtof(value, nullptr);
}

static auto envInt(const char* name, int fallback) -> int {
    const char* value = envValue(name);
    return value == nullptr ? fallback : std::atoi(value);
}

static auto envTime(const char* name, std::time_t fallback) -> std::time_t {
    const char* value = envValue(name);
    return value == nullptr ? fallback : static_cast<std::time_t>(std::atoll(value));
}

static auto applyWeatherEnvOverride(WeatherClient::Snapshot& snapshot) -> void {
    if (envValue("HOST_WEATHER_HAS_DATA") == nullptr) {
        return;
    }

    snapshot.hasData = envInt("HOST_WEATHER_HAS_DATA", 1) != 0;
    snapshot.hasAirQuality = envInt("HOST_WEATHER_HAS_AIR", 0) != 0;
    snapshot.currentTime = envTime("HOST_WEATHER_CURRENT_TIME", snapshot.currentTime);
    snapshot.lastUpdated = envTime("HOST_WEATHER_LAST_UPDATED", snapshot.lastUpdated);
    snapshot.currentTemperature = envFloat("HOST_WEATHER_CURRENT_TEMP", snapshot.currentTemperature);
    snapshot.currentRain = envFloat("HOST_WEATHER_CURRENT_RAIN", snapshot.currentRain);
    snapshot.currentPrecipitation = envFloat("HOST_WEATHER_CURRENT_PRECIP", snapshot.currentPrecipitation);
    snapshot.currentPrecipitationProbability =
        envFloat("HOST_WEATHER_CURRENT_PROB", snapshot.currentPrecipitationProbability);
    snapshot.currentHumidity = envFloat("HOST_WEATHER_CURRENT_HUMIDITY", snapshot.currentHumidity);
    snapshot.currentWeatherCode = envInt("HOST_WEATHER_CURRENT_CODE", snapshot.currentWeatherCode);
    snapshot.isRaining = snapshot.currentRain > 0.0F || snapshot.currentPrecipitation > 0.0F;
    snapshot.timezone = envValue("HOST_WEATHER_TIMEZONE") != nullptr ? envValue("HOST_WEATHER_TIMEZONE") : "Asia/Seoul";

    for (size_t index = 0; index < snapshot.forecast.size(); ++index) {
        const std::string prefix = "HOST_WEATHER_F" + std::to_string(index) + "_";
        auto& entry = snapshot.forecast[index];
        entry.timestamp = envTime((prefix + "TIME").c_str(), entry.timestamp);
        entry.temperature = envFloat((prefix + "TEMP").c_str(), entry.temperature);
        entry.rain = envFloat((prefix + "RAIN").c_str(), entry.rain);
        entry.precipitation = envFloat((prefix + "PRECIP").c_str(), entry.precipitation);
        entry.precipitationProbability = envFloat((prefix + "PROB").c_str(), entry.precipitationProbability);
        entry.humidity = envFloat((prefix + "HUMIDITY").c_str(), entry.humidity);
        entry.weatherCode = envInt((prefix + "CODE").c_str(), entry.weatherCode);
    }
}

int main(int argc, char** argv) {
    const std::filesystem::path repoRoot = std::filesystem::current_path();
    const std::string preset = parseArg(argc, argv, "--preset", "rain");
    const std::filesystem::path outputPath = parseArg(
        argc, argv, "--output", (repoRoot / "output" / "display-manager-host-exact.bmp").string());

#if defined(_WIN32)
    _putenv_s("TZ", "KST-9");
    _tzset();
#endif

    LittleFSClass::setRoot(repoRoot / "data");

    configManager.setClockEnabled(true);
    configManager.setWeatherEnabled(true);
    configManager.setClockUse24Hour(true);
    configManager.setLCDRotation(0);
    configManager.setWeatherTimezone("Asia/Seoul");
    configManager.setTimezoneRegion("Asia/Seoul");

    WiFiManager hostWifi("", "", AP_SSID, "");
    wifiManager = &hostWifi;

    WeatherClient weather;
    weatherClient = &weather;
    configManager.weather_location_name = "서울시";
    g_hostNow = presetTimestamp(preset);
    applyWeatherPreset(weather.mutableSnapshot(), preset, g_hostNow);
    applyWeatherEnvOverride(weather.mutableSnapshot());
    g_hostNow = envTime("HOST_NOW", g_hostNow);

    DisplayManager::begin();
    if (preset == "text" || preset == "fallback-text") {
        renderTextPreset(DisplayManager::getGfx());
    } else {
        DisplayManager::update();
    }

    auto* gfx = DisplayManager::getGfx();
    auto* surface = static_cast<Arduino_ST7789*>(gfx);
    if (surface == nullptr || !surface->saveBMP(outputPath)) {
        std::cerr << "Failed to save BMP to " << outputPath << '\n';
        return 1;
    }

    std::cout << outputPath.string() << '\n';
    return 0;
}
