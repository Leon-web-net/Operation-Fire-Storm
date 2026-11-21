#include "config.hpp"
#include "frame.hpp"
#include <cstdio>
#include <cstring>
#include <string>

int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); ++failures; } \
} while (0)

#define RUN(test) do { \
    int before = failures; \
    test(); \
    std::fprintf(stderr, "[%s] %s\n", failures == before ? " OK " : "FAIL", #test); \
} while (0)


// HELPER FUNCTIONS

constexpr uint8_t PLAIN[3] = {0x02, 0x11, 0x22};   // an OUI in neither table
constexpr uint8_t TCP = 6, UDP = 17, ICMP = 1;

// build a frame: Ethernet II + IPv4 (IHL 5) + payload of the given size.
// MACs are the OUI followed by tail bytes (default 1, 2, 3).
std::string frame(const uint8_t* dst_oui, const uint8_t* src_oui, uint8_t protocol, size_t payload_size = 0,
                  const uint8_t* tail = nullptr) {
    static const uint8_t default_tail[3] = {1, 2, 3};
    if (!tail) tail = default_tail;
    size_t total = 20 + payload_size;

    std::string f;
    f.append(reinterpret_cast<const char*>(dst_oui), 3);
    f.append(reinterpret_cast<const char*>(tail), 3);
    f.append(reinterpret_cast<const char*>(src_oui), 3);
    f.append(reinterpret_cast<const char*>(tail), 3);
    f += char(0x08); f += char(0x00);                                   // ether type IPv4
    f += '\x45';                                                        // version 4, IHL 5
    f += '\0';                                                          // TOS
    f += char(total >> 8); f += char(total & 0xFF);                     // total length, big-endian
    f += std::string(4, '\0');                                          // id, flags, fragment offset
    f += '\x40';                                                        // TTL
    f += char(protocol);
    f += std::string(10, '\0');                                         // checksum, src IP, dst IP
    return f + std::string(payload_size, 'x');
}

unsigned route_of(const uint8_t* dst_oui, const uint8_t* src_oui, uint8_t protocol) {
    std::string f = frame(dst_oui, src_oui, protocol);
    return route(parse_frame(f.data()));
}


// TESTS

// 1. a valid frame: 14 + IP total length
// Expected: 14 + 20 + 100 = 134
void test_frame_len() {
    std::string f = frame(PLAIN, PLAIN, TCP, 100);
    CHECK(frame_len(f.data(), f.size()) == 134);
}

// 2. fewer than 18 bytes: the length isn't known yet
// Expected: 0 for every n from 0 to 17
void test_frame_len_short() {
    std::string f = frame(PLAIN, PLAIN, TCP, 100);
    for (size_t n = 0; n < FRAME_LEN_BYTES; ++n) CHECK(frame_len(f.data(), n) == 0);
}

// 3. wrong ether type (ARP)
// Expected: FRAME_INVALID
void test_frame_len_bad_ethertype() {
    std::string f = frame(PLAIN, PLAIN, TCP);
    f[12] = '\x08'; f[13] = '\x06';
    CHECK(frame_len(f.data(), f.size()) == FRAME_INVALID);
}

// 4. IP version 6
// Expected: FRAME_INVALID
void test_frame_len_bad_version() {
    std::string f = frame(PLAIN, PLAIN, TCP);
    f[14] = '\x65';
    CHECK(frame_len(f.data(), f.size()) == FRAME_INVALID);
}

// 5. IHL 4 (header shorter than the minimum)
// Expected: FRAME_INVALID
void test_frame_len_bad_ihl() {
    std::string f = frame(PLAIN, PLAIN, TCP);
    f[14] = '\x44';
    CHECK(frame_len(f.data(), f.size()) == FRAME_INVALID);
}

// 6. total length 19, smaller than the 20 byte header
// Expected: FRAME_INVALID
void test_frame_len_total_too_small() {
    std::string f = frame(PLAIN, PLAIN, TCP);
    f[17] = 19;
    CHECK(frame_len(f.data(), f.size()) == FRAME_INVALID);
}

// 7. the result doesn't depend on how many bytes are available (once there are 18)
// Expected: 60 for n = 18 and for the whole 60 byte frame
void test_frame_len_independent_of_n() {
    std::string f = frame(PLAIN, PLAIN, TCP, 26);
    CHECK(f.size() == 60);
    CHECK(frame_len(f.data(), 18) == 60);
    CHECK(frame_len(f.data(), 40) == 60);
    CHECK(frame_len(f.data(), 60) == 60);
}

