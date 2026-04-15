#pragma once

#include <cstdint>
#include <cstring>

#ifndef PROGMEM
#define PROGMEM
#endif

#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*reinterpret_cast<const uint8_t*>(addr))
#endif

#ifndef pgm_read_word
#define pgm_read_word(addr) (*reinterpret_cast<const uint16_t*>(addr))
#endif

#ifndef pgm_read_dword
#define pgm_read_dword(addr) (*reinterpret_cast<const uint32_t*>(addr))
#endif

#ifndef memcpy_P
#define memcpy_P(dest, src, len) std::memcpy((dest), (src), (len))
#endif
