#include "net.hpp"

#include <arpa/inet.h>  // htons, inet_pton
#include <netinet/in.h> // sockaddr_in
#include <sys/socket.h> // socket, bind, listen, connect, send
#include <unistd.h>     // close

#include <cerrno>
#include <cstdint>
#include <cstdio>

// 127.0.0.1:port, in the form the socket calls want
static sockaddr_in loopback(int port){
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port)); // kernel requires network order
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    return addr;
}

int make_listener(int port){
    int fd = socket(AF_INET, SOCK_STREAM,0);
    if(fd<0){ perror("socket"); return -1;}

    int yes = 1; // reuse address if server restarts
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr = loopback(port);
    if(bind(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))<0){
        perror("bind"); close(fd); return -1;
    }
    if(listen(fd,1)<0){
        perror("listen"); close(fd); return -1;
    }
    return fd;
}

int connect_to(int port){
    int fd = socket(AF_INET, SOCK_STREAM,0);
    if(fd<0){ perror("socket"); return -1;}

    sockaddr_in addr = loopback(port);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("connect"); close(fd); return -1;
    }
    return fd;
}

bool send_all(int fd, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("send");
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}