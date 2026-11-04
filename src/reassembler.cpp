#include "reassembler.hpp"

bool add_packet(Reassembler& r, const CtfsHeader& header, const char* payload){
    auto& chunks = r.files[header.file_id];   // creates the file's map on its first packet
    if(chunks.count(header.seq) != 0) return false; // duplicate: keep the first copy
    chunks[header.seq] = std::vector<char>(payload, payload + header.len);
    return true;
}
