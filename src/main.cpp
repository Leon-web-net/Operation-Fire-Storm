#include "config.hpp"
#include "net.hpp"
#include "parser.hpp"
#include "reassembler.hpp"

#include <sys/socket.h> //accept, recv
#include <unistd.h> //close

#include <cerrno>
#include <cstdio>

int main(){
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
        if(res == AddResult::WriteError){
            std::fprintf(stderr, "error: could not write spool for file %d\n", header.file_id);
            spool_ok = false;
            return;
        }
        std::printf("CTFS file=%d seq=%d len=%d\n", header.file_id, header.seq, header.len);
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

    if(ok) ok = finish(parser, forward) && spool_ok;
    // Only write after a clean run: a failed run must not replace a good PDF with a partial one.
    // One output file per file ID: 1.pdf, 2.pdf, ... (on failure the destructor deletes the spools)
    if(ok) ok = write_files(reassembler);
    else   std::fprintf(stderr, "Run failed: no PDF written\n");

    close(in_fd);
    if(out_fd != -1) close(out_fd);
    close(listen_fd);

    return ok ? 0 :1;
}