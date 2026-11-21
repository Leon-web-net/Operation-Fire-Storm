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
    uint32_t checksum;   // offset 9, big-endian
    bool     padding_ok; // offset 13: true if the 3 padding bytes are 00 00 00 (else not a CTFS packet)
};

// Decode the header fields. p must point at the 'C' of "CTFS" with at least HEADER_LEN bytes available.
CtfsHeader parse_header(const char* p);

// The brief's checksum over header + payload: big-endian 32-bit words, checksum field replaced by "CTFS",
// add (mod 2^32) then rotate left 1 per word; a short last word is padded with zeros.
// Rebuilds the header from h, so it assumes zero padding (the parser only emits packets with padding_ok).
uint32_t ctfs_checksum(const CtfsHeader& h, const char* payload);
