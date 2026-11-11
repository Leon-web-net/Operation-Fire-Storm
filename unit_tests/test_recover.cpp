// Recovery from real crash spools.
// Every <id>.spool in unit_tests/data/ was left by killing firestorm mid-run, before any PDF was written.
// This test rebuilds each one into <project>/<id>.partial.pdf, and if unit_tests/data/<id>.pdf exists,
// checks the result against it byte for byte.
//
// <id> must be a file ID (0-255, no leading zeros), because recover_files reads the ID from the name;
// any other .spool name fails the test instead of being skipped silently.
// Recovery deletes the spools it used, so the test copies the fixtures into <project>/spool/ and recovers
// from the copies: the fixtures themselves are never touched, and the test can be run any number of times.
// PROJECT_DIR and DATA_DIR are set by CMake.
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

// "0".."255" exactly as recover_files expects (so "007", "300" and "expected" are rejected)
bool is_file_id(const std::string& s) {
    if (s.empty() || s.size() > 3 || s.find_first_not_of("0123456789") != std::string::npos) return false;
    return std::stoi(s) <= 255 && std::to_string(std::stoi(s)) == s;
}

int main() {
    const fs::path project = PROJECT_DIR, data = DATA_DIR, spool = project / "spool";

    // recover_files rebuilds EVERY spool in the folder: never touch spools left by a real crash
    fs::create_directories(spool);
    if (!fs::is_empty(spool)) {
        std::fprintf(stderr, "FAIL: %s is not empty (spools from a real crash?). Recover or move them first.\n",
            spool.c_str());
        return 1;
    }

    // Copy every correctly named fixture into place
    std::vector<std::string> ids;
    for (const auto& entry : fs::directory_iterator(data)) {
        if (entry.path().extension() != ".spool") continue;
        const std::string id = entry.path().stem().string();
        if (!is_file_id(id)) {
            std::fprintf(stderr, "FAIL: %s: fixture name must be a file ID 0-255, e.g. 1.spool\n",
                entry.path().filename().c_str());
            ++failures;
            continue;
        }
        fs::copy_file(entry.path(), spool / entry.path().filename());
        ids.push_back(id);
    }
    if (ids.empty()) { std::fprintf(stderr, "FAIL: no <id>.spool fixtures in %s\n", data.c_str()); return 1; }

    Reassembler r;
    r.dir = project.string();
    CHECK(recover_files(r));

    for (const std::string& id : ids) {
        int before = failures;
        const std::string pdf = read_file(project / (id + ".partial.pdf"));
        CHECK(pdf.compare(0, 5, "%PDF-") == 0);                                                // a PDF from the start...
        CHECK(pdf.find("%%EOF", pdf.size() < 32 ? 0 : pdf.size() - 32) != std::string::npos);  // ...to the end
        if (fs::exists(data / (id + ".pdf"))) CHECK(pdf == read_file(data / (id + ".pdf")));
        CHECK(!fs::exists(spool / (id + ".spool")));   // recovery used up the copy...
        CHECK(fs::exists(data / (id + ".spool")));     // ...and the fixture is kept for the next run

        std::fprintf(stderr, "[%s] %s.spool -> %s.partial.pdf%s\n", failures == before ? " OK " : "FAIL",
            id.c_str(), id.c_str(), fs::exists(data / (id + ".pdf")) ? " (compared with its .pdf)" : "");
    }

    std::fprintf(stderr, failures == 0 ? "\nAll tests passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
