function rotationHandler() {
  return {
    loading: false,
    rotation: 0,
    brightness: 100,
    nightBrightnessEnabled: false,
    nightBrightness: 20,
    nightStartTime: "22:00",
    nightEndTime: "07:00",
    statusMsg: "",

    minuteToTime(value) {
      const minute = Math.max(0, Math.min(1439, Number(value) || 0));
      const hour = Math.floor(minute / 60);
      const min = minute % 60;
      return `${String(hour).padStart(2, "0")}:${String(min).padStart(2, "0")}`;
    },

    timeToMinute(value) {
      const parts = String(value || "00:00").split(":");
      const hour = Math.max(0, Math.min(23, Number(parts[0]) || 0));
      const minute = Math.max(0, Math.min(59, Number(parts[1]) || 0));
      return hour * 60 + minute;
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
          this.nightStartTime = this.minuteToTime(data.night_start_minute);
          this.nightEndTime = this.minuteToTime(data.night_end_minute);
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
        night_start_minute: this.timeToMinute(this.nightStartTime),
        night_end_minute: this.timeToMinute(this.nightEndTime),
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
            this.nightStartTime = this.minuteToTime(data.night_start_minute);
            this.nightEndTime = this.minuteToTime(data.night_end_minute);
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
