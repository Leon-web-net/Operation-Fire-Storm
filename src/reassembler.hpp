#pragma once

#include "ctfs.hpp"
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

// Reassembly: stores CTFS payloads on disk, one spool file per file ID,
//  so files don't have to fit in RAM.

struct Slice {
    uint64_t offset;   // where the payload starts in the spool (just past its 4-byte header)
    uint16_t len;
};

struct SpoolFile {
    std::FILE* fp = nullptr;            // opened "a+b" (append + read); "rb" when recovered
    std::string path;                   // <dir>/spool/<id>.spool (or the file it was recovered from)
    bool       failed = false;          // a write failed: refuse further packets for this file
    uint64_t   size = 0;                // bytes written so far = where the next record goes
    std::map<uint16_t, Slice> index;    // seq -> where its payload is
};

struct Reassembler {
    std::string dir = ".";              // output folder for PDFs; spools go in <dir>/spool/
    std::map<uint8_t, SpoolFile> files; // file_id -> its spool

    Reassembler() = default;
    ~Reassembler();                     // closes spools but never deletes them (see write_files)
    Reassembler(const Reassembler&) = delete;            // owns open files: no copies
    Reassembler& operator=(const Reassembler&) = delete;
};

enum class AddResult { Stored, Duplicate, WriteError };

// Append one packet to its file's spool. A repeated seq is rejected (the first copy is kept).
AddResult add_packet(Reassembler& r, const CtfsHeader& header, const char* payload);

// Read the payload for one index entry back from the spool into out. Returns false on error.
bool read_payload(SpoolFile& f, const Slice& s, std::vector<char>& out);

// Copy every spool into <dir>/<id>.pdf in seq order, one packet at a time, then delete the spool.
// The name is <id>.partial.pdf instead if seqs are missing or the data was recovered.
// Writes via a .tmp + rename, so a failure never touches an existing PDF. A spool is only
bool write_files(Reassembler& r, bool recovered = false);

// After a crash or failed run: rebuild every <id>.spool (or <id>.spool.old) in <dir>/spool/ from its
bool recover_files(Reassembler& r);