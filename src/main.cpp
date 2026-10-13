#include <sys/socket.h> // socket, bind, listen, accept, recv
#include <netinet/in.h> // sockaddr_in
#include <arpa/inet.h> // htons, htonl
#include <unistd.h> // close

#include <cstdio>
#include <cerrno>

constexpr int LISTEN_PORT  = 5733;
constexpr int FORWARD_PORT = 5744;

int make_listener(int port){
    int fd = socket(AF_INET,SOCK_STREAM,0);
    if(fd<0) { perror("socket"); return -1;}

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr); // presentation to network (success = 1, Invalid IP = 0)

    if(bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))<0){
        perror("bind"); close(fd); return -1;
    }
    if(listen(fd,1)<0){
        perror("listen"); close(fd); return -1;
    }

    return fd;
}

int connect_to(int port){

    int sender = socket(AF_INET,SOCK_STREAM,0);
    if(sender<0){ perror("socket"); return -1; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port =  htons(port); // kernel requires that field in network order
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if(connect(sender, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))<0){
        perror("connect"); close(sender); return -1;
    }

    return sender;

}

bool send_all(int fd, const char* data, size_t len){

    size_t sent = 0;
    while(sent < len){
        ssize_t n = send(fd,data+sent, len-sent,MSG_NOSIGNAL);
        if(n<0){
            if(errno == EINTR) continue;
            perror("send");
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

int main() {

    int listen_fd = make_listener(LISTEN_PORT);
    if(listen_fd<0) return 1;
    std::printf("Listening on %d....\n",LISTEN_PORT);
    int in_fd = accept(listen_fd,nullptr,nullptr);
    if(in_fd<0){ perror("accept"); return 1;}

    int out_fd = -1;
    char buf[65536];
    while(true){
        ssize_t n = recv(in_fd, buf, sizeof(buf), 0);
        if(n==0)break;
        if(n<0){
            if(errno == EINTR) continue;
            perror("recv");
            break;
        }
        if(out_fd == -1){
            out_fd = connect_to(FORWARD_PORT);
            if(out_fd < 0) break;
        }
        
        if(!send_all(out_fd, buf, n)) break;
    }

    close(in_fd); 
    if(out_fd != -1) close(out_fd);
    close(listen_fd);

    return 0;
}
