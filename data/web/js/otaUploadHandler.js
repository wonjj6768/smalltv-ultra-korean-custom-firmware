function otaUploadHandler() {
  const releaseApi =
    "https://api.github.com/repos/wonjj6768/smalltv-ultra-korean-custom-firmware/releases/latest";

  return {
    uploading: false,
    downloading: false,
    checking: false,
    uploadMessage: "",
    updateMessage: "",
    uploadType: "firmware",
    progress: 0,
    etaText: "",
    downloadProgress: 0,
    downloadEtaText: "",
    downloadMessage: "",
    xhr: null,
    downloadXhr: null,
    currentVersion: "",
    latestVersion: "",
    latestUrl: "",
    latestAssets: {},
    updateAvailable: false,

    isSameOrNewer(current, latest) {
      if (!current || !latest) {
        return false;
      }
      return current === latest || current.startsWith(latest + "-");
    },

    findAsset(release, name) {
      return (release.assets || []).find((asset) => asset.name === name) || null;
    },

    async init() {
      window.addEventListener("beforeunload", (event) => {
        if (!this.uploading && !this.downloading) {
          return;
        }
        event.preventDefault();
        event.returnValue = "";
      });
      await this.checkLatestRelease();
    },

    async setDeviceUpdateAvailable(available) {
      this.updateAvailable = available;
      try {
        await apiFetch("/api/v1/system/update-available", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ available }),
        });
      } catch (err) {
        console.error(err);
      }
    },

    async checkLatestRelease() {
      this.checking = true;
      this.updateMessage = "Checking latest release...";

      try {
        const versionResp = await apiFetch("/api/v1/system/version");
        const versionData = await versionResp.json();
        this.currentVersion = versionData.version || "";

        const releaseResp = await fetch(releaseApi, {
          headers: { Accept: "application/vnd.github+json" },
        });
        if (!releaseResp.ok) {
          await this.setDeviceUpdateAvailable(false);
          this.updateMessage = "No GitHub release found yet.";
          return;
        }

        const release = await releaseResp.json();
        this.latestVersion = release.tag_name || "";
        this.latestUrl = release.html_url || "";
        this.latestAssets = {
          firmware: this.findAsset(release, "firmware.bin"),
          fs: this.findAsset(release, "littlefs.bin"),
        };
        const hasUpdate = !this.isSameOrNewer(
          this.currentVersion,
          this.latestVersion,
        );
        await this.setDeviceUpdateAvailable(hasUpdate);

        if (!this.latestAssets.firmware || !this.latestAssets.fs) {
          this.updateMessage =
            "Latest release exists, but firmware.bin or littlefs.bin is missing.";
          return;
        }

        this.updateMessage = this.updateAvailable
          ? "Update available. Apply firmware first, then LittleFS."
          : "Already on this release or a newer development build.";
      } catch (err) {
        await this.setDeviceUpdateAvailable(false);
        this.updateMessage = "Failed to check update";
        console.error(err);
      } finally {
        this.checking = false;
      }
    },

    async uploadFile() {
      const fileInput = this.$refs.fileInput;
      if (!fileInput.files.length) {
        this.uploadMessage = "Please select a file";
        return;
      }

      const file = fileInput.files[0];
      if (!file.name.toLowerCase().endsWith(".bin")) {
        this.uploadMessage = "Only .bin files are allowed";
        return;
      }

      const endpoint =
        this.uploadType === "firmware" ? "/api/v1/ota/fw" : "/api/v1/ota/fs";
      await this.uploadBlob(file, endpoint);
    },

    async applyReleaseAsset(kind) {
      const asset = this.latestAssets[kind];
      if (!asset || !asset.browser_download_url) {
        this.uploadMessage = "Release asset is missing";
        return;
      }

      const endpoint = kind === "firmware" ? "/api/v1/ota/fw" : "/api/v1/ota/fs";
      this.uploadMessage = "";
      this.downloadMessage = "Downloading " + asset.name + "... Keep this tab open.";

      try {
        const blob = await this.downloadReleaseAsset(asset);
        const file = new File([blob], asset.name, {
          type: "application/octet-stream",
        });
        this.downloadMessage = "Download finished. Uploading to device...";
        await this.uploadBlob(file, endpoint);
      } catch (err) {
        this.uploadMessage =
          "Browser could not download the release asset. Use the release link below.";
        this.downloadMessage = "Download failed.";
        this.downloadXhr = null;
        console.error(err);
      }
    },

    async downloadReleaseAsset(asset) {
      this.downloading = true;
      this.downloadProgress = 0;
      this.downloadEtaText = "";

      return new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        const startTime = Date.now();
        this.downloadXhr = xhr;

        xhr.responseType = "blob";
        xhr.onprogress = (e) => {
          if (e.lengthComputable) {
            const percent = Math.round((e.loaded / e.total) * 100);
            this.downloadProgress = percent;
            const elapsed = (Date.now() - startTime) / 1000;
            const rate = e.loaded / Math.max(elapsed, 0.001);
            const remaining = e.total - e.loaded;
            this.downloadEtaText =
              rate > 0 ? "ETA: " + Math.round(remaining / rate) + "s" : "";
          } else {
            this.downloadProgress = Math.min(99, this.downloadProgress + 1);
            this.downloadEtaText = "Downloading...";
          }
        };

        xhr.onload = () => {
          this.downloadXhr = null;
          this.downloading = false;
          if (xhr.status >= 200 && xhr.status < 300) {
            this.downloadProgress = 100;
            this.downloadEtaText = "";
            resolve(xhr.response);
            return;
          }
          reject(new Error("Download failed: " + xhr.status));
        };

        xhr.onerror = () => {
          this.downloadXhr = null;
          this.downloading = false;
          reject(new Error("Download error"));
        };

        xhr.onabort = () => {
          this.downloadXhr = null;
          this.downloading = false;
          reject(new Error("Download canceled"));
        };

        xhr.open("GET", asset.browser_download_url);
        xhr.send();
      });
    },

    async uploadBlob(file, endpoint) {
      this.uploading = true;
      this.uploadMessage = "Uploading " + file.name + " to device... Keep this tab open.";
      this.progress = 0;
      this.etaText = "";

      const formData = new FormData();
      formData.append("file", file);

      this.xhr = new XMLHttpRequest();
      const startTime = Date.now();

      this.xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) {
          const percent = Math.round((e.loaded / e.total) * 100);
          this.progress = percent;
          const elapsed = (Date.now() - startTime) / 1000;
          const rate = e.loaded / Math.max(elapsed, 0.001);
          const remaining = e.total - e.loaded;
          this.etaText = rate > 0 ? "ETA: " + Math.round(remaining / rate) + "s" : "";
        } else {
          this.progress = Math.min(99, this.progress + 1);
        }
      };

      this.xhr.onload = async () => {
        this.uploading = false;
        try {
          if (this.xhr.status >= 200 && this.xhr.status < 300) {
            const data = JSON.parse(this.xhr.responseText || "{}");
            this.uploadMessage = data.message || "Upload finished";
            this.progress = 100;
            this.etaText = "";
          } else {
            this.uploadMessage =
              "Error: " + (this.xhr.responseText || this.xhr.status);
          }
        } catch (e) {
          this.uploadMessage = "Upload finished";
        }
        this.xhr = null;
      };

      this.xhr.onerror = () => {
        this.uploading = false;
        this.uploadMessage = "Upload error";
        this.xhr = null;
      };

      this.xhr.onabort = () => {
        this.uploading = false;
        this.uploadMessage = "Upload canceled";
        this.xhr = null;
      };

      this.xhr.open("POST", endpoint);
      this.xhr.send(formData);
    },

    async cancelUpload() {
      if (this.downloadXhr) {
        try {
          this.downloadXhr.abort();
        } catch (e) {
          // ignore
        }
      }

      if (this.xhr) {
        try {
          this.xhr.abort();
        } catch (e) {
          // ignore
        }
      }

      try {
        await apiFetch("/api/v1/ota/cancel", { method: "POST" });
      } catch (e) {
        // ignore
      }
      this.uploading = false;
      this.downloading = false;
      this.progress = 0;
      this.etaText = "";
      this.downloadProgress = 0;
      this.downloadEtaText = "";
    },
  };
}
