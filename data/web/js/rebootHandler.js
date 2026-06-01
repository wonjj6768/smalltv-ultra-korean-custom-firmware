function rebootHandler() {
  return {
    loading: false,
    message: "",
    async reboot() {
      this.loading = true;
      this.message = "Rebooting...";
      try {
        const res = await apiFetch("/api/v1/reboot", { method: "POST" });
        if (res.ok) {
          this.message = "Device is rebooting...";
        } else {
          this.message = "Reboot failed!";
        }
      } catch (e) {
        this.message = "Error: " + e;
      }
      setTimeout(() => {
        this.loading = false;
      }, 5000);
    },
    async factoryReset() {
      if (
        !confirm(
          "Factory reset clears WiFi and dashboard settings. Continue?",
        )
      ) {
        return;
      }

      this.loading = true;
      this.message = "Factory reset...";
      try {
        const res = await apiFetch("/api/v1/factory-reset", {
          method: "POST",
        });
        if (res.ok) {
          this.message =
            "Settings cleared. Device will reboot into setup mode.";
        } else {
          this.message = "Factory reset failed!";
        }
      } catch (e) {
        this.message = "Error: " + e;
      }
      setTimeout(() => {
        this.loading = false;
      }, 5000);
    },
  };
}
