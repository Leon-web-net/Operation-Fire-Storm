#pragma once

#include <cstdint>

// Big-endian (network order) reads and writes, shared by the CTFS header, frames and the spool.
// void* so they work on both char and unsigned char buffers.

inline uint16_t read_u16(const void* v){
    const auto* p = static_cast<const unsigned char*>(v);
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

inline uint32_t read_u32(const void* v){
    const auto* p = static_cast<const unsigned char*>(v);
    return (static_cast<uint32_t>(read_u16(p)) << 16) | read_u16(p + 2);
}

inline void write_u16(void* v, uint16_t x){
    auto* p = static_cast<unsigned char*>(v);
    p[0] = static_cast<unsigned char>(x >> 8);
    p[1] = static_cast<unsigned char>(x);
}
