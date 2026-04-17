#pragma once

#include "Arduino.h"

class IPAddress {
   public:
    IPAddress() : octets_{0, 0, 0, 0} {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : octets_{a, b, c, d} {}

    auto toString() const -> String {
        return String(octets_[0]) + "." + String(octets_[1]) + "." + String(octets_[2]) + "." + String(octets_[3]);
    }

   private:
    uint8_t octets_[4];
};
