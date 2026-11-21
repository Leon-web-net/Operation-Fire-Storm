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


// Stage 2 framing: the stream is CTFS packets and Ethernet frames back to back, split by their lengths.
// A unit starting with "CTFS" and zero padding is a packet; anything else must be a frame (see frame.hpp).

// Called once per complete frame (only valid during the call). Return false on error.
using FrameFn = std::function<bool(const char* frame, size_t len)>;

// Process newly received bytes. Returns false if on_frame fails or a unit isn't a valid frame.
bool feed_frames(Parser& p, const char* data, size_t n, const FrameFn& on_frame, const PacketFn& on_packet);

// Call once when the input stream ends: an incomplete last unit is dropped with a warning.
void finish_frames(Parser& p);