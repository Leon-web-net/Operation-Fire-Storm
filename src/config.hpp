#pragma once

#include <cstdint>

constexpr int LISTEN_PORT = 5733;
constexpr int FORWARD_PORT = 5744;   // Stage 1: all normal traffic. Stage 2: TCP

// Stage 2 output ports
constexpr int TCP_PORT = FORWARD_PORT;
constexpr int MONITOR_PORT = 5745;
constexpr int UDP_PORT = 5746;

// MAC prefixes (OUIs) used by route()
constexpr uint8_t DROP_SRC_OUIS[][3] = {      // VMware: attacker traffic, dropped
    {0x00, 0x0C, 0x29}, {0x00, 0x50, 0x56}, {0x00, 0x1C, 0x14},
};
constexpr uint8_t MONITOR_DST_OUIS[][3] = {   // Microsoft and Dell: also copied to MONITOR_PORT
    {0x00, 0x1A, 0xA0}, {0x00, 0x1E, 0xC9}, {0x00, 0x15, 0x5D},
};
