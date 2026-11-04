#include "parser.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring> 

namespace{
    constexpr char MARKER[] = {'C','T','F','S'};
    constexpr size_t MARKER_LEN = sizeof(MARKER);
}

bool feed(Parser& p, const char* data, size_t n, const ForwardFn& forward, const PacketFn& on_packet){
    p.pending.insert(p.pending.end(), data, data +n);

    size_t pos = 0;
    bool waiting = false; // true when we need more data before we can go further

    while(!waiting && pos < p.pending.size()){
        switch(p.state){

        case State::Searching: {
            // Look for the next "CTFS" marker
            auto it = std::search(p.pending.begin()+ static_cast<std::ptrdiff_t>(pos),
             p.pending.end(), MARKER, MARKER + MARKER_LEN);

            if(it != p.pending.end()){
                size_t marker_pos = static_cast<size_t>(it - p.pending.begin());
                if(!forward(p.pending.data() + pos, marker_pos - pos)) return false;

                pos = marker_pos;
                p.state = State::InMessage;
                break;
            }

            // No marker: hold back up to 3 trailing bytes in case they start a marker split across reads
            size_t remaining = p.pending.size() - pos;
            size_t keep = 0;
            size_t max_keep = std::min<size_t>(MARKER_LEN -1, remaining);

            for(size_t k = max_keep; k>0; --k){
                if(std::memcmp(p.pending.data() +p.pending.size()-k, MARKER,k ) == 0){
                    keep = k;
                    break;
                }
            }

            size_t safe_len = remaining - keep;
            if(!forward(p.pending.data() +pos, safe_len)) return false;
            pos += safe_len;
            waiting = true; // need more data to find a marker
            break;
        }

        case State::InMessage: {
            // pos points at the 'C' of "CTFS". Wait for the full header, then the full payload.
            size_t left = p.pending.size() - pos;
            if(left < HEADER_LEN){ waiting = true; break; }

            const char* msg = p.pending.data() +pos;
            CtfsHeader header = parse_header(msg);
            size_t total = HEADER_LEN + static_cast<size_t>(header.len);

            if(left < total){ waiting = true; break; }

            on_packet(header, msg + HEADER_LEN); 

            pos += total;
            p.state = State::Searching;
            break;
        }

        }
    }

    p.pending.erase(p.pending.begin(), 
        p.pending.begin() + static_cast<std::ptrdiff_t>(pos));
    
    return true;
}


bool finish(Parser& p, const ForwardFn& forward){
    // Stream ended mid packet, forward the partial packet as normal data
    if(p.state == State::InMessage && !p.pending.empty()){
          std::fprintf(stderr, "warning: stream ended inside a CTFS packet, forwarding %zu bytes as normal traffic\n",
            p.pending.size());
    }

    // Forward any remaining bytes
    bool ok = p.pending.empty() || forward(p.pending.data(), p.pending.size());
    
    p.pending.clear();
    p.state = State::Searching;
    return ok;
}
