#pragma once
#include <cstddef>

// Creates a TCP server socket that listens for incoming connections on 127.0.0.1 and the specified port
// Returns listening fd or -1 on error
int make_listener(int port); 

// Connects to a TCP server on 127.0.0.1 using the specified port
// Returns the connected fd or -1 on error
int connect_to(int port);


bool send_all(int fd, const char* data, size_t len);