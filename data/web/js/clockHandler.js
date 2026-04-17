function clockHandler() {
  return {
    loading: false,
    enabled: false,
    use24h: true,
    weatherEnabled: false,
    latitude: 37.566,
    longitude: 126.9784,
    timezone: "Asia/Seoul",
    locationName: "서울특별시",
    regionResults: [],
    selectedRegionResult: "",
    regionStatus: "",
    statusMsg: "",
    weatherStatus: "",
    weatherTimezone: "",
    currentSummary: "",
    forecastSummary: [],

    formatForecastHour(timestamp, timezone) {
      const date = new Date((timestamp || 0) * 1000);
      try {
        return new Intl.DateTimeFormat([], {
          hour: "2-digit",
          hour12: false,
          timeZone: timezone === "auto" ? undefined : timezone,
        }).format(date);
      } catch (_err) {
        return date.toISOString().slice(11, 13);
      }
    },

    async fetchClockConfig() {
      const response = await apiFetch("/api/v1/display/clock");
      const data = await response.json();
      this.enabled = !!data.enabled;
      this.use24h = data.use24h !== false;
    },

    async fetchWeatherConfig() {
      const response = await apiFetch("/api/v1/weather/config");
      const data = await response.json();
      this.weatherEnabled = !!data.enabled;
      this.latitude = Number(data.latitude || 0);
      this.longitude = Number(data.longitude || 0);
      this.timezone = data.timezone || "Asia/Seoul";
      this.locationName = this.toCityLevelLabel(data.location_name || "");
    },

    normalizeRegionLabel(label) {
      return (label || "").replace(/\s+/g, " ").trim();
    },

    toCityLevelLabel(label) {
      const normalized = this.normalizeRegionLabel(label);
      if (!normalized) {
        return "";
      }

      const parts = normalized.split(" ").filter(Boolean);
      const cityLike = parts.find((part) => /(?:시|구|군)$/.test(part));
      return cityLike || parts[parts.length - 1] || normalized;
    },

    scoreGeocodingResult(result, label) {
      const haystack = [
        result.name,
        result.admin1,
        result.admin2,
        result.admin3,
        result.admin4,
      ]
        .filter(Boolean)
        .join(" ");
      const normalizedHaystack = this.normalizeRegionLabel(haystack);
      const tokens = this.normalizeRegionLabel(label)
        .split(" ")
        .filter(Boolean);
      return tokens.reduce(
        (score, token) => score + (normalizedHaystack.includes(token) ? 1 : 0),
        0,
      );
    },

    buildRegionLabel(result) {
      const admin1 = this.normalizeRegionLabel(result.admin1 || "");
      const admin2 = this.normalizeRegionLabel(result.admin2 || "");
      const name = this.normalizeRegionLabel(result.name || "");

      if (admin2) {
        return admin2;
      }

      if (name) {
        return name;
      }

      return admin1;
    },

    async geocodeRegion(query, label) {
      const params = new URLSearchParams({
        name: query,
        count: "20",
        language: "ko",
        format: "json",
        countryCode: "KR",
      });
      const response = await fetch(
        `https://geocoding-api.open-meteo.com/v1/search?${params.toString()}`,
      );
      if (!response.ok) {
        throw new Error("region lookup failed");
      }
      const data = await response.json();
      const results = Array.isArray(data.results) ? data.results : [];
      if (!results.length) {
        return [];
      }
      return results
        .map((result) => ({
          label: this.buildRegionLabel(result),
          latitude: Number(result.latitude || 0),
          longitude: Number(result.longitude || 0),
          timezone: result.timezone || "Asia/Seoul",
          score: this.scoreGeocodingResult(result, label),
        }))
        .sort((left, right) => right.score - left.score)
        .slice(0, 12);
    },

    async searchRegions() {
      const label = this.normalizeRegionLabel(this.locationName);
      if (!label) {
        this.regionStatus = "Enter a Korean region name first";
        return;
      }

      this.loading = true;
      this.regionStatus = "Searching regions...";
      try {
        const parts = label.split(" ").filter(Boolean);
        const queries = [label];
        if (parts.length >= 2) {
          queries.push(parts.slice(-2).join(" "));
        }
        if (parts.length >= 1) {
          queries.push(parts[parts.length - 1]);
        }

        let matches = [];
        for (const query of queries) {
          matches = await this.geocodeRegion(query, label);
          if (matches.length) {
            break;
          }
        }

        if (!matches.length) {
          throw new Error("No matching Korean region found");
        }

        this.regionResults = matches;
        this.selectedRegionResult = matches[0].label;
        this.regionStatus = `Found ${matches.length} matching regions`;
      } catch (err) {
        this.regionResults = [];
        this.selectedRegionResult = "";
        this.regionStatus = err.message || "Failed to resolve region";
        console.error(err);
      } finally {
        this.loading = false;
      }
    },

    applySelectedRegion() {
      const selected = this.regionResults.find(
        (result) => result.label === this.selectedRegionResult,
      );
      if (!selected) {
        this.regionStatus = "Choose a search result first";
        return;
      }
      this.locationName = this.toCityLevelLabel(selected.label);
      this.latitude = selected.latitude;
      this.longitude = selected.longitude;
      this.timezone = selected.timezone || "Asia/Seoul";
      this.regionStatus = `Applied ${this.locationName}`;
    },

    async fetchWeatherStatus() {
      const response = await apiFetch("/api/v1/weather/status");
      const data = await response.json();
      this.weatherStatus = data.status || "";
      this.weatherTimezone = data.timezone || this.timezone || "Asia/Seoul";
      if (!data.hasData) {
        this.currentSummary = "";
        this.forecastSummary = [];
        return;
      }

      const raining = data.isRaining ? "Raining" : "Dry";
      const mm = Number(data.currentPrecipitation || 0).toFixed(1);
      this.currentSummary = `${data.currentTemperature}C / ${raining} / ${mm}mm`;
      const tz = data.timezone || this.timezone || "UTC";
      this.forecastSummary = (data.forecast || []).map((entry) => {
        const hour = this.formatForecastHour(entry.time, tz);
        return `${hour}h ${entry.temperature}C ${Number(
          entry.precipitation || 0,
        ).toFixed(1)}mm`;
      });
    },

    async fetchSettings() {
      this.loading = true;
      try {
        await this.fetchClockConfig();
        await this.fetchWeatherConfig();
        await this.fetchWeatherStatus();
        this.statusMsg = "";
      } catch (err) {
        this.statusMsg = "Failed to load dashboard settings";
        console.error(err);
      } finally {
        this.loading = false;
      }
    },

    async saveSettings() {
      this.loading = true;
      try {
        const clockResponse = await apiFetch("/api/v1/display/clock", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            enabled: this.enabled,
            use24h: this.use24h,
          }),
        });
        const clockData = await clockResponse.json();
        if (clockData.status !== "ok") {
          throw new Error(clockData.message || "clock save failed");
        }

        const weatherResponse = await apiFetch("/api/v1/weather/config", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            enabled: this.weatherEnabled,
            latitude: Number(this.latitude),
            longitude: Number(this.longitude),
            timezone: (this.timezone || "Asia/Seoul").trim(),
            location_name: this.toCityLevelLabel(this.locationName),
          }),
        });
        const weatherData = await weatherResponse.json();
        if (weatherData.status !== "ok") {
          throw new Error(weatherData.message || "weather save failed");
        }

        this.enabled = !!clockData.enabled;
        this.use24h = clockData.use24h !== false;
        this.weatherEnabled = !!weatherData.enabled;
        this.latitude = Number(weatherData.latitude || 0);
        this.longitude = Number(weatherData.longitude || 0);
        this.timezone = weatherData.timezone || "Asia/Seoul";
        this.locationName = this.toCityLevelLabel(
          weatherData.location_name || this.locationName,
        );
        this.statusMsg = "Dashboard settings updated";
        await this.fetchWeatherStatus();
      } catch (err) {
        this.statusMsg = err.message || "Failed to save dashboard settings";
        console.error(err);
      } finally {
        this.loading = false;
      }
    },

    init() {
      this.fetchSettings();
    },
  };
}
