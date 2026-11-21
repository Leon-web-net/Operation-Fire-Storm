#include "reassembler.hpp"

#include <cerrno>
#include <charconv>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

// e.g. path_for(r, 1, ".pdf") -> "<dir>/1.pdf"
static std::string path_for(const Reassembler& r, uint8_t file_id, const char* ext){
    return r.dir + "/" + std::to_string(file_id) + ext;
}

// Spools live in their own folder, so the output folder only holds PDFs
static std::string spool_dir(const Reassembler& r){
    return r.dir + "/spool";
}

Reassembler::~Reassembler(){
    // Close only: a spool is deleted once its PDF is written, so after a failure it's kept for --recover
    for(auto& [file_id, f] : files){
        if(f.fp) std::fclose(f.fp);
    }
}

AddResult add_packet(Reassembler& r, const CtfsHeader& header, const char* payload){
    SpoolFile& f = r.files[header.file_id];  
    if(f.failed) return AddResult::WriteError;  
    if(f.index.count(header.seq) != 0) return AddResult::Duplicate; // keep the first copy

    if(!f.fp){
        std::error_code ec;
        fs::create_directory(spool_dir(r), ec);  // not create_directories: a missing r.dir stays an error
        f.path = spool_dir(r) + "/" + std::to_string(header.file_id) + ".spool";
        if(fs::exists(f.path)){                  // left by a crashed or failed run: keep it for --recover
            const std::string old_path = f.path + ".old";
            if(fs::exists(old_path)){            // never overwrite it: rename would replace it silently
                std::fprintf(stderr, "error: %s already exists; run --recover first, or move %s and %s\n",
                    old_path.c_str(), f.path.c_str(), old_path.c_str());
                f.failed = true;
                return AddResult::WriteError;
            }
            if(std::rename(f.path.c_str(), old_path.c_str()) != 0){
                std::fprintf(stderr, "error: cannot rename %s to %s: %s\n", f.path.c_str(), old_path.c_str(), std::strerror(errno));
                f.failed = true;
                return AddResult::WriteError;
            }
            std::fprintf(stderr, "warning: kept spool from an earlier run as %s.old\n", f.path.c_str());
        }
        f.fp = std::fopen(f.path.c_str(), "a+b");  // append mode: every write lands at the end
    }

    const unsigned char rec[4] = {
        static_cast<unsigned char>(header.seq >> 8), static_cast<unsigned char>(header.seq),
        static_cast<unsigned char>(header.len >> 8), static_cast<unsigned char>(header.len),
    };
    // fflush hands each record to the OS right away, so it survives the process being killed
    if(!f.fp || 
        std::fwrite(rec, 1, 4, f.fp) != 4 || std::fwrite(payload, 1, header.len, f.fp) != header.len     
        || std::fflush(f.fp) != 0){
        std::perror(f.path.c_str());
        f.failed = true;
        return AddResult::WriteError;
    }

    f.index[header.seq] = {f.size + 4, header.len};
    f.size += 4 + header.len;
    return AddResult::Stored;
}

bool read_payload(SpoolFile& f, const Slice& s, std::vector<char>& out){
    out.resize(s.len);
    // C stdio rule: switching between writing and reading on one FILE* needs an fseek (or fflush) in between.
    if(std::fseek(f.fp, static_cast<long>(s.offset), SEEK_SET) != 0) return false;  // also flushes pending writes
    bool ok = std::fread(out.data(), 1, s.len, f.fp) == s.len;
    std::fseek(f.fp, 0, SEEK_END);   // the switch back to writing (the position doesn't matter in "a+b")
    return ok;
}

// Copy the spool's payloads, in seq order, into a new file at path. Only one payload is in memory at a time.
static bool copy_spool(SpoolFile& f, const std::string& path){
    std::FILE* out = std::fopen(path.c_str(), "wb");
    if(!out){ std::perror(path.c_str()); return false; }

    std::vector<char> buf;   // reused for every packet, never bigger than 64 KB
    bool ok = true;
    for(const auto& [seq, slice] : f.index){
        if(!read_payload(f, slice, buf) || std::fwrite(buf.data(), 1, slice.len, out) != slice.len){
            ok = false;
            break;
        }
    }
    if(std::fclose(out) != 0) ok = false;
    return ok;
}

