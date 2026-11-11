#pragma once

#include "ctfs.hpp"
#include <cstddef>
#include <functional>
#include <vector>

// Framing: finds CTFS packets in the TCP byte stream

enum class State{
    Searching, // scanning for CTFS
    InMessage  // found CTFS, waiting for the rest of the packet
};

// Called with normal traffic to send downstream. Return false on error.
using ForwardFn = std::function<bool(const char* data, size_t len)>;

// Called once per complete CTFS packet. payload points at header.len bytes.
// payload is only valid during the call (it points into the recv buffer or the parser's pending bytes): copy what you keep.
using PacketFn = std::function<void(const CtfsHeader& header, const char* payload)>;

struct Parser {
    State state = State::Searching;
    std::vector<char> pending; // bytes not yet processed
};

// Process newly received bytes.
bool feed(Parser& p, const char* data, size_t n, const ForwardFn& forward, const PacketFn& on_packet);

// Call once when the input stream ends: forwards any held-back bytes.
bool finish(Parser& p, const ForwardFn& forward);