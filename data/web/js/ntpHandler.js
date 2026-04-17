function ntpHandler() {
  return {
    loading: false,
    lastStatus: "",
    lastSyncTime: 0,
    lastOk: false,
    ntpServer: "",
    timezoneRegion: "Asia/Seoul",
    timezoneOffsetMinutes: 540,
    timezoneOptions: [
      { value: "Asia/Seoul", label: "Korea (UTC+9) - Asia/Seoul" },
      { value: "Asia/Tokyo", label: "Japan (UTC+9) - Asia/Tokyo" },
      { value: "Asia/Shanghai", label: "China (UTC+8) - Asia/Shanghai" },
      { value: "Asia/Singapore", label: "Singapore (UTC+8) - Asia/Singapore" },
      { value: "Asia/Bangkok", label: "Thailand (UTC+7) - Asia/Bangkok" },
      { value: "Europe/London", label: "UK (UTC+0) - Europe/London" },
      { value: "Europe/Berlin", label: "Central Europe (UTC+1) - Europe/Berlin" },
      { value: "UTC", label: "UTC (UTC+0)" },
      { value: "America/New_York", label: "US Eastern (UTC-5) - America/New_York" },
      { value: "America/Chicago", label: "US Central (UTC-6) - America/Chicago" },
      { value: "America/Denver", label: "US Mountain (UTC-7) - America/Denver" },
      { value: "America/Los_Angeles", label: "US Pacific (UTC-8) - America/Los_Angeles" },
      { value: "Australia/Sydney", label: "Australia East (UTC+10) - Australia/Sydney" },
    ],

    get timezoneDescription() {
      const sign = this.timezoneOffsetMinutes >= 0 ? "+" : "";
      return `Selected offset: UTC${sign}${this.timezoneOffsetMinutes / 60}`;
    },

    offsetForRegion(region) {
      const table = {
        "Asia/Seoul": 540,
        "Asia/Tokyo": 540,
        "Asia/Shanghai": 480,
        "Asia/Singapore": 480,
        "Asia/Bangkok": 420,
        "Europe/London": 0,
        "Europe/Berlin": 60,
        UTC: 0,
        "America/New_York": -300,
        "America/Chicago": -360,
        "America/Denver": -420,
        "America/Los_Angeles": -480,
        "Australia/Sydney": 600,
      };
      return table[region] ?? 540;
    },

    fetchStatus() {
      apiFetch("/api/v1/ntp/status")
        .then((r) => r.json())
        .then((data) => {
          this.lastStatus = data.lastStatus || "";
          this.lastSyncTime = data.lastSyncTime || 0;
          this.lastOk = data.lastOk || false;
        })
        .catch((err) => {
          this.lastStatus = "error fetching status";
          console.error(err);
        });
    },

    syncNow() {
      this.loading = true;
      apiFetch("/api/v1/ntp/sync", { method: "POST" })
        .then((r) => r.json())
        .then((data) => {
          this.lastStatus = data.lastStatus || "";
          this.lastSyncTime = data.lastSyncTime || 0;
          this.lastOk = data.status === "ok";
        })
        .catch((err) => {
          this.lastStatus = "sync failed";
          console.error(err);
        })
        .finally(() => {
          this.loading = false;
        });
    },

    fetchConfig() {
      apiFetch("/api/v1/ntp/config")
        .then((r) => r.json())
        .then((data) => {
          this.ntpServer = data.ntp_server || "";
          this.timezoneRegion = data.timezone_region || "Asia/Seoul";
          this.timezoneOffsetMinutes = this.offsetForRegion(this.timezoneRegion);
        })
        .catch((err) => {
          console.error("failed to fetch ntp config", err);
        });
    },

    saveConfig() {
      const payload = {
        ntp_server: (this.ntpServer || "").trim(),
        timezone_region: this.timezoneRegion,
      };
      apiFetch("/api/v1/ntp/config", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      })
        .then((r) => r.json())
        .then((data) => {
          if (data.status === "ok") {
            this.ntpServer = data.ntp_server || this.ntpServer || "pool.ntp.org";
            this.timezoneRegion = data.timezone_region || this.timezoneRegion;
            this.timezoneOffsetMinutes = this.offsetForRegion(this.timezoneRegion);
            this.lastStatus = data.message || "Config saved";
            this.fetchStatus();
          } else {
            this.lastStatus = data.message || "save failed";
          }
        })
        .catch((err) => {
          this.lastStatus = "save failed";
          console.error(err);
        });
    },

    humanTime(ts) {
      if (!ts || ts === 0) return "never";
      const d = new Date(ts * 1000);
      return d.toLocaleString();
    },

    init() {
      this.timezoneOffsetMinutes = this.offsetForRegion(this.timezoneRegion);
      this.fetchStatus();
      this.fetchConfig();
    },
  };
}
