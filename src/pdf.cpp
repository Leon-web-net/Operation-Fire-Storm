#include "pdf.hpp"

#include <cstdio>
#include <fstream>
#include <string>

namespace {
    // Write every chunk, in sequence-number order (std::map is sorted by key).
    bool write_chunks_to(const std::map<uint16_t, std::vector<char>>& chunks, const std::string& path){
        std::ofstream out(path, std::ios::binary);
        if(!out){
            std::perror(path.c_str());
            return false;
        }

        for(const auto& [seq, payload]: chunks){
            (void)seq; // unused var warning
            out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        }

        out.close();
        if(!out){
            std::fprintf(stderr, "Failed writing %s\n", path.c_str());
            return false;
        }
        return true;
    }
}

bool write_pdf(const std::map<uint16_t, std::vector<char>>& chunks, const char* path){
    // Write to a temporary file first, then rename() it over the real one.
    // rename() is atomic, so path is always either the old file or the complete new one,
    // never a half-written mix, even if we crash part way through.
    const std::string tmp = std::string(path) + ".tmp";

    if(!write_chunks_to(chunks, tmp)){
        std::remove(tmp.c_str()); // don't leave a partial .tmp behind
        return false;
    }

    if(std::rename(tmp.c_str(), path) != 0){
        std::perror("rename");
        std::remove(tmp.c_str());
        return false;
    }

    std::printf("Wrote %zu CTFS chunks to %s\n", chunks.size(), path);
    return true;
}
