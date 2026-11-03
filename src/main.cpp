#include "config.hpp"
#include "net.hpp"
#include "parser.hpp"
#include "pdf.hpp"

#include <sys/socket.h> //accept, recv, send, socket
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

    //  FORWARDING FUNCTION
    auto forward = [&out_fd](const char* data, size_t len){
        if(len == 0) return true;
        if(out_fd == -1){
            out_fd = connect_to(FORWARD_PORT);
            if(out_fd < 0) return false;
        }
        return send_all(out_fd, data, len);
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
        if(!feed(parser,buf,static_cast<size_t>(n),forward)){
            ok = false;
            break;
        }
    }

    if(ok && !finish(parser,forward)) ok = false;
    // Only write after a clean run: a failed run must not replace a good output.pdf with a partial one.
    if(ok){
        if(!write_pdf(parser.chunks, PDF_PATH)) ok = false;
    } else {
        std::fprintf(stderr, "Run failed: %s not written\n", PDF_PATH);
    }

    close(in_fd);
    if(out_fd != - 1) close(out_fd);
    close(listen_fd);

    return ok ? 0 :1;
}