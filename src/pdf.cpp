#include "pdf.hpp"

#include <cstdio>
#include <fstream>

bool write_pdf(const std::map<uint16_t, std::vector<char>>& chunks, const char* path){
    std::ofstream out(path, std::ios::binary);
    if(!out){
        std::perror(path);
        return false;
    }

    // iterate map in key order (sequence number order)
    for(const auto& [seq, payload]: chunks){
        (void)seq; // unused var warning
        out.write(payload.data(), static_cast<std::streamsize>(payload.size()));

        if(!out){
            std::fprintf(stderr, "Failed writing PDF data\n");
            return false;
        }
    }

    out.close();
    if(!out){
        std::fprintf(stderr, "Failed closing PDF file\n");
        return false;
    }

    std::printf("Wrote %zu CTFS chunks to %s\n", chunks.size(),path);
    return true;
}