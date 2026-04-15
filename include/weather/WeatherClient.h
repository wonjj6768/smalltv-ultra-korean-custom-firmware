// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * GeekMagic Open Firmware
 * Copyright (C) 2026 Times-Z
 */

#pragma once

#include <Arduino.h>
#include <array>
#include <ctime>

class WeatherClient {
   public:
    struct ForecastEntry {
        time_t timestamp = 0;
        float temperature = 0.0F;
        float rain = 0.0F;
        float precipitation = 0.0F;
        float precipitationProbability = -1.0F;
        int weatherCode = -1;
    };

    struct Snapshot {
        bool hasData = false;
        bool hasAirQuality = false;
        bool fetching = false;
        bool isRaining = false;
        float currentTemperature = 0.0F;
        float currentRain = 0.0F;
        float currentPrecipitation = 0.0F;
        float currentPrecipitationProbability = -1.0F;
        float currentCloudCover = 0.0F;
        float currentVisibility = 0.0F;
        float currentPm25 = 0.0F;
        float currentOzone = 0.0F;
        float currentPm25Aqi = 0.0F;
        float currentOzoneAqi = 0.0F;
        int currentWeatherCode = -1;
        time_t currentTime = 0;
        time_t lastUpdated = 0;
        long utcOffsetSeconds = 0;
        String timezone;
        String status;
        std::array<ForecastEntry, 4> forecast{};
    };

    WeatherClient();

    void begin();
    void loop();
    bool refreshNow();
    const Snapshot& getSnapshot() const;

   private:
    bool fetchForecast();
    static auto buildForecastUrl(float latitude, float longitude, const String& timezone) -> String;

    Snapshot _snapshot;
    unsigned long _nextRefreshMs = 0;
};
