#include "parser.hpp"
#include <cstdio>
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

std::string payload(Parser& p, int seq) {
    return std::string(p.chunks[seq].begin(), p.chunks[seq].end());
}


// TESTS

// 1. Normal traffic passes straight through.
// Expected: text with no CTFS marker is forwarded unchanged and nothing is stored.
void test_plain_traffic() {
    forwarded.clear();
    Parser p;
    std::string input = "hello world";
 
    feed(p, input.data(), input.size(), collect);
    finish(p, collect);
 
    CHECK(forwarded == "hello world");
    CHECK(p.chunks.empty());
}
 
// 2. A CTFS packet is stored, and NOT forwarded.
// Expected: the packet's payload is stored under its seq; only the text around it is forwarded.
void test_one_packet() {
    forwarded.clear();
    Parser p;
    std::string input = "before" + packet(0, "SECRET") + "after";
 
    feed(p, input.data(), input.size(), collect);
    finish(p, collect);
 
    CHECK(forwarded == "beforeafter");
    CHECK(p.chunks.size() == 1);
    CHECK(std::string(p.chunks[0].begin(), p.chunks[0].end()) == "SECRET");
}
 
// 3. TCP can split data anywhere. Cut the input into two recv()s at every position.
// Expected: wherever TCP splits the data into two reads, the result is the same as one read.
void test_split_everywhere() {
    std::string input = "before" + packet(0, "SECRET") + "after";
 
    for (size_t cut = 1; cut < input.size(); ++cut) {
        forwarded.clear();
        Parser p;
 
        feed(p, input.data(), cut, collect);                        
        feed(p, input.data() + cut, input.size() - cut, collect);   
        finish(p, collect);
 
        CHECK(forwarded == "beforeafter");
        CHECK(p.chunks.size() == 1);
    }
}

// 4. Parser handles incoming data one byte at a time
// Expected: feeding one byte per read still gives the same result as one big read.
void test_one_byte_at_a_time() {
    std::string input = "before" + packet(0, "SECRET") + "after";
 
    forwarded.clear();
    Parser p;
 
    for (size_t i = 0; i < input.size(); ++i) {
        feed(p, input.data() + i, 1, collect);
    }
    finish(p, collect);
 
    CHECK(forwarded == "beforeafter");
    CHECK(p.chunks.size() == 1);
}

// 5. A partial CTFS marker that turns out not to be a marker is forwarded as normal traffic
// Expected: a trailing "CT" is held back in case it's the start of CTFS, then forwarded once it isn't.
void test_partial_marker(){
    forwarded.clear();
    Parser p;
    
    std::string first = "beforeCT";
    feed(p,first.data(), first.size(), collect);
    CHECK(forwarded == "before");

    std::string second = "X";
    feed(p,second.data(), second.size(), collect);
    CHECK(forwarded == "beforeCTX");
    CHECK(p.chunks.empty());

}

// 6. An incomplete CTFS header is retained until the remaining bytes arrive
//  Expected: with only part of a header received, nothing is stored or forwarded until the rest arrives.
void test_partial_header() {
    forwarded.clear();
    Parser p;
    std::string pkt = packet(0, "SECRET");

    std::string first  = "before" + pkt.substr(0, 10);   
    std::string second = pkt.substr(10) + "after";      

    feed(p, first.data(), first.size(), collect);
    CHECK(forwarded == "before");          
    CHECK(p.chunks.empty());              

    feed(p, second.data(), second.size(), collect);
    finish(p, collect);
    CHECK(forwarded == "beforeafter");
    CHECK(p.chunks.size() == 1);           
}
// 7. Several CTFS packets can be received in one read, and the normal traffic between them is forwarded
// Expected: every packet in a single read is stored, and the normal byte between them is forwarded.
void test_several_packets_one_read() {
    forwarded.clear();
    Parser p;
    std::string input = packet(0, "AAA") + "x" + packet(1, "BBB") + packet(2, "CCC");

    feed(p, input.data(), input.size(), collect);
    finish(p, collect);

    CHECK(forwarded == "x");               // only the normal byte gets through
    CHECK(p.chunks.size() == 3);
    CHECK(payload(p, 0) == "AAA");
    CHECK(payload(p, 1) == "BBB");
    CHECK(payload(p, 2) == "CCC");
}


// 8. A truncated CTFS packet at the end of the stream is forwarded, not lost
// Expected: a packet cut off by the end of the stream is forwarded as normal data, not dropped.
void test_truncated_packet_at_eof() {
    forwarded.clear();
    Parser p;
    std::string pkt = packet(0, "SECRET");
    std::string input = "before" + pkt.substr(0, pkt.size() - 3);

    feed(p, input.data(), input.size(), collect);
    finish(p, collect);

    CHECK(forwarded == input);             // the partial packet is forwarded, not lost
    CHECK(p.chunks.empty());
}

// 9. A repeated seq must not silently replace data already received.
// Expected: when a seq arrives twice, the first payload is kept and the duplicate is ignored.
void test_duplicate_seq() {
    forwarded.clear();
    Parser p;
    std::string input = packet(5, "old") + packet(5, "new");

    feed(p, input.data(), input.size(), collect);
    finish(p, collect);

    CHECK(p.chunks.size() == 1);
    CHECK(payload(p, 5) == "old");         // first one kept, duplicate ignored
}

// 10. The stream ends partway through a CTFS header: those bytes are forwarded too
// Expected: a header cut off by the end of the stream is forwarded as normal data, not dropped.
void test_truncated_header_at_eof() {
    forwarded.clear();
    Parser p;
    std::string input = "before" + packet(0, "SECRET").substr(0, 10);  // marker + 6 header bytes

    feed(p, input.data(), input.size(), collect);
    finish(p, collect);

    CHECK(forwarded == input);
    CHECK(p.chunks.empty());
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

    std::fprintf(stderr, failures == 0 ? "\nAll tests passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;   // ctest reads this: 0 = pass
}
 