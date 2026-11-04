#include "config.hpp"
#include "net.hpp"
#include "parser.hpp"
#include "pdf.hpp"
#include "reassembler.hpp"

#include <sys/socket.h> //accept, recv, send, socket
#include <unistd.h> //close

#include <cerrno>
#include <cstdio>
#include <string>

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
    auto on_packet = [&reassembler](const CtfsHeader& header, const char* payload){
        if(!add_packet(reassembler, header, payload)){
             std::fprintf(stderr, "warning: duplicate seq %u for file %u ignored\n",
                 static_cast<unsigned>(header.seq), static_cast<unsigned>(header.file_id));
            return;
        }
        std::printf("CTFS file=%u seq=%u len=%u\n", static_cast<unsigned>(header.file_id),
            static_cast<unsigned>(header.seq), static_cast<unsigned>(header.len));
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

    if(ok && !finish(parser,forward)) ok = false;
    // Only write after a clean run: a failed run must not replace a good output.pdf with a partial one.
    if(ok){
        // One output file per file ID. With a single file (the normal case) it's just output.pdf.
        for(const auto& [file_id, chunks] : reassembler.files){
            std::string path = reassembler.files.size() == 1
                ? std::string(PDF_PATH)
                : "file_" + std::to_string(file_id) + "_" + PDF_PATH;
            if(!write_pdf(chunks, path.c_str())) ok = false;
        }
    } else {
        std::fprintf(stderr, "Run failed: %s not written\n", PDF_PATH);
    }

    close(in_fd);
    if(out_fd != - 1) close(out_fd);
    close(listen_fd);

    return ok ? 0 :1;
}