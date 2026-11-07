// Golden test: the real stage-1 capture, fed through parser + reassembler, must rebuild expected.pdf exactly.
// DATA_DIR is set by CMake (unit_tests/data).
#include "parser.hpp"
#include "reassembler.hpp"
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

    const std::string expected_pdf = read_file(fs::path(DATA_DIR) / "expected.pdf");
    const std::vector<std::string> records = pcap_records(read_file(fs::path(DATA_DIR) / "stream.pcap"));
    CHECK(!expected_pdf.empty());
    CHECK(records.size() == 277);

    std::string stream, expected_forwarded;       // the TCP byte stream, and its non-CTFS part
    for (const std::string& rec : records) {
        stream += rec;
        if (rec.compare(0, 4, "CTFS") != 0) expected_forwarded += rec;
    }

    const fs::path dir = fs::temp_directory_path() / "firestorm_test_golden";
    for (size_t chunk : {1, 2, 3, 5, 15, 16, 17, 100, 1000, 1500, 65536}) {
        fs::remove_all(dir);
        fs::create_directories(dir);

        std::string forwarded;
        ForwardFn forward = [&](const char* data, size_t len) { forwarded.append(data, len); return true; };
        bool spool_ok = true;
        {
            Reassembler r;
            r.dir = dir.string();
            PacketFn on_packet = [&](const CtfsHeader& h, const char* payload) {
                if (add_packet(r, h, payload) == AddResult::WriteError) spool_ok = false;
            };

            Parser p;
            bool ok = true;
            for (size_t pos = 0; ok && pos < stream.size(); pos += chunk) {
                ok = feed(p, stream.data() + pos, std::min(chunk, stream.size() - pos), forward, on_packet);
            }
            CHECK(ok && finish(p, forward) && spool_ok);
            CHECK(write_files(r));
        }

        int before = failures;
        CHECK(read_file(dir / "1.pdf") == expected_pdf);
        CHECK(forwarded == expected_forwarded);
        std::fprintf(stderr, "[%s] chunk size %zu\n", failures == before ? " OK " : "FAIL", chunk);
    }
    fs::remove_all(dir);

    std::fprintf(stderr, failures == 0 ? "\nAll tests passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
