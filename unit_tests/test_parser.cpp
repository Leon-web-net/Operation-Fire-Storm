#include "parser.hpp"
#include <cstdio>
#include <string>
#include <vector>

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

 // build one CTFS packet (16 byte header + payload)
 std::string packet(int seq, const std::string& payload){
    std::string p = "CTFS";
    p += char(seq >> 8); p += char(seq & 0xFF);                         // seq, big-endian
    p += char(payload.size() >> 8); p += char(payload.size() & 0xFF);  // len, big-endian
    p += std::string(1, '\x01');    // file ID
    p += std::string(4, '\0');      // checksum (not checked)
    p += std::string(3, '\0');      // padding
    return p + payload;
 }

 // feed the parser, collecting everything it forwards
std::string forwarded;
ForwardFn collect = [](const char* data, size_t len) {
    forwarded.append(data, len);
    return true;
};

// collect every complete packet the parser emits, in order (duplicates included)
struct Emitted {
    uint16_t seq;
    std::string payload;
};
std::vector<Emitted> packets;
PacketFn collect_packet = [](const CtfsHeader& header, const char* payload) {
    packets.push_back({header.seq, std::string(payload, header.len)});  // copy: payload is only valid during the call
};

// start each test with nothing forwarded and nothing emitted
void reset() {
    forwarded.clear();
    packets.clear();
}


// TESTS

// 1. Normal traffic passes straight through.
// Expected: text with no CTFS marker is forwarded unchanged and no packet is emitted.
void test_plain_traffic() {
    reset();
    Parser p;
    std::string input = "hello world";
 
    feed(p, input.data(), input.size(), collect, collect_packet);
    finish(p, collect);
 
    CHECK(forwarded == "hello world");
    CHECK(packets.empty());
}
 
// 2. A CTFS packet is emitted, and NOT forwarded.
// Expected: the packet is handed to on_packet with its seq and payload; only the text around it is forwarded.
void test_one_packet() {
    reset();
    Parser p;
    std::string input = "before" + packet(0, "SECRET") + "after";
 
    feed(p, input.data(), input.size(), collect, collect_packet);
    finish(p, collect);
 
    CHECK(forwarded == "beforeafter");
    CHECK(packets.size() == 1);
    CHECK(packets[0].seq == 0);
    CHECK(packets[0].payload == "SECRET");
}
 
// 3. TCP can split data anywhere. Cut the input into two recv()s at every position.
// Expected: wherever TCP splits the data into two reads, the result is the same as one read.
void test_split_everywhere() {
    std::string input = "before" + packet(0, "SECRET") + "after";
 
    for (size_t cut = 1; cut < input.size(); ++cut) {
        reset();
        Parser p;
 
        feed(p, input.data(), cut, collect, collect_packet);                        
        feed(p, input.data() + cut, input.size() - cut, collect, collect_packet);   
        finish(p, collect);
 
        CHECK(forwarded == "beforeafter");
        CHECK(packets.size() == 1);
    }
}

// 4. Parser handles incoming data one byte at a time
// Expected: feeding one byte per read still gives the same result as one big read.
void test_one_byte_at_a_time() {
    std::string input = "before" + packet(0, "SECRET") + "after";
 
    reset();
    Parser p;
 
    for (size_t i = 0; i < input.size(); ++i) {
        feed(p, input.data() + i, 1, collect, collect_packet);
    }
    finish(p, collect);
 
    CHECK(forwarded == "beforeafter");
    CHECK(packets.size() == 1);
}

// 5. A partial CTFS marker that turns out not to be a marker is forwarded as normal traffic
// Expected: a trailing "CT" is held back in case it's the start of CTFS, then forwarded once it isn't.
void test_partial_marker(){
    reset();
    Parser p;
    
    std::string first = "beforeCT";
    feed(p,first.data(), first.size(), collect, collect_packet);
    CHECK(forwarded == "before");

    std::string second = "X";
    feed(p,second.data(), second.size(), collect, collect_packet);
    CHECK(forwarded == "beforeCTX");
    CHECK(packets.empty());

}

