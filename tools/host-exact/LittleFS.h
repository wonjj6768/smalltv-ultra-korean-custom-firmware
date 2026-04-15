#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "Arduino.h"

enum SeekMode {
    SeekSet = 0,
    SeekCur = 1,
    SeekEnd = 2,
};

class File {
   public:
    File() = default;
    explicit File(const std::filesystem::path& path) : stream_(path, std::ios::binary) {}

    explicit operator bool() const { return stream_.is_open(); }

    auto read(uint8_t* buffer, size_t length) -> int {
        if (!stream_.is_open()) {
            return 0;
        }
        stream_.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(length));
        return static_cast<int>(stream_.gcount());
    }

    auto seek(uint32_t position, SeekMode mode) -> bool {
        if (!stream_.is_open()) {
            return false;
        }
        std::ios_base::seekdir dir = std::ios::beg;
        if (mode == SeekCur) {
            dir = std::ios::cur;
        } else if (mode == SeekEnd) {
            dir = std::ios::end;
        }
        stream_.clear();
        stream_.seekg(static_cast<std::streamoff>(position), dir);
        return !stream_.fail();
    }

    auto close() -> void {
        if (stream_.is_open()) {
            stream_.close();
        }
    }

   private:
    std::ifstream stream_;
};

class LittleFSClass {
   public:
    auto begin(bool = true) -> bool { return true; }

    auto exists(const String& path) const -> bool { return std::filesystem::exists(resolve(path)); }
    auto exists(const char* path) const -> bool { return std::filesystem::exists(resolve(path)); }

    auto open(const String& path, const char* mode) const -> File {
        if (mode == nullptr || mode[0] != 'r') {
            return File();
        }
        return File(resolve(path));
    }

    static auto setRoot(std::filesystem::path root) -> void { rootPath() = std::move(root); }

   private:
    static auto rootPath() -> std::filesystem::path& {
        static std::filesystem::path value;
        return value;
    }

    static auto resolve(const String& path) -> std::filesystem::path {
        const std::string raw = path.std();
        if (raw.empty()) {
            return rootPath();
        }
        if (raw.front() == '/') {
            return rootPath() / raw.substr(1);
        }
        return rootPath() / raw;
    }
};

inline LittleFSClass LittleFS;
