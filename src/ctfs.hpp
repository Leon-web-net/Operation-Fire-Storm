#pragma once

#include <cstddef>
#include <cstdint>

// The CTFS packet format, shared by the parser (which finds packets)
// and the reassembler (which stores them). Belongs to neither.

constexpr size_t HEADER_LEN = 16;

// The 16-byte CTFS header, decoded
struct CtfsHeader {
    uint16_t seq;        // offset 4, big-endian
    uint16_t len;        // offset 6, big-endian: payload length
    uint8_t  file_id;    // offset 8
    uint32_t checksum;   // offset 9 (byte order unconfirmed, read as big-endian; not checked yet)
    bool     padding_ok; // offset 13: true if the 3 padding bytes are 00 00 00 (not acted on yet)
};

// Decode the header fields. p must point at the 'C' of "CTFS" with at least HEADER_LEN bytes available.
CtfsHeader parse_header(const char* p);