// 6. An incomplete CTFS header is retained until the remaining bytes arrive
//  Expected: with only part of a header received, nothing is emitted or forwarded until the rest arrives.
void test_partial_header() {
    reset();
    Parser p;
    std::string pkt = packet(0, "SECRET");

    std::string first  = "before" + pkt.substr(0, 10);   
    std::string second = pkt.substr(10) + "after";      

    feed(p, first.data(), first.size(), collect, collect_packet);
    CHECK(forwarded == "before");          
    CHECK(packets.empty());              

    feed(p, second.data(), second.size(), collect, collect_packet);
    finish(p, collect);
    CHECK(forwarded == "beforeafter");
    CHECK(packets.size() == 1);           
}
// 7. Several CTFS packets can be received in one read, and the normal traffic between them is forwarded
// Expected: every packet in a single read is emitted, and the normal byte between them is forwarded.
void test_several_packets_one_read() {
    reset();
    Parser p;
    std::string input = packet(0, "AAA") + "x" + packet(1, "BBB") + packet(2, "CCC");

    feed(p, input.data(), input.size(), collect, collect_packet);
    finish(p, collect);

    CHECK(forwarded == "x");               // only the normal byte gets through
    CHECK(packets.size() == 3);
    CHECK(packets[0].payload == "AAA");
    CHECK(packets[1].payload == "BBB");
    CHECK(packets[2].payload == "CCC");
}


// 8. A truncated CTFS packet at the end of the stream is forwarded, not lost
// Expected: a packet cut off by the end of the stream is forwarded as normal data, not dropped.
void test_truncated_packet_at_eof() {
    reset();
    Parser p;
    std::string pkt = packet(0, "SECRET");
    std::string input = "before" + pkt.substr(0, pkt.size() - 3);

    feed(p, input.data(), input.size(), collect, collect_packet);
    finish(p, collect);

    CHECK(forwarded == input);             // the partial packet is forwarded, not lost
    CHECK(packets.empty());
}

// 9. A repeated seq is still emitted: deciding what to do with duplicates is the reassembler's job.
// Expected: both packets are handed out, in the order they arrived.
void test_duplicate_seq() {
    reset();
    Parser p;
    std::string input = packet(5, "old") + packet(5, "new");

    feed(p, input.data(), input.size(), collect, collect_packet);
    finish(p, collect);

    CHECK(packets.size() == 2);
    CHECK(packets[0].payload == "old");
    CHECK(packets[1].payload == "new");
}

// 10. The stream ends partway through a CTFS header: those bytes are forwarded too
// Expected: a header cut off by the end of the stream is forwarded as normal data, not dropped.
void test_truncated_header_at_eof() {
    reset();
    Parser p;
    std::string input = "before" + packet(0, "SECRET").substr(0, 10);  // marker + 6 header bytes

    feed(p, input.data(), input.size(), collect, collect_packet);
    finish(p, collect);

    CHECK(forwarded == input);
    CHECK(packets.empty());
}

// 11. parse_header reads every field from the right offset
// Expected: distinct values in each field come back exactly; non-zero padding is flagged.
void test_parse_header() {
    // CTFS | seq 0x0102 | len 0x0003 | file 0x07 | checksum 0xAABBCCDD | padding 00 00 00
    std::string hdr = std::string("CTFS\x01\x02\x00\x03\x07\xAA\xBB\xCC\xDD", 13) + std::string(3, '\0');
    CHECK(hdr.size() == HEADER_LEN);

    CtfsHeader h = parse_header(hdr.data());
    CHECK(h.seq == 0x0102);
    CHECK(h.len == 3);
    CHECK(h.file_id == 7);
    CHECK(h.checksum == 0xAABBCCDD);
    CHECK(h.padding_ok);

    hdr[15] = '\x01';                      // break the padding
    CHECK(!parse_header(hdr.data()).padding_ok);
}

int main() {
    // hide the parser's "CTFS seq=..." logs (if this fails, the logs just stay visible)
    if (!std::freopen("/dev/null", "w", stdout)) std::fprintf(stderr, "note: logs not hidden\n");
 
    RUN(test_plain_traffic);
    RUN(test_one_packet);
    RUN(test_split_everywhere);
    RUN(test_one_byte_at_a_time);
    RUN(test_partial_marker);
    RUN(test_partial_header);
    RUN(test_several_packets_one_read);
    RUN(test_truncated_packet_at_eof);
    RUN(test_duplicate_seq);
    RUN(test_truncated_header_at_eof);
    RUN(test_parse_header);

    std::fprintf(stderr, failures == 0 ? "\nAll tests passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;   // ctest reads this: 0 = pass
}
 