#include "parser.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring> 

namespace{
    constexpr char MARKER[] = {'C','T','F','S'};
    constexpr size_t MARKER_LEN = sizeof(MARKER);
}

uint16_t read_u16(const char* p){
    return static_cast<uint16_t>(
        (static_cast<uint8_t>(p[0])<<8) | static_cast<uint8_t>(p[1])
    );
}

bool feed(Parser& p, const char* data, size_t n, const ForwardFn& forward){
    p.pending.insert(p.pending.end(), data, data +n);

    size_t pos = 0;

    while(pos<p.pending.size()){
        if(p.state == State::Searching){
            auto it = std::search(p.pending.begin()+ static_cast<std::ptrdiff_t>(pos),
             p.pending.end(), MARKER, MARKER + MARKER_LEN);

            if(it != p.pending.end()){
                size_t marker_pos = static_cast<size_t>(it - p.pending.begin());
                if(!forward(p.pending.data() + pos, marker_pos - pos)) return false;

                pos = marker_pos;
                p.state = State::INMessage;
                continue; 
            }

            size_t remaining = p.pending.size() - pos;
            size_t keep = 0;
            size_t max_keep = std::min<size_t>(MARKER_LEN -1, remaining);

            for(size_t k = max_keep; k>0; --k){
                if(std::memcmp(p.pending.data() +p.pending.size()-k, MARKER,k ) == 0){
                    keep = k;
                    break; 
                }
            }
            
            // 
            size_t safe_len = remaining - keep;
            if(!forward(p.pending.data() +pos, safe_len)) return false;
            pos += safe_len;
            break; // need more data to find marker
        }

        // p.state == State::IMessage change to one function per state
        size_t left = p.pending.size() - pos;
        // fewer than 16: can't read seq/len yet, wait
        if(left<HEADER_LEN) break;

        // CTFS (4 bytes) + seq (2 bytes) + len (2 bytes) 
        const char* msg = p.pending.data() +pos;
        uint16_t seq = read_u16(msg +4);
        uint16_t len = read_u16(msg +6);
        size_t total = HEADER_LEN + static_cast<size_t>(len); // HEADER_LEN + payload length

        if(left < total) break; // wait for payload to arrive

        p.chunks[seq] = std::vector<char>(msg + HEADER_LEN, msg + total);
        std::printf("CTFS seq=%u len=%u\n", static_cast<unsigned>(seq),
         static_cast<unsigned>(len));
        
         pos += total;
         p.state = State::Searching;
    }

    p.pending.erase(p.pending.begin(), 
        p.pending.begin() + static_cast<std::ptrdiff_t>(pos));
    
    return true;
}

// forwards incomplete data, error if partial CTFS packet is left in the buffer
bool finish(Parser& p, const ForwardFn& forward){
    
    if(p.state == State::INMessage && !p.pending.empty()){
        std::fprintf(stderr, "warning: stream ended inside a CTFS packet, forwarding %zu bytes as normal traffic\n",
            p.pending.size());
    }

    if(!p.pending.empty()){
        if(!forward(p.pending.data(), p.pending.size())) return false;
    }
    p.pending.clear();
    p.state = State::Searching;
    return true;
}
