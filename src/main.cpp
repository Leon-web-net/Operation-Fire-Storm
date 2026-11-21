#include "config.hpp"
#include "frame.hpp"
#include "net.hpp"
#include "parser.hpp"
#include "reassembler.hpp"

#include <sys/socket.h> //accept, recv
#include <unistd.h> //close
#include <cerrno>
#include <cstdio>
#include <string>

// One downstream port, connected lazily on first use.
struct Out { int port; int fd = -1; };

static bool send_to(Out& o, const char* data, size_t len){
    if(len == 0) return true;
    if(o.fd == -1){
        o.fd = connect_to(o.port);
        if(o.fd < 0) return false;
    }
    return send_all(o.fd, data, len);
}

int main(int argc, char** argv){
    // ./firestorm --recover: rebuild PDFs from the spools a crashed or failed run left behind
    if(argc == 2 && std::string(argv[1]) == "--recover"){
        Reassembler r;
        return recover_files(r) ? 0 : 1;
    }
    bool stage2 = (argc == 2 && std::string(argv[1]) == "--stage2");
    if(argc != 1 && !stage2){ std::fprintf(stderr, "usage: %s [--stage2 | --recover]\n", argv[0]); return 1; }

    int listen_fd = make_listener(LISTEN_PORT);
    if (listen_fd < 0) return 1;
    std::printf("Listening on port %d\n", LISTEN_PORT);

    int in_fd = accept(listen_fd, nullptr, nullptr);
    if(in_fd < 0) { perror("accept"); close(listen_fd); return 1; }
    Out outs[] = {{TCP_PORT}, {MONITOR_PORT}, {UDP_PORT}};

    // Stage 1: forward normal traffic to the downstream port (TCP_PORT == FORWARD_PORT).
    auto forward = [&outs](const char* data, size_t len){
        return send_to(outs[0], data, len);
    };

    // Stage 2: send each frame to every output its route bit selects; no bits means drop.
    auto on_frame = [&outs](const char* frame, size_t len){
        unsigned r = route(parse_frame(frame));
        const unsigned bits[] = {ROUTE_TCP, ROUTE_MONITOR, ROUTE_UDP};
        for(int i = 0; i < 3; i++){
            if((r & bits[i]) && !send_to(outs[i], frame, len)) return false;
        }
        return true;
    };

    // Each complete CTFS packet: log it, then hand it to the reassembler
    Reassembler reassembler;
    bool spool_ok = true;
    auto on_packet = [&reassembler, &spool_ok](const CtfsHeader& header, const char* payload){
        AddResult res = add_packet(reassembler, header, payload);
        if(res == AddResult::Duplicate){
            std::fprintf(stderr, "warning: duplicate seq %d for file %d ignored\n", header.seq, header.file_id);
            return;
        }
        if(res == AddResult::WriteError){   // add_packet already printed the reason, once
            spool_ok = false;
            return;
        }
        std::printf("CTFS file=%d seq=%d len=%d\n", header.file_id, header.seq, header.len);
        // Kept even on a mismatch: in the Stage 2 data the last packet of 4 of the 5 files fails
        // (only those whose length isn't a multiple of 4), and dropping it would corrupt the PDF.
        if(ctfs_checksum(header, payload) != header.checksum){
            std::fprintf(stderr, "warning: checksum mismatch, file %d seq %d (kept)\n", header.file_id, header.seq);
        }
    };

    char buf[65536];
    Parser parser;
    bool ok = true;

    while(true){
        ssize_t n = recv(in_fd, buf, sizeof(buf),0);
        if (n == 0) break;
        if(n<0){
            if(errno == EINTR) continue;
            perror("recv");
            ok = false;
            break;
        }
        bool fed = stage2 ? feed_frames(parser, buf, static_cast<size_t>(n), on_frame, on_packet)
                          : feed(parser, buf, static_cast<size_t>(n), forward, on_packet);
        if(!fed){
            ok = false;
            break;
        }
    }
    
    // Only write after a clean run: a failed run must not replace a good PDF with a partial one.
    if(ok){
        if(stage2) finish_frames(parser);
        else       ok = finish(parser, forward);
        ok = ok && spool_ok;
    }
    if(ok) ok = write_files(reassembler);
    else   std::fprintf(stderr, "Run failed: no PDF written. Spools kept: run with --recover to rebuild them.\n");

    close(in_fd);
    for(Out& o : outs) if(o.fd != -1) close(o.fd);
    close(listen_fd);

    return ok ? 0 :1;
}