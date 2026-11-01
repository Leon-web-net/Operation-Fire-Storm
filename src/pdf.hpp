#pragma once

#include <cstdint>
#include <map>
#include <vector>

// Write the payloads to path in sequence-number order, Returns false on error
bool write_pdf(const std::map<uint16_t,std::vector<char>>& chunks, 
    const char* path);