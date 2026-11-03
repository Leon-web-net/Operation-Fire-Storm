#include "pdf.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

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

using Chunks = std::map<uint16_t, std::vector<char>>;

std::vector<char> bytes(const std::string& s) {
    return std::vector<char>(s.begin(), s.end());
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_file(const fs::path& path, const std::string& contents) {
    std::ofstream(path, std::ios::binary) << contents;
}

// Each test gets its own empty folder, so tests can't affect each other or the repo
fs::path fresh_dir(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / ("firestorm_test_pdf_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// TESTS


// 1. Chunks are written in sequence-number order, whatever order they arrived in
void test_writes_in_seq_order() {
    fs::path dir = fresh_dir("order");
    fs::path out = dir / "output.pdf";
    Chunks chunks;
    chunks[2] = bytes("CCC");
    chunks[0] = bytes("AAA");
    chunks[1] = bytes("BBB");

    CHECK(write_pdf(chunks, out.c_str()));
    CHECK(read_file(out) == "AAABBBCCC");
}

// 2. After a successful write, no .tmp file is left behind
void test_no_tmp_left_after_success() {
    fs::path dir = fresh_dir("no_tmp");
    fs::path out = dir / "output.pdf";
    Chunks chunks;
    chunks[0] = bytes("data");

    CHECK(write_pdf(chunks, out.c_str()));
    CHECK(fs::exists(out));
    CHECK(!fs::exists(dir / "output.pdf.tmp"));
}

// 3. A new write fully replaces an old, longer file (no leftover bytes at the end)
void test_replaces_existing_file() {
    fs::path dir = fresh_dir("replace");
    fs::path out = dir / "output.pdf";
    write_file(out, "OLD CONTENTS THAT ARE LONGER");
    Chunks chunks;
    chunks[0] = bytes("new");

    CHECK(write_pdf(chunks, out.c_str()));
    CHECK(read_file(out) == "new");
}

// 4. If the write fails, an existing good file is left untouched
void test_failed_write_keeps_old_file() {
    fs::path dir = fresh_dir("keep_old");
    fs::path out = dir / "output.pdf";
    write_file(out, "GOOD PDF");
    // A folder called output.pdf.tmp means the temp file can't be opened, so the write must fail
    fs::create_directory(dir / "output.pdf.tmp");
    Chunks chunks;
    chunks[0] = bytes("partial");

    CHECK(!write_pdf(chunks, out.c_str()));
    CHECK(read_file(out) == "GOOD PDF");
}

// 5. Writing into a folder that doesn't exist fails cleanly instead of crashing
void test_missing_folder_fails() {
    fs::path dir = fresh_dir("missing");
    fs::path out = dir / "no_such_folder" / "output.pdf";
    Chunks chunks;
    chunks[0] = bytes("data");

    CHECK(!write_pdf(chunks, out.c_str()));
    CHECK(!fs::exists(out));
}

int main() {
    // hide the "Wrote N CTFS chunks" logs and expected error messages
    if (!std::freopen("/dev/null", "w", stdout)) std::fprintf(stderr, "note: logs not hidden\n");

    RUN(test_writes_in_seq_order);
    RUN(test_no_tmp_left_after_success);
    RUN(test_replaces_existing_file);
    RUN(test_failed_write_keeps_old_file);
    RUN(test_missing_folder_fails);

    for (const char* name : {"order", "no_tmp", "replace", "keep_old", "missing"}) {
        fs::remove_all(fs::temp_directory_path() / (std::string("firestorm_test_pdf_") + name));
    }

    std::fprintf(stderr, failures == 0 ? "\nAll tests passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;   // ctest reads this: 0 = pass
}
