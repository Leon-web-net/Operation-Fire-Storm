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

namespace {
    uint32_t add_word(uint32_t acc, uint32_t word){
        acc += word;                         // unsigned: wraps mod 2^32
        return (acc << 1) | (acc >> 31);     // rotate left 1
    }
}

uint32_t ctfs_checksum(const CtfsHeader& h, const char* payload){
    // Header words, with "CTFS" in place of the checksum (bytes 9-12) and zero padding (13-15)
    uint32_t acc = 0;
    acc = add_word(acc, 0x43544653u);                                         // "CTFS"
    acc = add_word(acc, (static_cast<uint32_t>(h.seq) << 16) | h.len);
    acc = add_word(acc, (static_cast<uint32_t>(h.file_id) << 24) | 0x435446u); // file id, "CTF"
    acc = add_word(acc, 0x53000000u);                                         // "S", padding

    size_t i = 0;
    for(; i + 4 <= h.len; i += 4) acc = add_word(acc, read_u32(payload + i));
    if(i < h.len){                           // short last word: pad with zeros
        uint32_t word = 0;
        for(size_t k = 0; k < 4; ++k){
            word <<= 8;
            if(i + k < h.len) word |= static_cast<uint8_t>(payload[i + k]);
        }
        acc = add_word(acc, word);
    }
    return acc;
}
