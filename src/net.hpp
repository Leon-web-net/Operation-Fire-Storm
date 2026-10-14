#pragma once
#include <cstddef>

// Returns listening fd or -1 on error
int make_listener(int port); 

// Returns the connected fd or -1 on error
int connect_to(int port);

bool send_all(int fd, const char* data, size_t len);