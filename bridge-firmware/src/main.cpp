#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <Updater.h>

namespace {

constexpr uint32_t SERIAL_BAUD_RATE = 115200;
constexpr const char* AP_SSID = "GeekMagic";
constexpr const char* AP_PASSWORD = "";
constexpr const char* UPDATE_PATH = "/bridgeupdate";

constexpr int16_t LCD_W = 240;
constexpr int16_t LCD_H = 240;
constexpr uint8_t LCD_ROTATION = 0;
constexpr int8_t LCD_MOSI_GPIO = 13;
constexpr int8_t LCD_SCK_GPIO = 14;
constexpr int8_t LCD_DC_GPIO = 0;
constexpr int8_t LCD_RST_GPIO = 2;
constexpr uint8_t LCD_SPI_MODE = SPI_MODE3;
constexpr uint32_t LCD_SPI_HZ = 40000000;
constexpr int8_t LCD_BACKLIGHT_GPIO = 5;
constexpr bool LCD_BACKLIGHT_ACTIVE_LOW = true;

constexpr uint16_t LCD_BLACK = 0x0000;
constexpr uint16_t LCD_WHITE = 0xFFFF;
constexpr uint16_t LCD_GREEN = 0x07E0;
constexpr uint16_t LCD_RED = 0xF800;

constexpr uint32_t LCD_HARDWARE_RESET_DELAY_MS = 120;
constexpr uint32_t LCD_BEGIN_DELAY_MS = 10;

constexpr uint8_t ST7789_SLEEP_OUT = 0x11;
constexpr uint8_t ST7789_PORCH = 0xB2;
constexpr uint8_t ST7789_TEARING_EFFECT = 0x35;
constexpr uint8_t ST7789_MEMORY_ACCESS_CONTROL = 0x36;
constexpr uint8_t ST7789_COLORMODE = 0x3A;
constexpr uint8_t ST7789_POWER_B7 = 0xB7;
constexpr uint8_t ST7789_POWER_BB = 0xBB;
constexpr uint8_t ST7789_POWER_C0 = 0xC0;
constexpr uint8_t ST7789_POWER_C2 = 0xC2;
constexpr uint8_t ST7789_POWER_C3 = 0xC3;
constexpr uint8_t ST7789_POWER_C4 = 0xC4;
constexpr uint8_t ST7789_POWER_C6 = 0xC6;
constexpr uint8_t ST7789_POWER_D0 = 0xD0;
constexpr uint8_t ST7789_POWER_D6 = 0xD6;
constexpr uint8_t ST7789_GAMMA_POS = 0xE0;
constexpr uint8_t ST7789_GAMMA_NEG = 0xE1;
constexpr uint8_t ST7789_GAMMA_CTRL = 0xE4;
constexpr uint8_t ST7789_INVERSION_ON = 0x21;
constexpr uint8_t ST7789_DISPLAY_ON = 0x29;
constexpr uint8_t ST7789_PORCH_PARAM_HS = 0x1F;
constexpr uint8_t ST7789_PORCH_PARAM_VS = 0x1F;
constexpr uint8_t ST7789_PORCH_PARAM_DUMMY = 0x00;
constexpr uint8_t ST7789_PORCH_PARAM_HBP = 0x33;
constexpr uint8_t ST7789_PORCH_PARAM_VBP = 0x33;
constexpr uint8_t ST7789_TEARING_PARAM_OFF = 0x00;
constexpr uint8_t ST7789_MADCTL_PARAM_DEFAULT = 0x00;
constexpr uint8_t ST7789_COLORMODE_RGB565 = 0x05;
constexpr uint8_t ST7789_B7_PARAM_DEFAULT = 0x00;
constexpr uint8_t ST7789_BB_PARAM_VOLTAGE = 0x36;
constexpr uint8_t ST7789_C0_PARAM_1 = 0x2C;
constexpr uint8_t ST7789_C2_PARAM_1 = 0x01;
constexpr uint8_t ST7789_C3_PARAM_1 = 0x13;
constexpr uint8_t ST7789_C4_PARAM_1 = 0x20;
constexpr uint8_t ST7789_C6_PARAM_1 = 0x13;
constexpr uint8_t ST7789_D0_PARAM_1 = 0xA4;
constexpr uint8_t ST7789_D0_PARAM_2 = 0xA1;
constexpr uint8_t ST7789_D6_PARAM_1 = 0xA1;
constexpr uint32_t ST7789_SLEEP_DELAY_MS = 120;

constexpr uint8_t ST7789_ADDR_START_HIGH = 0x00;
constexpr uint8_t ST7789_ADDR_START_LOW = 0x00;
constexpr uint8_t ST7789_ADDR_END_HIGH = 0x00;
constexpr uint8_t ST7789_ADDR_END_LOW = 0xEF;

constexpr uint8_t ST7789_GAMMA_POS_DATA[] = {0xD0, 0x08, 0x11, 0x08, 0x0C, 0x15, 0x39,
                                              0x33, 0x50, 0x36, 0x13, 0x14, 0x29, 0x2D};
constexpr uint8_t ST7789_GAMMA_NEG_DATA[] = {0xD0, 0x08, 0x10, 0x08, 0x06, 0x06, 0x39,
                                              0x44, 0x51, 0x0B, 0x16, 0x14, 0x2F, 0x31};
constexpr uint8_t ST7789_GAMMA_CTRL_DATA[] = {0x1D, 0x00, 0x00};

Arduino_HWSPI g_lcdBus(LCD_DC_GPIO, -1, &SPI, true);
Arduino_ST7789 g_lcd(&g_lcdBus, -1, 0, true, LCD_W, LCD_H);
ESP8266WebServer g_server(80);

bool g_updateFailed = false;
bool g_updateFinished = false;
String g_updateStatus;
unsigned long g_restartAtMs = 0;

const char BRIDGE_PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>OTA Bridge</title>
  <style>
    body { font-family: sans-serif; background: #111; color: #eee; margin: 0; padding: 24px; }
    main { max-width: 520px; margin: 0 auto; }
    h1 { margin-top: 0; }
    .card { background: #1b1b1b; border: 1px solid #333; border-radius: 12px; padding: 16px; }
    input, button { width: 100%; margin-top: 12px; }
    button { padding: 12px; font-size: 16px; }
    code { color: #8fd3ff; }
  </style>
</head>
<body>
  <main>
    <div class="card">
      <h1>OTA Bridge</h1>
      <p>Upload <code>firmware.bin</code> only.</p>
      <form method="POST" action="/bridgeupdate" enctype="multipart/form-data">
        <input type="file" name="update" accept=".bin" required>
        <button type="submit">Upload firmware.bin</button>
      </form>
    </div>
  </main>
</body>
</html>
)HTML";

auto drawBridgeScreen(const String& title, const String& line1, const String& line2, const String& line3,
                      const String& line4, const String& line5, const String& line6, const String& line7,
                      uint16_t accent = LCD_WHITE) -> void {
    g_lcd.fillScreen(LCD_BLACK);
    g_lcd.setTextColor(LCD_WHITE, LCD_BLACK);
    g_lcd.setTextSize(3);
    g_lcd.setCursor(14, 28);
    g_lcd.print(title);

    g_lcd.setTextColor(accent, LCD_BLACK);
    g_lcd.setTextSize(2);
    g_lcd.setCursor(14, 82);
    g_lcd.print(line1);
    g_lcd.setCursor(14, 104);
    g_lcd.print(line2);
    g_lcd.setCursor(14, 126);
    g_lcd.print(line3);
    g_lcd.setCursor(14, 148);
    g_lcd.print(line4);
    g_lcd.setCursor(14, 170);
    g_lcd.print(line5);
    g_lcd.setCursor(14, 196);
    g_lcd.print(line6);
    g_lcd.setCursor(14, 218);
    g_lcd.print(line7);
}

auto lcdBacklightOn() -> void {
    pinMode(static_cast<uint8_t>(LCD_BACKLIGHT_GPIO), OUTPUT);
    digitalWrite(static_cast<uint8_t>(LCD_BACKLIGHT_GPIO), LCD_BACKLIGHT_ACTIVE_LOW ? LOW : HIGH);
}

auto lcdHardReset() -> void {
    pinMode(static_cast<uint8_t>(LCD_RST_GPIO), OUTPUT);
    digitalWrite(static_cast<uint8_t>(LCD_RST_GPIO), HIGH);
    delay(LCD_HARDWARE_RESET_DELAY_MS);
    digitalWrite(static_cast<uint8_t>(LCD_RST_GPIO), LOW);
    delay(LCD_HARDWARE_RESET_DELAY_MS);
    digitalWrite(static_cast<uint8_t>(LCD_RST_GPIO), HIGH);
    delay(LCD_HARDWARE_RESET_DELAY_MS);
}

auto stWriteCommand(uint8_t value) -> void { g_lcdBus.writeCommand(value); }
auto stWriteData(uint8_t value) -> void { g_lcdBus.write(value); }

auto lcdRunVendorInit() -> void {
    g_lcdBus.beginWrite();

    stWriteCommand(ST7789_SLEEP_OUT);
    delay(ST7789_SLEEP_DELAY_MS);

    stWriteCommand(ST7789_PORCH);
    stWriteData(ST7789_PORCH_PARAM_HS);
    stWriteData(ST7789_PORCH_PARAM_VS);
    stWriteData(ST7789_PORCH_PARAM_DUMMY);
    stWriteData(ST7789_PORCH_PARAM_HBP);
    stWriteData(ST7789_PORCH_PARAM_VBP);

    stWriteCommand(ST7789_TEARING_EFFECT);
    stWriteData(ST7789_TEARING_PARAM_OFF);

    stWriteCommand(ST7789_MEMORY_ACCESS_CONTROL);
    stWriteData(ST7789_MADCTL_PARAM_DEFAULT);

    stWriteCommand(ST7789_COLORMODE);
    stWriteData(ST7789_COLORMODE_RGB565);

    stWriteCommand(ST7789_POWER_B7);
    stWriteData(ST7789_B7_PARAM_DEFAULT);
    stWriteCommand(ST7789_POWER_BB);
    stWriteData(ST7789_BB_PARAM_VOLTAGE);
    stWriteCommand(ST7789_POWER_C0);
    stWriteData(ST7789_C0_PARAM_1);
    stWriteCommand(ST7789_POWER_C2);
    stWriteData(ST7789_C2_PARAM_1);
    stWriteCommand(ST7789_POWER_C3);
    stWriteData(ST7789_C3_PARAM_1);
    stWriteCommand(ST7789_POWER_C4);
    stWriteData(ST7789_C4_PARAM_1);
    stWriteCommand(ST7789_POWER_C6);
    stWriteData(ST7789_C6_PARAM_1);
    stWriteCommand(ST7789_POWER_D6);
    stWriteData(ST7789_D6_PARAM_1);
    stWriteCommand(ST7789_POWER_D0);
    stWriteData(ST7789_D0_PARAM_1);
    stWriteData(ST7789_D0_PARAM_2);
    stWriteCommand(ST7789_POWER_D6);
    stWriteData(ST7789_D6_PARAM_1);

    stWriteCommand(ST7789_GAMMA_POS);
    for (uint8_t value : ST7789_GAMMA_POS_DATA) {
        stWriteData(value);
    }
    stWriteCommand(ST7789_GAMMA_NEG);
    for (uint8_t value : ST7789_GAMMA_NEG_DATA) {
        stWriteData(value);
    }
    stWriteCommand(ST7789_GAMMA_CTRL);
    for (uint8_t value : ST7789_GAMMA_CTRL_DATA) {
        stWriteData(value);
    }

    stWriteCommand(ST7789_INVERSION_ON);
    stWriteCommand(ST7789_DISPLAY_ON);

    stWriteCommand(ST7789_CASET);
    stWriteData(ST7789_ADDR_START_HIGH);
    stWriteData(ST7789_ADDR_START_LOW);
    stWriteData(ST7789_ADDR_END_HIGH);
    stWriteData(ST7789_ADDR_END_LOW);

    stWriteCommand(ST7789_RASET);
    stWriteData(ST7789_ADDR_START_HIGH);
    stWriteData(ST7789_ADDR_START_LOW);
    stWriteData(ST7789_ADDR_END_HIGH);
    stWriteData(ST7789_ADDR_END_LOW);

    stWriteCommand(ST7789_RAMWR);
    g_lcdBus.endWrite();
}

auto initDisplay() -> void {
    lcdBacklightOn();
    g_lcdBus.begin(static_cast<int32_t>(LCD_SPI_HZ), static_cast<int8_t>(LCD_SPI_MODE));
    lcdHardReset();
    lcdRunVendorInit();
    delay(LCD_BEGIN_DELAY_MS);
    g_lcd.setRotation(LCD_ROTATION);
    g_lcd.fillScreen(LCD_BLACK);
    g_lcd.setTextColor(LCD_WHITE, LCD_BLACK);
}

auto beginAccessPoint() -> void {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
}

auto updateStartAllowed(const String& filename) -> bool {
    return filename == "firmware.bin";
}

void handleLegacyUpdateUpload() {
    HTTPUpload& upload = g_server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        g_updateFailed = false;
        g_updateFinished = false;
        g_updateStatus = "";

        if (!updateStartAllowed(upload.filename)) {
            g_updateFailed = true;
            g_updateStatus = "Only firmware.bin allowed";
            drawBridgeScreen("OTA Bridge", String("AP SSID: ") + AP_SSID, "http://192.168.4.1/bridgeupdate", "", "",
                             "", g_updateStatus, "", LCD_RED);
            return;
        }

        const uint32_t sketchSpace = (ESP.getFreeSketchSpace() - 0x1000U) & 0xFFFFF000U;
        if (!Update.begin(sketchSpace, U_FLASH)) {
            g_updateFailed = true;
            g_updateStatus = Update.getErrorString();
            drawBridgeScreen("OTA Bridge", String("AP SSID: ") + AP_SSID, "http://192.168.4.1/bridgeupdate", "", "",
                             "", g_updateStatus, "", LCD_RED);
            return;
        }

        drawBridgeScreen("OTA Bridge", String("AP SSID: ") + AP_SSID, "http://192.168.4.1/bridgeupdate", "", "",
                         "", "Uploading", "firmware.bin", LCD_GREEN);
        return;
    }

    if (g_updateFailed) {
        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            g_updateFailed = true;
            g_updateStatus = Update.getErrorString();
            drawBridgeScreen("OTA Bridge", String("AP SSID: ") + AP_SSID, "http://192.168.4.1/bridgeupdate", "", "",
                             "", g_updateStatus, "", LCD_RED);
        }
        return;
    }

    if (upload.status == UPLOAD_FILE_END) {
        if (!Update.end(true)) {
            g_updateFailed = true;
            g_updateStatus = Update.getErrorString();
            drawBridgeScreen("OTA Bridge", String("AP SSID: ") + AP_SSID, "http://192.168.4.1/bridgeupdate", "", "",
                             "", g_updateStatus, "", LCD_RED);
            return;
        }

        g_updateFinished = true;
        g_updateStatus = "Update complete";
        g_restartAtMs = millis() + 1500UL;
        drawBridgeScreen("OTA Bridge", "Upload complete", "Rebooting...", "", "", "", "Main firmware next",
                         "Flash littlefs.bin", LCD_GREEN);
        return;
    }

    if (upload.status == UPLOAD_FILE_ABORTED) {
        g_updateFailed = true;
        g_updateStatus = "Upload aborted";
        drawBridgeScreen("OTA Bridge", String("AP SSID: ") + AP_SSID, "http://192.168.4.1/bridgeupdate", "", "",
                         "", g_updateStatus, "", LCD_RED);
    }
}

void handleLegacyUpdatePost() {
    if (g_updateFailed) {
        g_server.send(400, "text/plain", g_updateStatus);
        return;
    }

    if (g_updateFinished) {
        g_server.send(200, "text/plain", "OK");
        return;
    }

    g_server.send(500, "text/plain", "Update failed");
}

void registerRoutes() {
    g_server.on("/", HTTP_GET, []() {
        g_server.sendHeader("Location", UPDATE_PATH, true);
        g_server.send(302, "text/plain", "");
    });

    g_server.on(UPDATE_PATH, HTTP_GET, []() {
        g_server.send(200, "text/html", BRIDGE_PAGE);
    });

    g_server.on(UPDATE_PATH, HTTP_POST, handleLegacyUpdatePost, handleLegacyUpdateUpload);
}

}  // namespace

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(200);

    initDisplay();
    beginAccessPoint();
    registerRoutes();
    g_server.begin();

    drawBridgeScreen("OTA Bridge", String("AP SSID: ") + AP_SSID, "http://192.168.4.1/bridgeupdate", "", "", "",
                     "Upload", "firmware.bin");
}

void loop() {
    g_server.handleClient();

    if (g_updateFinished && g_restartAtMs != 0 && static_cast<long>(millis() - g_restartAtMs) >= 0) {
        ESP.restart();
    }

    delay(2);
}