// 8. parse_frame reads dst, src and protocol
// Expected: dst = bytes 0-5, src = bytes 6-11, protocol = 17
void test_parse_frame() {
    const uint8_t dst[3] = {0xAA, 0xBB, 0xCC}, src[3] = {0xDD, 0xEE, 0xFF};
    std::string f = frame(dst, src, UDP, 8);
    Frame fr = parse_frame(f.data());
    const uint8_t want_dst[6] = {0xAA, 0xBB, 0xCC, 1, 2, 3};
    const uint8_t want_src[6] = {0xDD, 0xEE, 0xFF, 1, 2, 3};
    CHECK(std::memcmp(fr.dst, want_dst, 6) == 0);
    CHECK(std::memcmp(fr.src, want_src, 6) == 0);
    CHECK(fr.protocol == UDP);
}

// 9. plain TCP
// Expected: ROUTE_TCP
void test_route_tcp() {
    CHECK(route_of(PLAIN, PLAIN, TCP) == ROUTE_TCP);
}

// 10. plain UDP
// Expected: ROUTE_UDP
void test_route_udp() {
    CHECK(route_of(PLAIN, PLAIN, UDP) == ROUTE_UDP);
}

// 11. other protocol (ICMP)
// Expected: nothing
void test_route_icmp() {
    CHECK(route_of(PLAIN, PLAIN, ICMP) == 0);
}

// 12. VMware source
// Expected: nothing, for TCP and UDP, for each of the 3 OUIs
void test_route_vmware_source() {
    for (const auto& oui : DROP_SRC_OUIS) {
        CHECK(route_of(PLAIN, oui, TCP) == 0);
        CHECK(route_of(PLAIN, oui, UDP) == 0);
    }
}

// 13. monitored destination
// Expected: TCP -> TCP|MONITOR, UDP -> UDP|MONITOR, ICMP -> MONITOR, for each of the 3 OUIs
void test_route_monitor_dest() {
    for (const auto& oui : MONITOR_DST_OUIS) {
        CHECK(route_of(oui, PLAIN, TCP) == (ROUTE_TCP | ROUTE_MONITOR));
        CHECK(route_of(oui, PLAIN, UDP) == (ROUTE_UDP | ROUTE_MONITOR));
        CHECK(route_of(oui, PLAIN, ICMP) == ROUTE_MONITOR);
    }
}

// 14. VMware source and monitored destination
// Expected: MONITOR only, even though the packet is otherwise dropped
void test_route_drop_and_monitor() {
    for (const auto& src : DROP_SRC_OUIS) {
        for (const auto& dst : MONITOR_DST_OUIS) {
            CHECK(route_of(dst, src, TCP) == ROUTE_MONITOR);
            CHECK(route_of(dst, src, UDP) == ROUTE_MONITOR);
        }
    }
}

// 15. VMware OUI as the destination
// Expected: not dropped, ROUTE_TCP
void test_route_vmware_dest() {
    for (const auto& oui : DROP_SRC_OUIS) CHECK(route_of(oui, PLAIN, TCP) == ROUTE_TCP);
}

// 16. monitor OUI as the source
// Expected: not monitored, ROUTE_TCP
void test_route_monitor_source() {
    for (const auto& oui : MONITOR_DST_OUIS) CHECK(route_of(PLAIN, oui, TCP) == ROUTE_TCP);
}

// 17. only the first 3 bytes of the MAC matter
// Expected: any tail still matches; a different 3rd byte does not
void test_route_oui_only() {
    const uint8_t tails[2][3] = {{0, 0, 0}, {0xFF, 0xFE, 0xFD}};
    for (const auto& tail : tails) {
        std::string f = frame(MONITOR_DST_OUIS[0], DROP_SRC_OUIS[0], TCP, 0, tail);
        CHECK(route(parse_frame(f.data())) == ROUTE_MONITOR);
    }
    uint8_t near_miss[3] = {DROP_SRC_OUIS[0][0], DROP_SRC_OUIS[0][1], uint8_t(DROP_SRC_OUIS[0][2] + 1)};
    CHECK(route_of(PLAIN, near_miss, TCP) == ROUTE_TCP);
}


int main() {
    RUN(test_frame_len);
    RUN(test_frame_len_short);
    RUN(test_frame_len_bad_ethertype);
    RUN(test_frame_len_bad_version);
    RUN(test_frame_len_bad_ihl);
    RUN(test_frame_len_total_too_small);
    RUN(test_frame_len_independent_of_n);
    RUN(test_parse_frame);
    RUN(test_route_tcp);
    RUN(test_route_udp);
    RUN(test_route_icmp);
    RUN(test_route_vmware_source);
    RUN(test_route_monitor_dest);
    RUN(test_route_drop_and_monitor);
    RUN(test_route_vmware_dest);
    RUN(test_route_monitor_source);
    RUN(test_route_oui_only);

    std::fprintf(stderr, failures == 0 ? "\nAll tests passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;   // ctest reads this: 0 = pass
}
