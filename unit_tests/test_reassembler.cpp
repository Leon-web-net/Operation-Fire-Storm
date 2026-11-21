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
// the spools are kept when the reassembler goes away without writing (so --recover can use them).
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
        CHECK(fs::exists(dir / "spool" / "1.spool"));
        CHECK(fs::exists(dir / "spool" / "2.spool"));
    }
    CHECK(fs::exists(dir / "spool" / "1.spool"));
    CHECK(fs::exists(dir / "spool" / "2.spool"));
}

// 3. The spool can't be created (folder doesn't exist).
// Expected: WriteError, not a crash.
void test_write_error_when_dir_missing() {
    Reassembler r;
    r.dir = (fresh_dir("no_dir") / "missing").string();

    CHECK(add(r, 1, 0, "data") == AddResult::WriteError);
}

// 4. After a write error the spool is marked failed and never retried.
// Expected: even once the folder exists, later packets get WriteError and no spool is created.
void test_failed_spool_not_retried() {
    fs::path dir = fresh_dir("no_retry") / "missing";
    Reassembler r;
    r.dir = dir.string();

    CHECK(add(r, 1, 0, "data") == AddResult::WriteError);
    fs::create_directories(dir);
    CHECK(add(r, 1, 1, "more") == AddResult::WriteError);
    CHECK(!fs::exists(dir / "spool" / "1.spool"));
}

// 5. A failed spool is never turned into a PDF.
// Expected: write_files returns false and writes no 1.pdf, but other files still get written.
void test_write_files_skips_failed_spool() {
    fs::path dir = fresh_dir("skip_failed");
    Reassembler r;
    r.dir = dir.string();

    add(r, 1, 0, "bad");
    add(r, 2, 0, "good");
    r.files[1].failed = true;              // as if a later write to 1.spool had failed

    CHECK(!write_files(r));
    CHECK(!fs::exists(dir / "1.pdf"));
    CHECK(read_file(dir / "2.pdf") == "good");
}

// 6. write_files puts out-of-order packets back in seq order.
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

// 7. Two file IDs give two output files.
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

// 8. After a successful write, only the PDF is left: no .spool, no .tmp.
void test_no_leftovers_after_write() {
    fs::path dir = fresh_dir("leftovers");
    Reassembler r;
    r.dir = dir.string();

    add(r, 1, 0, "data");
    CHECK(write_files(r));

    CHECK(fs::exists(dir / "1.pdf"));
    CHECK(!fs::exists(dir / "spool" / "1.spool"));
    CHECK(!fs::exists(dir / "1.pdf.tmp"));
}

// 9. A new write fully replaces an old, longer file (no leftover bytes at the end).
void test_replaces_existing_file() {
    fs::path dir = fresh_dir("replace");
    write_file(dir / "1.pdf", "OLD CONTENTS THAT ARE LONGER");
    Reassembler r;
    r.dir = dir.string();

    add(r, 1, 0, "new");
    CHECK(write_files(r));
    CHECK(read_file(dir / "1.pdf") == "new");
}

// 10. If the final write fails, an existing good 1.pdf is left untouched.
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

// 11. A zero-length payload (like the stream's last packet) is stored and written without problems.
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

// 12. Each record is on disk (handed to the OS) as soon as add_packet returns, not sitting in a buffer.
// Expected: the spool file's size already matches, while it is still open.
void test_records_flushed_immediately() {
    fs::path dir = fresh_dir("flushed");
    Reassembler r;
    r.dir = dir.string();

    add(r, 1, 0, "hello");
    CHECK(fs::file_size(dir / "spool" / "1.spool") == 4 + 5);
}

// 13. A spool left by an earlier run is renamed to .old, not deleted, and a new run starts clean.
// Expected: 1.spool.old holds the old data, the new 1.pdf only the new data.
void test_old_spool_kept() {
    fs::path dir = fresh_dir("old_spool");
    {
        Reassembler crashed;                   // goes away without write_files, like a killed run
        crashed.dir = dir.string();
        add(crashed, 1, 0, "OLD");
    }
    Reassembler r;
    r.dir = dir.string();
    add(r, 1, 0, "new");

    CHECK(fs::file_size(dir / "spool" / "1.spool.old") == 4 + 3);
    CHECK(write_files(r));
    CHECK(read_file(dir / "1.pdf") == "new");
    CHECK(fs::exists(dir / "spool" / "1.spool.old"));    // untouched: only --recover uses it
}

// 14. Recovery after a crash: out-of-order records, a duplicate, and a record torn by the crash.
// Expected: 1.partial.pdf holds the complete records in seq order; the torn one is ignored; spool deleted.
void test_recover_after_crash() {
    fs::path dir = fresh_dir("recover");
    {
        Reassembler crashed;
        crashed.dir = dir.string();
        add(crashed, 1, 2, "CCC");
        add(crashed, 1, 0, "AAA");
        add(crashed, 1, 1, "BBB");
    }
    // Simulate dying mid-write: a header that promises 100 bytes, followed by only 5
    std::ofstream(dir / "spool" / "1.spool", std::ios::binary | std::ios::app) << std::string("\x00\x03\x00\x64", 4) << "torn!";

    Reassembler r;
    r.dir = dir.string();
    CHECK(recover_files(r));
    CHECK(r.files[1].index.size() == 3);
    CHECK(read_file(dir / "1.partial.pdf") == "AAABBBCCC");
    CHECK(!fs::exists(dir / "1.pdf"));
    CHECK(!fs::exists(dir / "spool" / "1.spool"));
}

