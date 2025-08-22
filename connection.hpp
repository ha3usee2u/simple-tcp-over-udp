#pragma once
#include <chrono>
#include <unordered_map>

struct ConnectionState {
    uint32_t client_seq;
    uint32_t server_seq;
    uint16_t window_size;
    bool handshake_done;
    std::chrono::steady_clock::time_point last_active;
};
