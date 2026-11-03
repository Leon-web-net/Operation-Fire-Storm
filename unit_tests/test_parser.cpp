#include "parser.hpp"
#include <cstdio>
#include <string>

// helper
int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); ++failures; } \
} while (0)

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

// TESTS


// 1. Normal traffic passes straight through.
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
 
int main() {
    // hide the parser's "CTFS seq=..." logs (if this fails, the logs just stay visible)
    if (!std::freopen("/dev/null", "w", stdout)) std::fprintf(stderr, "note: logs not hidden\n");
 
    test_plain_traffic();
    test_one_packet();
    test_split_everywhere();
    test_one_byte_at_a_time();
    test_partial_marker();
    test_partial_header();

    if (failures == 0) std::fprintf(stderr, "All tests passed\n");
    return failures == 0 ? 0 : 1;   // ctest reads this: 0 = pass
}
 