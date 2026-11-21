#pragma once

#include <cstddef>
#include <cstdint>

// Stage 2: Ethernet + IPv4 frames in the input stream, and where each one is routed.

constexpr size_t ETH_HEADER_LEN = 14;
constexpr size_t FRAME_LEN_BYTES = 18;       // bytes needed to know a frame's length (IP total length at 16-17)
constexpr size_t FRAME_INVALID = SIZE_MAX;   // frame_len: these bytes can't start a frame

// Length of the frame starting at p (n bytes available): 14 + IPv4 total length.
// 0 = need more bytes; FRAME_INVALID = not Ethernet/IPv4 (ether_type != 0x0800, IP version != 4,
// IHL < 5, or total length < IHL * 4).
size_t frame_len(const char* p, size_t n);

struct Frame {
    uint8_t dst[6];
    uint8_t src[6];
    uint8_t protocol;   // IP protocol: 6 TCP, 17 UDP
};

// Decode the fields route() needs. p must hold a whole frame (frame_len returned a length).
Frame parse_frame(const char* p);

// Output ports a frame goes to, as a bitmask
enum Route : unsigned {
    ROUTE_TCP     = 1u << 0,   // TCP_PORT
    ROUTE_MONITOR = 1u << 1,   // MONITOR_PORT
    ROUTE_UDP     = 1u << 2,   // UDP_PORT
};

// The brief's routing rules (pure, no I/O):
// 1. source OUI is VMware: no TCP/UDP output
// 2. otherwise protocol 6 -> ROUTE_TCP, 17 -> ROUTE_UDP, else nothing
// 3. destination OUI is Microsoft/Dell: add ROUTE_MONITOR, even if 1 or 2 dropped it
unsigned route(const Frame& f);