// 15. Recovery falls back to <id>.spool.old when there is no newer <id>.spool.
void test_recover_uses_old_spool() {
    fs::path dir = fresh_dir("recover_old");
    {
        Reassembler crashed;
        crashed.dir = dir.string();
        add(crashed, 2, 0, "older");
    }
    fs::rename(dir / "spool" / "2.spool", dir / "spool" / "2.spool.old");

    Reassembler r;
    r.dir = dir.string();
    CHECK(recover_files(r));
    CHECK(read_file(dir / "2.partial.pdf") == "older");
}

// 16. A normal run with a missing seq is not given the real name.
// Expected: 1.partial.pdf, no 1.pdf.
void test_gap_gives_partial_pdf() {
    fs::path dir = fresh_dir("gap");
    Reassembler r;
    r.dir = dir.string();

    add(r, 1, 0, "A");
    add(r, 1, 2, "C");                         // seq 1 never arrived
    CHECK(write_files(r));
    CHECK(read_file(dir / "1.partial.pdf") == "AC");
    CHECK(!fs::exists(dir / "1.pdf"));
}

// 17. An existing <id>.spool.old is never overwritten.
// Expected: WriteError (now and for later packets), both files unchanged.
void test_existing_old_not_overwritten() {
    fs::path dir = fresh_dir("old_exists");
    fs::create_directory(dir / "spool");
    write_file(dir / "spool" / "1.spool", "NEWER");
    write_file(dir / "spool" / "1.spool.old", "OLDER");
    Reassembler r;
    r.dir = dir.string();

    CHECK(add(r, 1, 0, "x") == AddResult::WriteError);
    CHECK(add(r, 1, 1, "y") == AddResult::WriteError);
    CHECK(read_file(dir / "spool" / "1.spool") == "NEWER");
    CHECK(read_file(dir / "spool" / "1.spool.old") == "OLDER");
}

// 18. If renaming the old spool fails, the add fails.
// Expected: WriteError. Uses a read-only spool folder, so it is skipped when running as root.
void test_rename_failure() {
    fs::path dir = fresh_dir("rename_fail");
    fs::create_directory(dir / "spool");
    write_file(dir / "spool" / "1.spool", "OLD");
    fs::permissions(dir / "spool", fs::perms::owner_read | fs::perms::owner_exec);

    // Probe: if a file can still be created, permissions aren't enforced (root)
    std::FILE* probe = std::fopen((dir / "spool" / "probe").string().c_str(), "wb");
    if (probe) {
        std::fclose(probe);
        fs::permissions(dir / "spool", fs::perms::owner_all);
        std::fprintf(stderr, "skipped: folder permissions not enforced (running as root?)\n");
        return;
    }

    Reassembler r;
    r.dir = dir.string();
    CHECK(add(r, 1, 0, "new") == AddResult::WriteError);
    fs::permissions(dir / "spool", fs::perms::owner_all);   // so cleanup works
    CHECK(read_file(dir / "spool" / "1.spool") == "OLD");
}

// 19. After a clean write_files the empty spool folder is removed.
void test_empty_spool_dir_removed() {
    fs::path dir = fresh_dir("rm_spool_dir");
    Reassembler r;
    r.dir = dir.string();

    add(r, 1, 0, "data");
    CHECK(write_files(r));
    CHECK(fs::exists(dir / "1.pdf"));
    CHECK(!fs::exists(dir / "spool"));
}

// 20. A spool folder that still holds other files is never removed.
void test_nonempty_spool_dir_kept() {
    fs::path dir = fresh_dir("keep_spool_dir");
    fs::create_directory(dir / "spool");
    write_file(dir / "spool" / "9.spool.old", "OTHER");
    Reassembler r;
    r.dir = dir.string();

    add(r, 1, 0, "data");
    CHECK(write_files(r));
    CHECK(fs::exists(dir / "1.pdf"));
    CHECK(read_file(dir / "spool" / "9.spool.old") == "OTHER");
}

int main() {
    // hide the "Wrote N CTFS chunks" logs
    if (!std::freopen("/dev/null", "w", stdout)) std::fprintf(stderr, "note: logs not hidden\n");

    RUN(test_duplicate_keeps_first);
    RUN(test_two_file_ids_kept_separate);
    RUN(test_write_error_when_dir_missing);
    RUN(test_failed_spool_not_retried);
    RUN(test_write_files_skips_failed_spool);
    RUN(test_write_files_in_seq_order);
    RUN(test_write_files_two_ids);
    RUN(test_no_leftovers_after_write);
    RUN(test_replaces_existing_file);
    RUN(test_failed_write_keeps_old_file);
    RUN(test_zero_length_payload);
    RUN(test_records_flushed_immediately);
    RUN(test_old_spool_kept);
    RUN(test_recover_after_crash);
    RUN(test_recover_uses_old_spool);
    RUN(test_gap_gives_partial_pdf);
    RUN(test_existing_old_not_overwritten);
    RUN(test_rename_failure);
    RUN(test_empty_spool_dir_removed);
    RUN(test_nonempty_spool_dir_kept);

    fs::remove_all(TEST_ROOT);

    std::fprintf(stderr, failures == 0 ? "\nAll tests passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;   // ctest reads this: 0 = pass
}
