function rotationHandler() {
  return {
    loading: false,
    rotation: 0,
    brightness: 100,
    nightBrightnessEnabled: false,
    nightBrightness: 20,
    nightStartHour: 22,
    nightEndHour: 7,
    statusMsg: "",

    hourOptions() {
      return Array.from({ length: 24 }, (_, hour) => ({
        value: hour,
        label: `${String(hour).padStart(2, "0")}:00`,
      }));
    },

    clampHour(value, fallback) {
      const hour = Number(value);
      if (!Number.isInteger(hour)) {
        return fallback;
      }
      return Math.max(0, Math.min(23, hour));
    },

    fetchRotation() {
      this.loading = true;
      apiFetch("/api/v1/display/rotation")
        .then((r) => r.json())
        .then((data) => {
          this.rotation = Number.isInteger(data.rotation) ? data.rotation : 0;
          this.statusMsg = "";
        })
        .catch((err) => {
          this.statusMsg = "Failed to load rotation";
          console.error(err);
        })
        .finally(() => {
          this.loading = false;
        });
    },

    fetchBrightness() {
      this.loading = true;
      apiFetch("/api/v1/display/brightness")
        .then((r) => r.json())
        .then((data) => {
          this.brightness = Number.isInteger(data.brightness)
            ? data.brightness
            : 100;
          this.nightBrightnessEnabled = !!data.night_enabled;
          this.nightBrightness = Number.isInteger(data.night_brightness)
            ? data.night_brightness
            : 20;
          this.nightStartHour = this.clampHour(data.night_start_hour, 22);
          this.nightEndHour = this.clampHour(data.night_end_hour, 7);
          this.statusMsg = "";
        })
        .catch((err) => {
          this.statusMsg = "Failed to load brightness";
          console.error(err);
        })
        .finally(() => {
          this.loading = false;
        });
    },

    saveBrightness() {
      this.loading = true;
      const value = Math.max(5, Math.min(100, Number(this.brightness) || 100));
      const nightValue = Math.max(
        5,
        Math.min(100, Number(this.nightBrightness) || 20),
      );
      const payload = {
        brightness: value,
        night_enabled: !!this.nightBrightnessEnabled,
        night_brightness: nightValue,
        night_start_hour: this.clampHour(this.nightStartHour, 22),
        night_end_hour: this.clampHour(this.nightEndHour, 7),
      };

      apiFetch("/api/v1/display/brightness", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      })
        .then((r) => r.json())
        .then((data) => {
          if (data.status === "ok") {
            this.brightness = Number.isInteger(data.brightness)
              ? data.brightness
              : value;
            this.nightBrightnessEnabled = !!data.night_enabled;
            this.nightBrightness = Number.isInteger(data.night_brightness)
              ? data.night_brightness
              : nightValue;
            this.nightStartHour = this.clampHour(data.night_start_hour, 22);
            this.nightEndHour = this.clampHour(data.night_end_hour, 7);
            this.statusMsg = "Brightness updated";
          } else {
            this.statusMsg = data.message || "Failed to save brightness";
          }
        })
        .catch((err) => {
          this.statusMsg = "Failed to save brightness";
          console.error(err);
        })
        .finally(() => {
          this.loading = false;
        });
    },

    saveRotation() {
      this.loading = true;
      const payload = { rotation: Number(this.rotation) };

      apiFetch("/api/v1/display/rotation", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      })
        .then((r) => r.json())
        .then((data) => {
          if (data.status === "ok") {
            this.rotation = Number.isInteger(data.rotation)
              ? data.rotation
              : payload.rotation;
            this.statusMsg = "Rotation updated";
          } else {
            this.statusMsg = data.message || "Failed to save rotation";
          }
        })
        .catch((err) => {
          this.statusMsg = "Failed to save rotation";
          console.error(err);
        })
        .finally(() => {
          this.loading = false;
        });
    },

    init() {
      this.fetchRotation();
      this.fetchBrightness();
    },
  };
}
