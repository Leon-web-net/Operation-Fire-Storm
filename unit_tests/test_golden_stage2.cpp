// Golden test, stage 2: the real capture, fed through feed_frames + route + reassembler, must rebuild
// 0.pdf..4.pdf and give each output port exactly the frames routes.txt says. DATA_DIR is set by CMake.
#include "frame.hpp"
#include "parser.hpp"
#include "reassembler.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); ++failures; } \
} while (0)


std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

uint32_t be32(const std::string& s, size_t at) {
    return (uint32_t(uint8_t(s[at])) << 24) | (uint32_t(uint8_t(s[at + 1])) << 16) |
           (uint32_t(uint8_t(s[at + 2])) << 8) | uint32_t(uint8_t(s[at + 3]));
}

// The records of a big-endian pcap (as written by test.py --pcap): one record per sent unit
std::vector<std::string> pcap_records(const std::string& pcap) {
    std::vector<std::string> records;
    size_t pos = 24;                              // skip the global header
    while (pos + 16 <= pcap.size()) {
        uint32_t len = be32(pcap, pos + 8);       // captured length
        records.push_back(pcap.substr(pos + 16, len));
        pos += 16 + len;
    }
    return records;
}

int main() {
    if (!std::freopen("/dev/null", "w", stdout)) std::fprintf(stderr, "note: logs not hidden\n");

    const fs::path data = fs::path(DATA_DIR) / "stage2";
    const std::vector<std::string> records = pcap_records(read_file(data / "stream.pcap"));
    CHECK(records.size() == 2813);

    std::vector<unsigned> routes;                 // one per non-CTFS record
    {
        std::ifstream in(data / "routes.txt");
        unsigned v;
        while (in >> v) routes.push_back(v);
    }

    std::string stream;
    std::string expected[3];                      // per port: TCP, MONITOR, UDP
    size_t frame_no = 0;
    for (const std::string& rec : records) {
        stream += rec;
        bool ctfs = rec.compare(0, 4, "CTFS") == 0 && rec.size() >= 16 &&
                    rec[13] == 0 && rec[14] == 0 && rec[15] == 0;
        if (ctfs) continue;
        CHECK(frame_no < routes.size());
        if (frame_no >= routes.size()) break;
        for (int bit = 0; bit < 3; ++bit)
            if (routes[frame_no] & (1u << bit)) expected[bit] += rec;
        ++frame_no;
    }
    CHECK(frame_no == routes.size());
    CHECK(routes.size() == 257);

    std::string expected_pdf[5];
    for (int i = 0; i < 5; ++i) {
        expected_pdf[i] = read_file(data / (std::to_string(i) + ".pdf"));
        CHECK(!expected_pdf[i].empty());
    }

    const fs::path dir = fs::temp_directory_path() / "firestorm_test_golden_stage2";
    for (size_t chunk : {1, 2, 3, 13, 16, 17, 18, 100, 1000, 1500, 65536}) {
        fs::remove_all(dir);
        fs::create_directories(dir);

        std::string received[3];
        bool spool_ok = true, ok = true;
        {
            Reassembler r;
            r.dir = dir.string();
            FrameFn on_frame = [&](const char* f, size_t len) {
                unsigned mask = route(parse_frame(f));
                for (int bit = 0; bit < 3; ++bit)
                    if (mask & (1u << bit)) received[bit].append(f, len);
                return true;
            };
            PacketFn on_packet = [&](const CtfsHeader& h, const char* payload) {
                if (add_packet(r, h, payload) == AddResult::WriteError) spool_ok = false;
            };

            Parser p;
            for (size_t pos = 0; ok && pos < stream.size(); pos += chunk) {
                ok = feed_frames(p, stream.data() + pos, std::min(chunk, stream.size() - pos), on_frame, on_packet);
            }
            finish_frames(p);
            CHECK(ok && spool_ok);
            CHECK(write_files(r));
        }

        int before = failures;
        for (int i = 0; i < 5; ++i) CHECK(read_file(dir / (std::to_string(i) + ".pdf")) == expected_pdf[i]);
        for (int bit = 0; bit < 3; ++bit) CHECK(received[bit] == expected[bit]);
        std::fprintf(stderr, "[%s] chunk size %zu\n", failures == before ? " OK " : "FAIL", chunk);
    }
    fs::remove_all(dir);

    std::fprintf(stderr, failures == 0 ? "\nAll tests passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
