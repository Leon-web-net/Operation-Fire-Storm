#include "parser.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring> 

namespace{
    constexpr char MARKER[] = {'C','T','F','S'};
    constexpr size_t MARKER_LEN = sizeof(MARKER);

    // Runs the state machine over data[0..n). Returns how many bytes were consumed;
    // the rest is an incomplete marker/header/packet that needs more data.
    // Sets ok = false (and stops) if forward fails.
    size_t process(Parser& p, const char* data, size_t n,
                   const ForwardFn& forward, const PacketFn& on_packet, bool& ok){
        size_t pos = 0;

        while(pos < n){
            switch(p.state){

            case State::Searching: {
                // Look for the next "CTFS" marker
                const char* it = std::search(data + pos, data + n, MARKER, MARKER + MARKER_LEN);

                if(it != data + n){
                    size_t marker_pos = static_cast<size_t>(it - data);
                    if(!forward(data + pos, marker_pos - pos)){ ok = false; return pos; }

                    pos = marker_pos;
                    p.state = State::InMessage;
                    break;
                }

                // No marker: hold back up to 3 trailing bytes in case they start a marker split across reads
                size_t remaining = n - pos;
                size_t keep = 0;
                size_t max_keep = std::min<size_t>(MARKER_LEN -1, remaining);

                for(size_t k = max_keep; k>0; --k){
                    if(std::memcmp(data + n - k, MARKER, k) == 0){
                        keep = k;
                        break;
                    }
                }

                size_t safe_len = remaining - keep;
                if(!forward(data + pos, safe_len)){ ok = false; return pos; }
                return pos + safe_len; // need more data to find a marker
            }

            case State::InMessage: {
                // pos points at the 'C' of "CTFS". Wait for the full header, then the full payload.
                size_t left = n - pos;
                if(left < HEADER_LEN) return pos;

                const char* msg = data + pos;
                CtfsHeader header = parse_header(msg);
                size_t total = HEADER_LEN + static_cast<size_t>(header.len);

                if(left < total) return pos;

                on_packet(header, msg + HEADER_LEN);

                pos += total;
                p.state = State::Searching;
                break;
            }

            }
        }
        return pos;
    }
}

bool feed(Parser& p, const char* data, size_t n, const ForwardFn& forward, const PacketFn& on_packet){
    bool ok = true;

    if(p.pending.empty()){
        // Fast path (the usual case): work straight from the caller's buffer, no copy.
        // Only the unfinished tail (0-3 bytes, or a packet split across reads) is kept.
        size_t used = process(p, data, n, forward, on_packet, ok);
        p.pending.assign(data + used, data + n);
    } else {
        // Leftover bytes from the last read: join them with the new data, then process.
        p.pending.reserve(p.pending.size() + n); // also silences a GCC 13 false-positive -Wstringop-overflow on insert
        p.pending.insert(p.pending.end(), data, data + n);
        size_t used = process(p, p.pending.data(), p.pending.size(), forward, on_packet, ok);
        p.pending.erase(p.pending.begin(), p.pending.begin() + static_cast<std::ptrdiff_t>(used));
    }

    return ok;
}


bool finish(Parser& p, const ForwardFn& forward){
    // Stream ended mid packet, forward the partial packet as normal data
    if(p.state == State::InMessage){
          std::fprintf(stderr, "warning: stream ended inside a CTFS packet, forwarding %zu bytes as normal traffic\n",
            p.pending.size());
    }

    // Forward any remaining bytes
    bool ok = p.pending.empty() || forward(p.pending.data(), p.pending.size());
    
    p.pending.clear();
    p.state = State::Searching;
    return ok;
}
