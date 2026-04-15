#pragma once

#include <iostream>

class Logger {
   public:
    static auto info(const char* message, const char* tag = nullptr) -> void { log("INFO", message, tag); }
    static auto warn(const char* message, const char* tag = nullptr) -> void { log("WARN", message, tag); }
    static auto error(const char* message, const char* tag = nullptr) -> void { log("ERROR", message, tag); }

   private:
    static auto log(const char* level, const char* message, const char* tag) -> void {
        std::clog << "[" << level << "]";
        if (tag != nullptr) {
            std::clog << "[" << tag << "]";
        }
        std::clog << " " << (message == nullptr ? "" : message) << '\n';
    }
};
