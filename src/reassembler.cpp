#include "reassembler.hpp"

// e.g. path_for(r, 1, ".spool") -> "<dir>/1.spool"
static std::string path_for(const Reassembler& r, uint8_t file_id, const char* ext){
    return r.dir + "/" + std::to_string(file_id) + ext;
}

Reassembler::~Reassembler(){
    for(auto& [file_id, f] : files){
        if(!f.fp) continue;
        std::fclose(f.fp);
        std::remove(path_for(*this, file_id, ".spool").c_str());
    }
}

AddResult add_packet(Reassembler& r, const CtfsHeader& header, const char* payload){
    SpoolFile& f = r.files[header.file_id];   // creates the entry on the file's first packet
    if(!f.fp){
        f.fp = std::fopen(path_for(r, header.file_id, ".spool").c_str(), "w+b");
        if(!f.fp) return AddResult::WriteError;
    }
    if(f.index.count(header.seq) != 0) return AddResult::Duplicate; // keep the first copy

    const unsigned char rec[4] = {
        static_cast<unsigned char>(header.seq >> 8), static_cast<unsigned char>(header.seq),
        static_cast<unsigned char>(header.len >> 8), static_cast<unsigned char>(header.len),
    };
    if(std::fwrite(rec, 1, 4, f.fp) != 4) return AddResult::WriteError;
    if(std::fwrite(payload, 1, header.len, f.fp) != header.len) return AddResult::WriteError;

    f.index[header.seq] = {f.size + 4, header.len};
    f.size += 4 + header.len;
    return AddResult::Stored;
}

bool read_payload(SpoolFile& f, const Slice& s, std::vector<char>& out){
    out.resize(s.len);
    // fflush is required when switching from writing to reading on the same FILE*
    if(std::fflush(f.fp) != 0) return false;
    if(std::fseek(f.fp, static_cast<long>(s.offset), SEEK_SET) != 0) return false;
    bool ok = std::fread(out.data(), 1, s.len, f.fp) == s.len;
    std::fseek(f.fp, 0, SEEK_END);   // put the position back at the end for the next append
    return ok;
}

// Copy the spool's payloads, in seq order, into a new file at path. Only one payload is in memory at a time.
static bool copy_spool(SpoolFile& f, const std::string& path){
    std::FILE* out = std::fopen(path.c_str(), "wb");
    if(!out){ std::perror(path.c_str()); return false; }

    std::vector<char> buf;   // reused for every packet, never bigger than 64 KB
    bool ok = true;
    for(const auto& [seq, slice] : f.index){
        (void)seq;
        if(!read_payload(f, slice, buf) || std::fwrite(buf.data(), 1, slice.len, out) != slice.len){
            ok = false;
            break;
        }
    }
    if(std::fclose(out) != 0) ok = false;
    return ok;
}

bool write_files(Reassembler& r){
    bool all_ok = true;
    for(auto& [file_id, f] : r.files){
        if(!f.fp || f.index.empty()) continue;   // nothing stored (that run already failed)

        // Gap check: seqs should be 0..last with none missing. Warn only for now.
        uint16_t last = f.index.rbegin()->first;
        if(f.index.size() != static_cast<size_t>(last) + 1){
            std::fprintf(stderr, "warning: file %u is missing %zu packet(s)\n",
                static_cast<unsigned>(file_id), static_cast<size_t>(last) + 1 - f.index.size());
        }

        const std::string pdf = path_for(r, file_id, ".pdf");
        const std::string tmp = pdf + ".tmp";
        if(!copy_spool(f, tmp) || std::rename(tmp.c_str(), pdf.c_str()) != 0){
            std::fprintf(stderr, "Failed writing %s\n", pdf.c_str());
            std::remove(tmp.c_str());   // any old <id>.pdf is left alone
            all_ok = false;
            continue;                   // the destructor deletes this spool
        }
        std::printf("Wrote %zu CTFS chunks to %s\n", f.index.size(), pdf.c_str());

        std::fclose(f.fp);
        f.fp = nullptr;
        std::remove(path_for(r, file_id, ".spool").c_str());
    }
    return all_ok;
}
