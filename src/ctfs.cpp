#include "ctfs.hpp"

namespace {
    // Read a big-endian (network order) 16-bit number from two bytes.
    uint16_t read_u16(const char* p){
        return static_cast<uint16_t>(
            (static_cast<uint8_t>(p[0])<<8) | static_cast<uint8_t>(p[1])
        );
    }

    uint32_t read_u32(const char* p){
        return (static_cast<uint32_t>(read_u16(p)) << 16) | read_u16(p + 2);
    }
}

CtfsHeader parse_header(const char* p){
    CtfsHeader h;
    h.seq        = read_u16(p + 4);
    h.len        = read_u16(p + 6);
    h.file_id    = static_cast<uint8_t>(p[8]);
    h.checksum   = read_u32(p + 9);
    h.padding_ok = p[13] == 0 && p[14] == 0 && p[15] == 0;
    return h;
}
