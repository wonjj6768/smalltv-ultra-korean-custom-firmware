// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * SmallTV-Ultra Korean Custom Firmware
 * Copyright (C) 2026 Times-Z
 */

#pragma once

#include <Arduino.h>
#include <IPAddress.h>
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
        float humidity = -1.0F;
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
        float currentHumidity = -1.0F;
        float currentCloudCover = 0.0F;
        float currentVisibility = 0.0F;
        float currentPm25 = 0.0F;
        float currentOzone = 0.0F;
        float currentPm25Aqi = 0.0F;
        float currentOzoneAqi = 0.0F;
        bool hasTodayHighTemperature = false;
        float todayHighTemperature = 0.0F;
        String todayHighDate;
        int currentWeatherCode = -1;
        time_t currentTime = 0;
        time_t lastUpdated = 0;
        long utcOffsetSeconds = 0;
        String timezone;
        String status;
        String source;
        std::array<ForecastEntry, 5> forecast{};
    };

    WeatherClient();

    void begin();
    void loop();
    bool refreshNow();
    void requestRefresh(unsigned long delayMs = 0);
    const Snapshot& getSnapshot() const;

   private:
    bool fetchForecast();
    bool startKmaRefresh();
    bool runKmaRefreshStep();
    bool fetchKmaCurrentStep();
    enum class StepResult : uint8_t {
        Continue,
        Success,
        Failed,
    };
    StepResult fetchKmaForecastStep();
    void finishKmaRefresh(bool ok);

    enum class RefreshPhase : uint8_t {
        Idle,
        Resolve,
        Current,
        Forecast,
    };

    Snapshot _snapshot;
    unsigned long _nextRefreshMs = 0;
    RefreshPhase _refreshPhase = RefreshPhase::Idle;
    String _pendingNcstDate;
    String _pendingNcstTime;
    String _pendingUltraDate;
    String _pendingUltraTime;
    int _pendingNx = 0;
    int _pendingNy = 0;
    float _pendingCurrentTemperature = 0.0F;
    float _pendingCurrentRain = 0.0F;
    float _pendingCurrentHumidity = -1.0F;
    int _pendingCurrentPty = 0;
    uint8_t _forecastAttempt = 0;
    IPAddress _kmaAddress;
    unsigned long _kmaAddressResolvedMs = 0;
    bool resolveKmaAddressStep();
};
