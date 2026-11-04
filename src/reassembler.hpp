#pragma once

#include "ctfs.hpp"

#include <cstdint>
#include <map>
#include <vector>

// Reassembly: collects CTFS payloads per file ID, by sequence number, so each file can be written out in order.
// Knows nothing about the byte stream; it only sees complete packets.

struct Reassembler {
    std::map<uint8_t, std::map<uint16_t,std::vector<char>>> files; // file_id -> seq -> payload
};

// Store one packet, Return false if file already has that seq
bool add_packet(Reassembler& r, const CtfsHeader& header, const char* payload);
