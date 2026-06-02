function rotationHandler() {
  return {
    loading: false,
    rotation: 0,
    brightness: 100,
    statusMsg: "",

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
      const payload = { brightness: value };

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
