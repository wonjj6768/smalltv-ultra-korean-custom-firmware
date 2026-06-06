#pragma once

#include <array>
#include <ctime>

#include "Arduino.h"

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
        int currentWeatherCode = -1;
        time_t currentTime = 0;
        time_t lastUpdated = 0;
        long utcOffsetSeconds = 0;
        String timezone;
        String status;
        String source;
        std::array<ForecastEntry, 5> forecast{};
    };

    auto begin() -> void {}
    auto loop() -> void {}
    auto refreshNow() -> bool { return true; }
    auto requestRefresh() -> void {}
    auto getSnapshot() const -> const Snapshot& { return snapshot_; }
    auto mutableSnapshot() -> Snapshot& { return snapshot_; }
    auto setSnapshot(const Snapshot& snapshot) -> void { snapshot_ = snapshot; }

   private:
    Snapshot snapshot_;
};
