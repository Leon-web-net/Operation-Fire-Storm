#pragma once

#include "ctfs.hpp"
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

// Reassembly: stores CTFS payloads on disk, one spool file per file ID, so files don't have to fit in RAM.
// Knows nothing about the byte stream; it only sees complete packets.
//
// Spool format: [seq u16 BE][len u16 BE][payload] repeated, in arrival order.
// The in-memory index says where each seq's payload is, sorted by seq.

struct Slice {
    uint64_t offset;   // where the payload starts in the spool (just past its 4-byte record header)
    uint16_t len;
};

struct SpoolFile {
    std::FILE* fp = nullptr;            // <dir>/<id>.spool, opened "a+b" (append + read)
    bool       failed = false;          // a write failed: refuse further packets for this file
    uint64_t   size = 0;                // bytes written so far = where the next record goes
    std::map<uint16_t, Slice> index;    // seq -> where its payload is
};

struct Reassembler {
    std::string dir = ".";              // where spools and output files go
    std::map<uint8_t, SpoolFile> files; // file_id -> its spool

    Reassembler() = default;
    ~Reassembler();                     // closes and deletes any spools still open
    Reassembler(const Reassembler&) = delete;            // owns open files: no copies
    Reassembler& operator=(const Reassembler&) = delete;
};

enum class AddResult { Stored, Duplicate, WriteError };

// Append one packet to its file's spool. A repeated seq is rejected (the first copy is kept).
// After the first write error the spool is marked failed: later packets get WriteError straight away.
AddResult add_packet(Reassembler& r, const CtfsHeader& header, const char* payload);

// Read the payload for one index entry back from the spool into out. Returns false on error.
bool read_payload(SpoolFile& f, const Slice& s, std::vector<char>& out);

// Copy every spool into <dir>/<id>.pdf in seq order, one packet at a time, then delete the spool.
// Writes via <id>.pdf.tmp + rename, so a failure never touches an existing <id>.pdf.
// Returns true if every file was written.
bool write_files(Reassembler& r);
