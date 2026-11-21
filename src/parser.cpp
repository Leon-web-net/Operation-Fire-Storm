#include "parser.hpp"
#include "frame.hpp"

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
                const char* marker_it = std::search(data + pos, data + n, 
                                        MARKER, MARKER + MARKER_LEN);

                if(marker_it != data + n){
                    size_t marker_pos = static_cast<size_t>(marker_it - data);
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
                if(!header.padding_ok){
                    // Not a real packet, just "CTFS" in normal traffic: forward the 'C', search again from the next byte
                    if(!forward(msg, 1)){ ok = false; return pos; }
                    pos += 1;
                    p.state = State::Searching;
                    break;
                }
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

    // Stage 2: split the stream by length. Same contract as process().
    size_t process_frames(const char* data, size_t n, const FrameFn& on_frame, const PacketFn& on_packet, bool& ok){
        size_t pos = 0;
        while(pos < n){
            const char* unit = data + pos;
            const size_t left = n - pos;
            if(left < MARKER_LEN) return pos;

            if(std::memcmp(unit, MARKER, MARKER_LEN) == 0){
                if(left < HEADER_LEN) return pos;
                CtfsHeader header = parse_header(unit);
                if(header.padding_ok){
                    size_t total = HEADER_LEN + static_cast<size_t>(header.len);
                    if(left < total) return pos;
                    on_packet(header, unit + HEADER_LEN);
                    pos += total;
                    continue;
                }
                // non-zero padding: not a packet, so it must be a frame
            }

            size_t len = frame_len(unit, left);
            if(len == 0) return pos;
            if(len == FRAME_INVALID){
                std::fprintf(stderr, "error: input is not a CTFS packet or an Ethernet/IPv4 frame, stopping\n");
                ok = false;
                return pos;
            }
            if(left < len) return pos;
            if(!on_frame(unit, len)){ ok = false; return pos; }
            pos += len;
        }
        return pos;
    }

    // Shared by both modes: process from the caller's buffer when nothing is pending (no copy),
    // otherwise join the leftover bytes with the new data first. Keeps whatever wasn't consumed.
    template <typename Process>
    void run(Parser& p, const char* data, size_t n, Process process){
        if(p.pending.empty()){
            size_t used = process(data, n);
            p.pending.assign(data + used, data + n);
        } else {
            p.pending.reserve(p.pending.size() + n); // also silences a GCC 13 false-positive -Wstringop-overflow on insert
            p.pending.insert(p.pending.end(), data, data + n);
            size_t used = process(p.pending.data(), p.pending.size());
            p.pending.erase(p.pending.begin(), p.pending.begin() + static_cast<std::ptrdiff_t>(used));
        }
    }
}

bool feed(Parser& p, const char* data, size_t n, const ForwardFn& forward, const PacketFn& on_packet){
    bool ok = true;
    run(p, data, n, [&](const char* d, size_t len){ return process(p, d, len, forward, on_packet, ok); });
    return ok;
}

bool feed_frames(Parser& p, const char* data, size_t n, const FrameFn& on_frame, const PacketFn& on_packet){
    bool ok = true;
    run(p, data, n, [&](const char* d, size_t len){ return process_frames(d, len, on_frame, on_packet, ok); });
    return ok;
}

void finish_frames(Parser& p){
    if(!p.pending.empty()){
        std::fprintf(stderr, "warning: stream ended inside a packet or frame, dropping %zu bytes\n", p.pending.size());
    }
    p.pending.clear();
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
