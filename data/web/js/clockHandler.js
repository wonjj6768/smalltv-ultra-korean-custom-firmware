function clockHandler() {
  return {
    loading: false,
    enabled: false,
    use24h: true,
    weatherEnabled: false,
    latitude: 37.566,
    longitude: 126.9784,
    kmaGridX: 60,
    kmaGridY: 127,
    timezone: "Asia/Seoul",
    locationName: "서울시",
    kmaApiKey: "",
    kmaKeyStatus: "",
    regionResults: [],
    selectedRegionResult: "",
    regionStatus: "",
    kmaRegions: [],
    statusMsg: "",
    weatherStatus: "",
    weatherSource: "",
    weatherTimezone: "",
    currentSummary: "",
    forecastSummary: [],

    formatForecastHour(timestamp) {
      const date = new Date((timestamp || 0) * 1000);
      return String(date.getUTCHours()).padStart(2, "0");
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
      this.kmaGridX = Number(data.kma_grid_x || 60);
      this.kmaGridY = Number(data.kma_grid_y || 127);
      this.timezone = data.timezone || "Asia/Seoul";
      this.locationName = this.normalizeRegionLabel(data.location_name || "");
      this.kmaApiKey = "";
      this.kmaKeyStatus = data.kma_api_key_set
        ? "KMA APIHub key is saved."
        : "Required for KMA APIHub weather.";
    },

    normalizeRegionLabel(label) {
      return (label || "").replace(/\s+/g, " ").trim();
    },

    compactRegionLabel(label) {
      return this.normalizeRegionLabel(label)
        .replace(/특별자치시/g, "시")
        .replace(/특별시/g, "시")
        .replace(/광역시/g, "시")
        .replace(/특별자치도/g, "도");
    },

    toCityLevelLabel(label) {
      const normalized = this.compactRegionLabel(label);
      if (!normalized) {
        return "";
      }

      const parts = normalized.split(" ").filter(Boolean);
      const filtered = parts.filter(
        (part) => !/(?:특별시|광역시|특별자치시|특별자치도|도)$/.test(part),
      );
      const sigungu = [];
      for (const part of filtered) {
        if (/(?:시|군|구)$/.test(part)) {
          sigungu.push(part);
          if (sigungu.length >= 2 || /(?:군|구)$/.test(part)) {
            break;
          }
        }
      }
      return sigungu.join(" ") || filtered[0] || parts[parts.length - 1] || normalized;
    },

    scoreRegionResult(result, label) {
      const haystack = [result.label, result.full].filter(Boolean).join(" ");
      const normalizedHaystack = this.normalizeRegionLabel(haystack);
      const tokens = this.normalizeRegionLabel(label)
        .split(" ")
        .filter(Boolean);
      return tokens.reduce(
        (score, token) => score + (normalizedHaystack.includes(token) ? 1 : 0),
        0,
      );
    },

    regionMatches(result, label) {
      const normalizedLabel = this.normalizeRegionLabel(label);
      const full = this.normalizeRegionLabel(result.full);
      const shortLabel = this.normalizeRegionLabel(result.label);
      return (
        full === normalizedLabel ||
        shortLabel === normalizedLabel ||
        full.includes(normalizedLabel) ||
        shortLabel.includes(normalizedLabel)
      );
    },

    applyRegionData(region) {
      this.locationName = this.toCityLevelLabel(region.label || region.full || this.locationName);
      this.latitude = Number(region.latitude || region.lat || 0);
      this.longitude = Number(region.longitude || region.lon || 0);
      this.kmaGridX = Number(region.x || 0);
      this.kmaGridY = Number(region.y || 0);
      this.timezone = region.timezone || "Asia/Seoul";
    },

    async syncTypedRegion() {
      const label = this.normalizeRegionLabel(this.locationName);
      if (!label) {
        return;
      }

      const regions = await this.loadKmaRegions();
      const matches = regions
        .map((region) => ({
          ...region,
          score: this.scoreRegionResult(region, label),
        }))
        .filter((region) => this.regionMatches(region, label) || region.score > 0)
        .sort((left, right) => {
          const leftExact =
            this.normalizeRegionLabel(left.full) === label ||
            this.normalizeRegionLabel(left.label) === label;
          const rightExact =
            this.normalizeRegionLabel(right.full) === label ||
            this.normalizeRegionLabel(right.label) === label;
          if (leftExact !== rightExact) {
            return leftExact ? -1 : 1;
          }
          return right.score - left.score;
        });

      if (matches.length) {
        this.applyRegionData(matches[0]);
        this.regionStatus = `Applied ${matches[0].full}`;
      }
    },

    async loadKmaRegions() {
      if (this.kmaRegions.length) {
        return this.kmaRegions;
      }
      const response = await fetch("./kma_regions.json?v=20260601b");
      if (!response.ok) {
        throw new Error("region lookup failed");
      }
      const data = await response.json();
      this.kmaRegions = Array.isArray(data) ? data : [];
      return this.kmaRegions;
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
        const regions = await this.loadKmaRegions();
        const matches = regions
          .map((result) => ({
            ...result,
            latitude: Number(result.lat || 0),
            longitude: Number(result.lon || 0),
            timezone: "Asia/Seoul",
            score: this.scoreRegionResult(result, label),
          }))
          .filter(
            (result) =>
              result.score > 0 || this.regionMatches(result, label),
          )
          .sort((left, right) => right.score - left.score)
          .slice(0, 12);

        if (!matches.length) {
          throw new Error("No matching KMA region found");
        }

        this.regionResults = matches;
        this.selectedRegionResult = matches[0].full;
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
        (result) => result.full === this.selectedRegionResult,
      );
      if (!selected) {
        this.regionStatus = "Choose a search result first";
        return;
      }
      this.loading = true;
      this.applyRegionData(selected);
      this.regionStatus = `Applied ${this.locationName}`;
      this.loading = false;
    },

    async fetchWeatherStatus() {
      const response = await apiFetch("/api/v1/weather/status");
      const data = await response.json();
      this.weatherStatus = data.status || "";
      this.weatherSource = data.source || "KMA APIHub";
      this.weatherTimezone = data.timezone || this.timezone || "Asia/Seoul";
      if (!data.hasData) {
        this.currentSummary = "";
        this.forecastSummary = [];
        return;
      }

      const precipitation = Number(data.currentPrecipitation || 0);
      const humidity = Number(data.currentHumidity || 0).toFixed(0);
      const rainSummary =
        data.isRaining || precipitation > 0
          ? `강수 ${precipitation.toFixed(1)}mm`
          : "비 없음";
      this.currentSummary = `${data.currentTemperature}℃ · ${rainSummary} · 습도 ${humidity}%`;
      this.forecastSummary = (data.forecast || []).map((entry) => {
        const hour = this.formatForecastHour(entry.time);
        const forecastPrecipitation = Number(entry.precipitation || 0);
        const secondary =
          forecastPrecipitation > 0
            ? `강수 ${forecastPrecipitation.toFixed(1)}mm`
            : `습도 ${Number(entry.humidity || 0).toFixed(0)}%`;
        return `${hour}시 ${entry.temperature}℃ · ${secondary}`;
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

        if (this.weatherEnabled) {
          await this.syncTypedRegion();
        }

        const weatherResponse = await apiFetch("/api/v1/weather/config", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            enabled: this.weatherEnabled,
            latitude: Number(this.latitude),
            longitude: Number(this.longitude),
            kma_grid_x: Number(this.kmaGridX),
            kma_grid_y: Number(this.kmaGridY),
            timezone: (this.timezone || "Asia/Seoul").trim(),
            location_name: this.normalizeRegionLabel(this.locationName),
            ...(this.kmaApiKey.trim()
              ? { kma_api_key: this.kmaApiKey.trim() }
              : {}),
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
        this.kmaGridX = Number(weatherData.kma_grid_x || this.kmaGridX || 60);
        this.kmaGridY = Number(weatherData.kma_grid_y || this.kmaGridY || 127);
        this.timezone = weatherData.timezone || "Asia/Seoul";
        this.kmaApiKey = "";
        this.kmaKeyStatus = weatherData.kma_api_key_set
          ? "KMA APIHub key is saved."
          : "Required for KMA APIHub weather.";
        this.locationName = this.normalizeRegionLabel(weatherData.location_name || this.locationName);
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
