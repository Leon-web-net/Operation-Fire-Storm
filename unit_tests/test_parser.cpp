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
    p += '\x01';                    // file ID
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

// the whole stream in one read, then end of stream
void run(const std::string& input) {
    reset();
    Parser p;
    feed(p, input.data(), input.size(), collect, collect_packet);
    finish(p, collect);
}


// TESTS

// 1. Normal traffic passes straight through.
// Expected: text with no CTFS marker is forwarded unchanged and no packet is emitted.
void test_plain_traffic() {
    run("hello world");

    CHECK(forwarded == "hello world");
    CHECK(packets.empty());
}

// 2. A CTFS packet is emitted, and NOT forwarded.
// Expected: the packet is handed to on_packet with its seq and payload; only the text around it is forwarded.
void test_one_packet() {
    run("before" + packet(0, "SECRET") + "after");

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
        CHECK(!packets.empty() && packets[0].payload == "SECRET");
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
    run(packet(0, "AAA") + "x" + packet(1, "BBB") + packet(2, "CCC"));

    CHECK(forwarded == "x");               // only the normal byte gets through
    CHECK(packets.size() == 3);
    CHECK(packets[0].payload == "AAA");
    CHECK(packets[1].payload == "BBB");
    CHECK(packets[2].payload == "CCC");
}

// 8. A truncated CTFS packet at the end of the stream is forwarded, not lost
// Expected: a packet cut off by the end of the stream is forwarded as normal data, not dropped.
void test_truncated_packet_at_eof() {
    std::string pkt = packet(0, "SECRET");
    std::string input = "before" + pkt.substr(0, pkt.size() - 3);
    run(input);

    CHECK(forwarded == input);             // the partial packet is forwarded, not lost
    CHECK(packets.empty());
}

// 9. A repeated seq is still emitted: deciding what to do with duplicates is the reassembler's job.
// Expected: both packets are handed out, in the order they arrived.
void test_duplicate_seq() {
    run(packet(5, "old") + packet(5, "new"));

    CHECK(packets.size() == 2);
    CHECK(packets[0].payload == "old");
    CHECK(packets[1].payload == "new");
}

