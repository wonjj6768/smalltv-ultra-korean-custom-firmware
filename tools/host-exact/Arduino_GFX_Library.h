#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Arduino.h"
#include "SPI.h"

#include "../../.pio/libdeps/esp12e/GFX Library for Arduino/src/Arduino_GFX.h"
#include "../../.pio/libdeps/esp12e/GFX Library for Arduino/src/canvas/Arduino_Canvas_Indexed.h"

static constexpr uint8_t ST7789_CASET = 0x2A;
static constexpr uint8_t ST7789_RASET = 0x2B;
static constexpr uint8_t ST7789_RAMWR = 0x2C;

class Arduino_HWSPI {
   public:
    Arduino_HWSPI(int8_t, int8_t, SPIClass*, bool) {}
    auto begin(int32_t, int8_t) -> bool { return true; }
    auto beginWrite() -> void {}
    auto endWrite() -> void {}
    auto writeCommand(uint8_t) -> void {}
    auto write(uint8_t) -> void {}
};

class Arduino_ST7789 : public Arduino_GFX {
   public:
    Arduino_ST7789(Arduino_HWSPI*, int8_t, int8_t, bool, int16_t width, int16_t height)
        : Arduino_GFX(width, height), raw_width_(width), raw_height_(height),
          pixels_(static_cast<size_t>(width) * static_cast<size_t>(height), 0) {}

    auto begin(int32_t = GFX_NOT_DEFINED) -> bool override { return true; }

    auto writePixelPreclipped(int16_t x, int16_t y, uint16_t color) -> void override {
        int16_t tx = x;
        int16_t ty = y;
        mapRotation(tx, ty);
        if (tx < 0 || ty < 0 || tx >= raw_width_ || ty >= raw_height_) {
            return;
        }
        pixels_[static_cast<size_t>(ty) * static_cast<size_t>(raw_width_) + static_cast<size_t>(tx)] = color;
    }

    auto saveBMP(const std::filesystem::path& path) const -> bool {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        if (!out.is_open()) {
            return false;
        }

        const int32_t bmpWidth = raw_width_;
        const int32_t bmpHeight = raw_height_;
        const uint32_t rowSize = static_cast<uint32_t>(((bmpWidth * 3) + 3) & ~3);
        const uint32_t imageSize = rowSize * static_cast<uint32_t>(bmpHeight);
        const uint32_t fileSize = 54U + imageSize;

        uint8_t header[54] = {};
        header[0] = 'B';
        header[1] = 'M';
        write32(header + 2, fileSize);
        write32(header + 10, 54U);
        write32(header + 14, 40U);
        write32(header + 18, static_cast<uint32_t>(bmpWidth));
        write32(header + 22, static_cast<uint32_t>(bmpHeight));
        header[26] = 1;
        header[28] = 24;
        write32(header + 34, imageSize);
        out.write(reinterpret_cast<const char*>(header), sizeof(header));

        std::vector<uint8_t> row(rowSize, 0);
        for (int32_t y = bmpHeight - 1; y >= 0; --y) {
            std::fill(row.begin(), row.end(), 0);
            for (int32_t x = 0; x < bmpWidth; ++x) {
                const uint16_t color =
                    pixels_[static_cast<size_t>(y) * static_cast<size_t>(raw_width_) + static_cast<size_t>(x)];
                const uint8_t red = static_cast<uint8_t>(((color >> 11) & 0x1F) * 255 / 31);
                const uint8_t green = static_cast<uint8_t>(((color >> 5) & 0x3F) * 255 / 63);
                const uint8_t blue = static_cast<uint8_t>((color & 0x1F) * 255 / 31);
                const size_t offset = static_cast<size_t>(x) * 3U;
                row[offset] = blue;
                row[offset + 1U] = green;
                row[offset + 2U] = red;
            }
            out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
        }

        return true;
    }

   private:
    static auto write32(uint8_t* dst, uint32_t value) -> void {
        dst[0] = static_cast<uint8_t>(value & 0xFFU);
        dst[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
        dst[2] = static_cast<uint8_t>((value >> 16) & 0xFFU);
        dst[3] = static_cast<uint8_t>((value >> 24) & 0xFFU);
    }

    auto mapRotation(int16_t& x, int16_t& y) const -> void {
        const int16_t ox = x;
        const int16_t oy = y;
        switch (_rotation & 0x3U) {
            case 0:
                return;
            case 1:
                x = static_cast<int16_t>(raw_width_ - 1 - oy);
                y = ox;
                return;
            case 2:
                x = static_cast<int16_t>(raw_width_ - 1 - ox);
                y = static_cast<int16_t>(raw_height_ - 1 - oy);
                return;
            default:
                x = oy;
                y = static_cast<int16_t>(raw_height_ - 1 - ox);
                return;
        }
    }

    int16_t raw_width_;
    int16_t raw_height_;
    std::vector<uint16_t> pixels_;
};
