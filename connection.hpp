#pragma once
#include <chrono>
#include <unordered_map>

struct ConnectionState {
    uint32_t client_seq;
    uint32_t server_seq;
    uint16_t window_size;
    bool handshake_done;
    std::chrono::steady_clock::time_point last_active;
    struct CongestionState {
        size_t cwnd = 1;
        size_t ssthresh = 512;
        size_t duplicateACKs = 0;
        bool inRecovery = false;
    };
};