// 10. The stream ends partway through a CTFS header: those bytes are forwarded too
// Expected: a header cut off by the end of the stream is forwarded as normal data, not dropped.
void test_truncated_header_at_eof() {
    std::string input = "before" + packet(0, "SECRET").substr(0, 10);  // marker + 6 header bytes
    run(input);

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

// 11. "CTFS" in normal traffic, followed by non-zero padding, is not a packet.
// Expected: all of it is forwarded unchanged, nothing emitted, and a real packet after it is still found.
void test_bad_padding_is_traffic() {
    std::string fake = packet(0, "DATA");
    fake[14] = 'x';   // padding byte 2 non-zero
    run("a" + fake + "b" + packet(1, "REAL"));

    CHECK(forwarded == "a" + fake + "b");
    CHECK(packets.size() == 1 && packets[0].payload == "REAL");
}

// 12. A real packet that starts inside the bytes of a fake header is still found.
// Expected: the fake "CTFS" prefix is forwarded, the real packet is emitted.
void test_packet_inside_fake_header() {
    std::string real = packet(2, "OK");
    real[10] = '\x7f';                   // non-zero checksum byte
    std::string input = "CTFS" + real;   // the fake header's padding is real's checksum bytes 9-11: non-zero
    for (size_t split = 0; split <= input.size(); ++split) {
        reset();
        Parser p;
        feed(p, input.data(), split, collect, collect_packet);
        feed(p, input.data() + split, input.size() - split, collect, collect_packet);
        finish(p, collect);
        CHECK(forwarded == "CTFS");
        CHECK(packets.size() == 1 && packets[0].payload == "OK");
    }
}

std::string unhex(const std::string& h) {
    std::string out;
    for (size_t i = 0; i + 1 < h.size(); i += 2) out += char(std::stoi(h.substr(i, 2), nullptr, 16));
    return out;
}

// 13. Checksum matches real Stage 2 packets.
// Expected: computed == stored for a zero-length and a 64-byte packet; a flipped payload bit changes it;
// a short last word is zero-padded (vector from a Python reference).
void test_checksum() {
    std::string empty = unhex("4354465302e2000001f761b64c000000");
    CtfsHeader h = parse_header(empty.data());
    CHECK(h.checksum == 0xf761b64cu);
    CHECK(ctfs_checksum(h, empty.data() + 16) == h.checksum);

    std::string full = unhex("435446530184004000f42f8881000000ce75507b235594412a55245889f2ca2abd93f0a613ce15f8"
                             "585bd423845e3f63d3de65b51b6b6a36d62edbbb69c6e4ee3de3e37b6aeaadd67ab27ad5952b52a2");
    h = parse_header(full.data());
    CHECK(ctfs_checksum(h, full.data() + 16) == h.checksum);
    full[20] ^= 1;
    CHECK(ctfs_checksum(h, full.data() + 16) != h.checksum);

    CtfsHeader s{};
    s.seq = 7; s.len = 3; s.file_id = 9;
    CHECK(ctfs_checksum(s, "abc") == 0xc3d832cau);
}

// STAGE 2 (length framing)

// Ethernet II + IPv4 frame with the given payload size (protocol 6, MACs zero)
std::string eth_frame(size_t payload_size, char fill) {
    std::string f(12, '\0');
    f += '\x08'; f += '\0';                         // ether type IPv4 (0x0800)
    size_t total = 20 + payload_size;
    std::string ip(20, '\0');
    ip[0] = 0x45;                                       // version 4, IHL 5
    ip[2] = char(total >> 8); ip[3] = char(total & 0xFF);
    ip[9] = 6;                                          // TCP
    return f + ip + std::string(payload_size, fill);
}

std::vector<std::string> frames;
FrameFn collect_frame = [](const char* data, size_t len) {
    frames.emplace_back(data, len);
    return true;
};

// feed input split once at `split`, then end of stream; returns feed's results ANDed
bool run_frames(const std::string& input, size_t split) {
    reset();
    frames.clear();
    Parser p;
    bool ok = feed_frames(p, input.data(), split, collect_frame, collect_packet);
    ok = ok && feed_frames(p, input.data() + split, input.size() - split, collect_frame, collect_packet);
    finish_frames(p);
    return ok;
}

// 14. Frames and packets back to back, split at every byte.
// Expected: each frame emitted whole and in order, the packet emitted, nothing else.
void test_frames_split_everywhere() {
    std::string f1 = eth_frame(10, 'a'), f2 = eth_frame(0, 'b'), f3 = eth_frame(300, 'c');
    std::string input = f1 + packet(4, "PDFDATA") + f2 + packet(5, "") + f3;
    for (size_t split = 0; split <= input.size(); ++split) {
        CHECK(run_frames(input, split));
        CHECK(frames.size() == 3 && frames[0] == f1 && frames[1] == f2 && frames[2] == f3);
        CHECK(packets.size() == 2 && packets[0].payload == "PDFDATA" && packets[1].seq == 5);
        CHECK(forwarded.empty());
    }
}

// 15. Bytes that are neither a packet nor a frame stop the run.
// Expected: feed_frames returns false; the frame before it was still emitted.
void test_frames_invalid() {
    std::string bad = eth_frame(4, 'x');
    bad[12] = '\x86'; bad[13] = '\xDD';                  // IPv6 ether type
    CHECK(!run_frames(eth_frame(4, 'a') + bad, 0));
    CHECK(frames.size() == 1);

    std::string fake = packet(0, "DATA");
    fake[15] = 1;                                       // bad padding: read as a frame, which it isn't
    CHECK(!run_frames(fake, 0));
    CHECK(packets.empty() && frames.empty());
}

// 16. Stream ends inside a frame.
// Expected: the complete frame is emitted, the partial one dropped, no error.
void test_frames_truncated() {
    std::string f = eth_frame(8, 'a');
    std::string input = f + f.substr(0, 20);
    CHECK(run_frames(input, input.size()));
    CHECK(frames.size() == 1 && frames[0] == f);
}

int main() {
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
    RUN(test_bad_padding_is_traffic);
    RUN(test_packet_inside_fake_header);
    RUN(test_checksum);
    RUN(test_frames_split_everywhere);
    RUN(test_frames_invalid);
    RUN(test_frames_truncated);

    std::fprintf(stderr, failures == 0 ? "\nAll tests passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;   // ctest reads this: 0 = pass
}