bool write_files(Reassembler& r, bool recovered){
    bool all_ok = true;
    for(auto& [file_id, f] : r.files){
        if(f.failed){ all_ok = false; continue; }   // a broken spool is never turned into a PDF
        if(f.index.empty()) continue;

        // Gap check in seq
        uint16_t last = f.index.rbegin()->first;
        size_t missing = static_cast<size_t>(last) + 1 - f.index.size();
        if(missing > 0){
            std::fprintf(stderr, "warning: file %d is missing %zu packet(s)\n", file_id, missing);
        }

        // Only a clean run with no gaps gets the real name
        const bool partial = recovered || missing > 0;
        const std::string pdf = path_for(r, file_id, partial ? ".partial.pdf" : ".pdf");
        const std::string tmp = pdf + ".tmp";
        if(!copy_spool(f, tmp) || std::rename(tmp.c_str(), pdf.c_str()) != 0){
            std::fprintf(stderr, "Failed writing %s\n", pdf.c_str());
            std::remove(tmp.c_str());   // any old PDF is left alone, and the spool is kept
            all_ok = false;
            continue;
        }
        std::printf("Wrote %zu CTFS chunks to %s\n", f.index.size(), pdf.c_str());

        std::fclose(f.fp);              // the PDF is safe: now the spool can go
        f.fp = nullptr;
        std::remove(f.path.c_str());
    }
    if(all_ok){
        std::error_code ec;
        fs::remove(spool_dir(r), ec);   // refuses a non-empty folder; any error is ignored
    }
    return all_ok;
}


// Rebuild one spool's index by reading its records from the start. A torn last record
// (the process died mid-write) is ignored. The first copy of a seq wins, as in add_packet
static bool recover_spool(SpoolFile& f, const std::string& path){
    std::error_code ec;
    const uint64_t file_size = fs::file_size(path, ec);
    f.fp = ec ? nullptr : std::fopen(path.c_str(), "rb");
    if(!f.fp){ std::fprintf(stderr, "cannot open %s\n", path.c_str()); return false; }
    f.path = path;

    unsigned char rec[4];
    while(std::fread(rec, 1, 4, f.fp) == 4){
        const uint16_t seq = static_cast<uint16_t>((rec[0] << 8) | rec[1]);
        const uint16_t len = static_cast<uint16_t>((rec[2] << 8) | rec[3]);
        if(f.size + 4 + len > file_size) break;            // torn record, its payload never fully arrived
        f.index.emplace(seq, Slice{f.size + 4, len});      // keep the first copy
        f.size += 4 + len;
        if(std::fseek(f.fp, len, SEEK_CUR) != 0) break;
    }
    if(f.size < file_size){
        std::fprintf(stderr, "warning: %s ends with a torn record (%llu bytes ignored)\n",
            path.c_str(), static_cast<unsigned long long>(file_size - f.size));
    }
    std::printf("Recovered %zu packets from %s\n", f.index.size(), path.c_str());
    return true;
}

bool recover_files(Reassembler& r){
    // file_id -> spool to recover. <id>.spool is the newest data, so it wins over <id>.spool.old.
    std::map<uint8_t, std::string> found;
    std::error_code ec;
    for(const auto& entry : fs::directory_iterator(spool_dir(r), ec)){
        fs::path name = entry.path().filename();        // "1.spool" or "1.spool.old"
        const bool old = name.extension() == ".old";
        if(old) name = name.stem();                     // "1.spool.old" -> "1.spool"
        if(name.extension() != ".spool") continue;      // not a spool

        const std::string id = name.stem().string();    // "1"
        uint8_t file_id;                                // from_chars rejects "", "x1", "-1", "256"
        auto [end, err] = std::from_chars(id.data(), id.data() + id.size(), file_id);
        if(err != std::errc() || end != id.data() + id.size()) continue;
        if(!old || found.count(file_id) == 0) found[file_id] = entry.path().string();  // .spool beats .spool.old
    }
    if(ec){ std::fprintf(stderr, "cannot read folder %s\n", spool_dir(r).c_str()); return false; }
    if(found.empty()){ std::fprintf(stderr, "no spools found in %s\n", spool_dir(r).c_str()); return false; }

    bool ok = true;
    for(const auto& [file_id, path] : found){
        if(!recover_spool(r.files[file_id], path)) ok = false;
    }
    return write_files(r, true) && ok;
}