#include "config.hpp"
#include "net.hpp"
#include "parser.hpp"
#include "reassembler.hpp"

#include <sys/socket.h> //accept, recv
#include <unistd.h> //close
#include <cerrno>
#include <cstdio>
#include <string>

int main(int argc, char** argv){
    // ./firestorm --recover: rebuild PDFs from the spools a crashed or failed run left behind
    if(argc == 2 && std::string(argv[1]) == "--recover"){
        Reassembler r;
        return recover_files(r) ? 0 : 1;
    }
    if(argc != 1){ std::fprintf(stderr, "usage: %s [--recover]\n", argv[0]); return 1; }

    int listen_fd = make_listener(LISTEN_PORT);
    if (listen_fd < 0) return 1;
    std::printf("Listening on port %d\n", LISTEN_PORT);

    int in_fd = accept(listen_fd, nullptr, nullptr);
    if(in_fd < 0) { perror("accept"); close(listen_fd); return 1; }
    int out_fd = -1; // connect lazily

    // Forward normal traffic to the downstream port.
    auto forward = [&out_fd](const char* data, size_t len){
        if(len == 0) return true;
        if(out_fd == -1){
            out_fd = connect_to(FORWARD_PORT);
            if(out_fd < 0) return false;
        }
        return send_all(out_fd, data, len);
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
        if(!feed(parser,buf,static_cast<size_t>(n),forward,on_packet)){
            ok = false;
            break;
        }
    }
    
    // Only write after a clean run: a failed run must not replace a good PDF with a partial one.
    if(ok) ok = finish(parser, forward) && spool_ok;
    if(ok) ok = write_files(reassembler);
    else   std::fprintf(stderr, "Run failed: no PDF written. Spools kept: run with --recover to rebuild them.\n");

    close(in_fd);
    if(out_fd != -1) close(out_fd);
    close(listen_fd);

    return ok ? 0 :1;
}