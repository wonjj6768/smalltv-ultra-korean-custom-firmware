#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <ios>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

using byte = uint8_t;
using boolean = bool;

class __FlashStringHelper;

class String {
   public:
    String() = default;
    String(const char* value) : value_(value == nullptr ? "" : value) {}
    String(const std::string& value) : value_(value) {}
    String(std::string&& value) noexcept : value_(std::move(value)) {}
    String(char value) : value_(1, value) {}
    String(unsigned char value) : value_(std::to_string(static_cast<unsigned int>(value))) {}
    String(int value) : value_(std::to_string(value)) {}
    String(unsigned int value) : value_(std::to_string(value)) {}
    String(long value) : value_(std::to_string(value)) {}
    String(unsigned long value) : value_(std::to_string(value)) {}
    String(long long value) : value_(std::to_string(value)) {}
    String(unsigned long long value) : value_(std::to_string(value)) {}
    String(float value) : value_(formatFloat(value, -1)) {}
    String(double value) : value_(formatFloat(value, -1)) {}
    String(float value, unsigned int decimals) : value_(formatFloat(value, static_cast<int>(decimals))) {}
    String(double value, unsigned int decimals) : value_(formatFloat(value, static_cast<int>(decimals))) {}

    auto c_str() const -> const char* { return value_.c_str(); }
    auto data() -> char* { return value_.empty() ? nullptr : value_.data(); }
    auto data() const -> const char* { return value_.data(); }
    auto length() const -> unsigned int { return static_cast<unsigned int>(value_.size()); }
    auto isEmpty() const -> bool { return value_.empty(); }
    auto reserve(unsigned int size) -> void { value_.reserve(size); }
    auto charAt(unsigned int index) const -> char { return index < value_.size() ? value_[index] : '\0'; }
    auto endsWith(const char* suffix) const -> bool {
        if (suffix == nullptr) {
            return false;
        }
        const std::string suffixValue(suffix);
        if (suffixValue.size() > value_.size()) {
            return false;
        }
        return value_.compare(value_.size() - suffixValue.size(), suffixValue.size(), suffixValue) == 0;
    }
    auto empty() const -> bool { return value_.empty(); }
    auto size() const -> size_t { return value_.size(); }
    auto std() const -> const std::string& { return value_; }

    auto operator+=(const String& rhs) -> String& {
        value_ += rhs.value_;
        return *this;
    }
    auto operator+=(const char* rhs) -> String& {
        value_ += (rhs == nullptr ? "" : rhs);
        return *this;
    }
    auto operator+=(char rhs) -> String& {
        value_ += rhs;
        return *this;
    }

    auto operator==(const String& rhs) const -> bool { return value_ == rhs.value_; }
    auto operator!=(const String& rhs) const -> bool { return value_ != rhs.value_; }
    auto operator<(const String& rhs) const -> bool { return value_ < rhs.value_; }

    friend auto operator+(const String& lhs, const String& rhs) -> String { return String(lhs.value_ + rhs.value_); }
    friend auto operator+(const String& lhs, const char* rhs) -> String {
        return String(lhs.value_ + std::string(rhs == nullptr ? "" : rhs));
    }
    friend auto operator+(const char* lhs, const String& rhs) -> String {
        return String(std::string(lhs == nullptr ? "" : lhs) + rhs.value_);
    }

   private:
    static auto formatFloat(double value, int decimals) -> std::string {
        std::ostringstream stream;
        if (decimals >= 0) {
            stream << std::fixed << std::setprecision(decimals);
        }
        stream << value;
        return stream.str();
    }

    std::string value_;
};

#define F(x) x

static constexpr uint8_t HIGH = 0x1;
static constexpr uint8_t LOW = 0x0;
static constexpr uint8_t OUTPUT = 0x1;

inline auto pinMode(uint8_t, uint8_t) -> void {}
inline auto digitalWrite(uint8_t, uint8_t) -> void {}
inline auto analogWrite(uint8_t, int) -> void {}
inline auto analogWriteRange(int) -> void {}
inline auto analogWriteFreq(int) -> void {}
inline auto yield() -> void { std::this_thread::yield(); }
inline auto delay(unsigned long ms) -> void { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

inline auto millis() -> unsigned long {
    static const auto start = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    return static_cast<unsigned long>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
}
