#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "Arduino.h"

#ifndef DEC
#define DEC 10
#endif
#ifndef HEX
#define HEX 16
#endif
#ifndef OCT
#define OCT 8
#endif
#ifndef BIN
#define BIN 2
#endif

class Printable {
   public:
    virtual ~Printable() = default;
    virtual auto printTo(class Print& printer) const -> size_t = 0;
};

class Print {
   public:
    virtual ~Print() = default;
    virtual auto write(uint8_t) -> size_t = 0;

    virtual auto write(const uint8_t* buffer, size_t size) -> size_t {
        size_t written = 0;
        if (buffer == nullptr) {
            return 0;
        }
        while (written < size) {
            written += write(buffer[written]);
        }
        return written;
    }

    auto write(const char* value) -> size_t {
        if (value == nullptr) {
            return 0;
        }
        return write(reinterpret_cast<const uint8_t*>(value), std::strlen(value));
    }

    auto write(const char* value, size_t size) -> size_t {
        return write(reinterpret_cast<const uint8_t*>(value), size);
    }

    auto write(char value) -> size_t { return write(static_cast<uint8_t>(value)); }
    auto write(int8_t value) -> size_t { return write(static_cast<uint8_t>(value)); }
    auto write(int value) -> size_t { return write(static_cast<uint8_t>(value)); }
    auto write(unsigned int value) -> size_t { return write(static_cast<uint8_t>(value)); }
    auto write(long value) -> size_t { return write(static_cast<uint8_t>(value)); }
    auto write(unsigned long value) -> size_t { return write(static_cast<uint8_t>(value)); }
    auto write(long long value) -> size_t { return write(static_cast<uint8_t>(value)); }
    auto write(unsigned long long value) -> size_t { return write(static_cast<uint8_t>(value)); }

    virtual auto availableForWrite() -> int { return 0; }
    virtual auto flush() -> void {}
    virtual auto outputCanTimeout() -> bool { return true; }

    auto print(const String& value) -> size_t { return write(value.c_str()); }
    auto print(const char* value) -> size_t { return write(value); }
    auto print(char value) -> size_t { return write(value); }
    auto print(const Printable& value) -> size_t { return value.printTo(*this); }
    auto print(const __FlashStringHelper* value) -> size_t {
        return write(reinterpret_cast<const char*>(value));
    }
    auto print(unsigned char value, int base = DEC) -> size_t {
        return printUnsigned(value, base);
    }
    auto print(int value, int base = DEC) -> size_t { return printSigned(value, base); }
    auto print(unsigned int value, int base = DEC) -> size_t { return printUnsigned(value, base); }
    auto print(long value, int base = DEC) -> size_t { return printSigned(value, base); }
    auto print(unsigned long value, int base = DEC) -> size_t { return printUnsigned(value, base); }
    auto print(long long value, int base = DEC) -> size_t { return printSigned(value, base); }
    auto print(unsigned long long value, int base = DEC) -> size_t { return printUnsigned(value, base); }
    auto print(double value, int digits = 2) -> size_t {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
        return write(buffer);
    }

    auto println() -> size_t { return write("\r\n"); }
    auto println(const String& value) -> size_t { return print(value) + println(); }
    auto println(const char* value) -> size_t { return print(value) + println(); }
    auto println(char value) -> size_t { return print(value) + println(); }
    auto println(const Printable& value) -> size_t { return print(value) + println(); }
    auto println(const __FlashStringHelper* value) -> size_t { return print(value) + println(); }
    auto println(unsigned char value, int base = DEC) -> size_t { return print(value, base) + println(); }
    auto println(int value, int base = DEC) -> size_t { return print(value, base) + println(); }
    auto println(unsigned int value, int base = DEC) -> size_t { return print(value, base) + println(); }
    auto println(long value, int base = DEC) -> size_t { return print(value, base) + println(); }
    auto println(unsigned long value, int base = DEC) -> size_t { return print(value, base) + println(); }
    auto println(long long value, int base = DEC) -> size_t { return print(value, base) + println(); }
    auto println(unsigned long long value, int base = DEC) -> size_t { return print(value, base) + println(); }
    auto println(double value, int digits = 2) -> size_t { return print(value, digits) + println(); }

   private:
    template <typename T>
    auto printSigned(T value, int base) -> size_t {
        if (base == DEC) {
            return write(std::to_string(value).c_str());
        }
        if (value < 0) {
            return write('-') + printUnsigned(static_cast<unsigned long long>(-value), base);
        }
        return printUnsigned(static_cast<unsigned long long>(value), base);
    }

    template <typename T>
    auto printUnsigned(T value, int base) -> size_t {
        if (base == DEC) {
            return write(std::to_string(value).c_str());
        }

        if (base < 2 || base > 16) {
            base = DEC;
        }

        std::string rendered;
        do {
            const auto digit = static_cast<unsigned>(value % static_cast<T>(base));
            rendered.push_back(static_cast<char>(digit < 10 ? ('0' + digit) : ('A' + digit - 10)));
            value /= static_cast<T>(base);
        } while (value != 0);

        std::reverse(rendered.begin(), rendered.end());
        return write(rendered.c_str());
    }
};
