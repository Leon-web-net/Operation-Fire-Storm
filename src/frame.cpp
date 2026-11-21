#include "frame.hpp"

#include "bytes.hpp"
#include "config.hpp"

#include <cstring>

namespace{
    constexpr uint16_t ETHER_TYPE_IPV4 = 0x0800;
    constexpr size_t IP_PROTOCOL_OFFSET = ETH_HEADER_LEN + 9;

    // Does the MAC start with any of the OUIs?
    template <size_t N>
    bool has_oui(const uint8_t* mac, const uint8_t (&ouis)[N][3]){
        for(size_t i = 0; i < N; ++i){
            if(std::memcmp(mac, ouis[i], 3) == 0) return true;
        }
        return false;
    }
}

size_t frame_len(const char* p, size_t n){
    if(n < FRAME_LEN_BYTES) return 0;

    if(read_u16(p + 12) != ETHER_TYPE_IPV4) return FRAME_INVALID;

    unsigned version = static_cast<unsigned char>(p[ETH_HEADER_LEN]) >> 4;
    unsigned ihl = static_cast<unsigned char>(p[ETH_HEADER_LEN]) & 0x0F;
    if(version != 4 || ihl < 5) return FRAME_INVALID;

    size_t total = read_u16(p + ETH_HEADER_LEN + 2);
    if(total < ihl * 4) return FRAME_INVALID;

    return ETH_HEADER_LEN + total;
}

Frame parse_frame(const char* p){
    Frame f;
    std::memcpy(f.dst, p, 6);
    std::memcpy(f.src, p + 6, 6);
    f.protocol = static_cast<uint8_t>(p[IP_PROTOCOL_OFFSET]);
    return f;
}

unsigned route(const Frame& f){
    unsigned out = 0;

    if(!has_oui(f.src, DROP_SRC_OUIS)){
        if(f.protocol == 6) out |= ROUTE_TCP;
        else if(f.protocol == 17) out |= ROUTE_UDP;
    }
    if(has_oui(f.dst, MONITOR_DST_OUIS)) out |= ROUTE_MONITOR;

    return out;
}
