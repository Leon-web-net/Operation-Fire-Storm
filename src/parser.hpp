#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

constexpr size_t HEADER_LEN = 16;

enum class State{
    Searching, // scanning for CTFS
    INMessage 
};

using ForwardFn = std::functional<bool(const char* data,size_t len)>;

struct Parser {
    State state = State::Searching;
    std::vector<char> pending; // bytes not yet processed
    std::map<uint16_t, std::vector<char>> chunks; // seq -> payload
};

// Read a big-endian (network order) 16-bit number from two bytes.
uint16_t read_u16(const char* p);

// Process newly received bytes. Does not assume recv() lines up with messages.
bool feed(Parser& p, const char* data, size_t n, const ForwawrdFn& forward);

// Call once when the input stream ends: forwards any held-back bytes.
bool finish(Parser& p, const ForwardFn& forward);