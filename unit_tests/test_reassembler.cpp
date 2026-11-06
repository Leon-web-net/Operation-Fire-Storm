#include "reassembler.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
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

namespace fs = std::filesystem;

// All test folders live under here; deleted at the end of main
const fs::path TEST_ROOT = fs::temp_directory_path() / "firestorm_test_reassembler";

// Empty temp folder for one test
fs::path fresh_dir(const std::string& name) {
    fs::path dir = TEST_ROOT / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_file(const fs::path& path, const std::string& contents) {
    std::ofstream(path, std::ios::binary) << contents;
}

// Give the reassembler one packet, as if the parser had just emitted it
AddResult add(Reassembler& r, uint8_t file_id, uint16_t seq, const std::string& payload) {
    CtfsHeader h{};
    h.seq = seq;
    h.len = static_cast<uint16_t>(payload.size());
    h.file_id = file_id;
    return add_packet(r, h, payload.data());
}

// Payload stored for (file_id, seq), read back from the spool
std::string stored(Reassembler& r, uint8_t file_id, uint16_t seq) {
    SpoolFile& f = r.files.at(file_id);
    std::vector<char> out;
    if (!read_payload(f, f.index.at(seq), out)) return "<read error>";
    return std::string(out.begin(), out.end());
}


// TESTS

// 1. A repeated seq does not replace data already received.
// Expected: Duplicate is returned, the first copy is kept, and nothing more is written to the spool.
void test_duplicate_keeps_first() {
    Reassembler r;
    r.dir = fresh_dir("dup").string();

    CHECK(add(r, 1, 5, "old") == AddResult::Stored);
    uint64_t size_before = r.files[1].size;
    CHECK(add(r, 1, 5, "new") == AddResult::Duplicate);

    CHECK(r.files[1].size == size_before);
    CHECK(r.files[1].index.size() == 1);
    CHECK(stored(r, 1, 5) == "old");
}

// 2. Two files in the same stream get separate spools.
// Expected: the same seq in different file IDs is not a duplicate; each spool holds its own data;
// the spools are deleted when the reassembler goes away.
void test_two_file_ids_kept_separate() {
    fs::path dir = fresh_dir("two_ids");
    {
        Reassembler r;
        r.dir = dir.string();

        CHECK(add(r, 1, 0, "file1-a") == AddResult::Stored);
        CHECK(add(r, 2, 0, "file2-a") == AddResult::Stored);   // same seq, different file: allowed
        CHECK(add(r, 1, 1, "file1-b") == AddResult::Stored);

        CHECK(r.files.size() == 2);
        CHECK(r.files[1].index.size() == 2);
        CHECK(r.files[2].index.size() == 1);
        CHECK(stored(r, 1, 0) == "file1-a");
        CHECK(stored(r, 1, 1) == "file1-b");
        CHECK(stored(r, 2, 0) == "file2-a");
        CHECK(fs::exists(dir / "1.spool"));
        CHECK(fs::exists(dir / "2.spool"));
    }
    CHECK(!fs::exists(dir / "1.spool"));
    CHECK(!fs::exists(dir / "2.spool"));
}

// 3. Packets that arrive out of order come out of the index in seq order.
void test_out_of_order_stored_in_order() {
    Reassembler r;
    r.dir = fresh_dir("order").string();

    add(r, 1, 2, "C");
    add(r, 1, 0, "A");
    add(r, 1, 1, "B");

    std::string joined;
    for (const auto& [seq, slice] : r.files[1].index) {
        (void)slice;
        joined += stored(r, 1, seq);
    }
    CHECK(joined == "ABC");
}

// 4. The spool can't be created (folder doesn't exist).
// Expected: WriteError, not a crash.
void test_write_error_when_dir_missing() {
    Reassembler r;
    r.dir = (fresh_dir("no_dir") / "missing").string();

    CHECK(add(r, 1, 0, "data") == AddResult::WriteError);
}

// 5. write_files puts out-of-order packets back in seq order.
// Expected: 1.pdf holds the payloads in seq order.
void test_write_files_in_seq_order() {
    fs::path dir = fresh_dir("write_order");
    Reassembler r;
    r.dir = dir.string();

    add(r, 1, 2, "CCC");
    add(r, 1, 0, "AAA");
    add(r, 1, 1, "BBB");

    CHECK(write_files(r));
    CHECK(read_file(dir / "1.pdf") == "AAABBBCCC");
}

// 6. Two file IDs give two output files.
void test_write_files_two_ids() {
    fs::path dir = fresh_dir("write_two");
    Reassembler r;
    r.dir = dir.string();

    add(r, 1, 0, "one");
    add(r, 2, 0, "two");

    CHECK(write_files(r));
    CHECK(read_file(dir / "1.pdf") == "one");
    CHECK(read_file(dir / "2.pdf") == "two");
}

// 7. After a successful write, only the PDF is left: no .spool, no .tmp.
void test_no_leftovers_after_write() {
    fs::path dir = fresh_dir("leftovers");
    Reassembler r;
    r.dir = dir.string();

    add(r, 1, 0, "data");
    CHECK(write_files(r));

    CHECK(fs::exists(dir / "1.pdf"));
    CHECK(!fs::exists(dir / "1.spool"));
    CHECK(!fs::exists(dir / "1.pdf.tmp"));
}

// 8. A new write fully replaces an old, longer file (no leftover bytes at the end).
void test_replaces_existing_file() {
    fs::path dir = fresh_dir("replace");
    write_file(dir / "1.pdf", "OLD CONTENTS THAT ARE LONGER");
    Reassembler r;
    r.dir = dir.string();

    add(r, 1, 0, "new");
    CHECK(write_files(r));
    CHECK(read_file(dir / "1.pdf") == "new");
}

// 9. If the final write fails, an existing good 1.pdf is left untouched.
void test_failed_write_keeps_old_file() {
    fs::path dir = fresh_dir("keep_old");
    write_file(dir / "1.pdf", "GOOD PDF");
    // A folder called 1.pdf.tmp means the temp file can't be opened, so the write must fail
    fs::create_directory(dir / "1.pdf.tmp");
    Reassembler r;
    r.dir = dir.string();

    add(r, 1, 0, "partial");
    CHECK(!write_files(r));
    CHECK(read_file(dir / "1.pdf") == "GOOD PDF");
}

// 10. A zero-length payload (like the stream's last packet) is stored and written without problems.
void test_zero_length_payload() {
    fs::path dir = fresh_dir("zero_len");
    Reassembler r;
    r.dir = dir.string();

    CHECK(add(r, 1, 0, "AB") == AddResult::Stored);
    CHECK(add(r, 1, 1, "") == AddResult::Stored);
    CHECK(r.files[1].index.size() == 2);

    CHECK(write_files(r));
    CHECK(read_file(dir / "1.pdf") == "AB");
}

int main() {
    // hide the "Wrote N CTFS chunks" logs
    if (!std::freopen("/dev/null", "w", stdout)) std::fprintf(stderr, "note: logs not hidden\n");

    RUN(test_duplicate_keeps_first);
    RUN(test_two_file_ids_kept_separate);
    RUN(test_out_of_order_stored_in_order);
    RUN(test_write_error_when_dir_missing);
    RUN(test_write_files_in_seq_order);
    RUN(test_write_files_two_ids);
    RUN(test_no_leftovers_after_write);
    RUN(test_replaces_existing_file);
    RUN(test_failed_write_keeps_old_file);
    RUN(test_zero_length_payload);

    fs::remove_all(TEST_ROOT);

    std::fprintf(stderr, failures == 0 ? "\nAll tests passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;   // ctest reads this: 0 = pass
}
