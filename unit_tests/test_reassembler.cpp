#include "reassembler.hpp"
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

// Give the reassembler one packet, as if the parser had just emitted it
bool add(Reassembler& r, uint8_t file_id, uint16_t seq, const std::string& payload) {
    CtfsHeader h{};
    h.seq = seq;
    h.len = static_cast<uint16_t>(payload.size());
    h.file_id = file_id;
    return add_packet(r, h, payload.data());
}

// Payload stored for (file_id, seq), as a string
std::string stored(Reassembler& r, uint8_t file_id, uint16_t seq) {
    const auto& chunk = r.files[file_id][seq];
    return std::string(chunk.begin(), chunk.end());
}


// TESTS

// 1. A repeated seq does not replace data already received.
// Expected: the first copy is kept, and add_packet reports the duplicate by returning false.
void test_duplicate_keeps_first() {
    Reassembler r;

    CHECK(add(r, 1, 5, "old"));
    CHECK(!add(r, 1, 5, "new"));           // duplicate rejected

    CHECK(r.files[1].size() == 1);
    CHECK(stored(r, 1, 5) == "old");
}

// 2. Two files in the same stream are kept apart.
// Expected: the same seq in different file IDs is not a duplicate; each file gets its own chunks.
void test_two_file_ids_kept_separate() {
    Reassembler r;

    CHECK(add(r, 1, 0, "file1-a"));
    CHECK(add(r, 2, 0, "file2-a"));        // same seq, different file: allowed
    CHECK(add(r, 1, 1, "file1-b"));

    CHECK(r.files.size() == 2);
    CHECK(r.files[1].size() == 2);
    CHECK(r.files[2].size() == 1);
    CHECK(stored(r, 1, 0) == "file1-a");
    CHECK(stored(r, 2, 0) == "file2-a");
}

// 3. Packets that arrive out of order are stored in seq order.
// Expected: iterating a file's chunks gives seq 0, 1, 2 whatever order they were added in.
void test_out_of_order_stored_in_order() {
    Reassembler r;

    add(r, 1, 2, "C");
    add(r, 1, 0, "A");
    add(r, 1, 1, "B");

    std::string joined;
    for (const auto& [seq, payload] : r.files[1]) {
        (void)seq;
        joined.append(payload.begin(), payload.end());
    }
    CHECK(joined == "ABC");
}

int main() {
    RUN(test_duplicate_keeps_first);
    RUN(test_two_file_ids_kept_separate);
    RUN(test_out_of_order_stored_in_order);

    std::fprintf(stderr, failures == 0 ? "\nAll tests passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;   // ctest reads this: 0 = pass
}
