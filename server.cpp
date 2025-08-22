#include "packet.hpp"
#include "protocol.hpp"
#include <unordered_map>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <iostream>
#include <cstring>
#include <unistd.h>

std::string getClientKey(const sockaddr_in& addr) {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
}

int main() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "❌ 無法建立 socket\n";
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(9000);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "❌ bind 失敗\n";
        return 1;
    }

    std::unordered_map<std::string, ConnectionState> connections;
    Protocol protocol;

    std::cout << "✅ Server 已啟動，等待封包...\n";

    while (true) {
        char buffer[4096];
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);

        ssize_t n = recvfrom(sock, buffer, sizeof(buffer), 0, (sockaddr*)&client_addr, &len);
        if (n <= 0) continue;

        std::string raw(buffer, n);
        Packet pkt = Packet::deserialize(raw);
        std::string client_key = getClientKey(client_addr);

        // 🆕 Debug: 顯示收到封包類型與 client key
        std::cout << "📥 收到封包：" << to_string(pkt.type) << " from " << client_key << "\n";

        // 🧩 尚未建立連線
        if (!connections.contains(client_key)) {
            if (pkt.type == PacketType::SYN) {
                connections[client_key] = ConnectionState{pkt.seq, 1000, 1024, false};

                // 🆕 傳入 client_key 以設定 payload
                Packet syn_ack = protocol.handleHandshake(pkt, connections[client_key], client_key);
                std::string serialized = syn_ack.serialize();
                sendto(sock, serialized.c_str(), serialized.size(), 0, (sockaddr*)&client_addr, len);

                std::cout << "🚀 傳送 SYN-ACK 給 " << client_key << "\n";
            } else {
                std::cerr << "⚠️ 未握手的 client 嘗試傳送資料：" << client_key << "\n";
            }
            continue;
        }

        auto& state = connections[client_key];

        // 🤝 完成三次握手
        if (!state.handshake_done && pkt.type == PacketType::ACK) {
            state.handshake_done = true;
            std::cout << "🤝 完成握手：" << client_key << "\n";
            continue;
        }

        Packet response;

        switch (pkt.type) {
            case PacketType::EXPR_REQ:
                response = protocol.handleExpression(pkt.payload, state);
                break;

            case PacketType::FILE_REQ: {
                auto packets = protocol.handleFileRequest(pkt.payload, state);
                for (auto& p : packets) {
                    std::string serialized = p.serialize();
                    sendto(sock, serialized.c_str(), serialized.size(), 0, (sockaddr*)&client_addr, len);
                }
                continue;
            }

            case PacketType::DATA_ACK:
                std::cout << "📬 收到 client ACK：" << pkt.ack << "\n";
                continue;

            default:
                std::cerr << "⚠️ 未知封包類型：" << to_string(pkt.type) << "\n";
                continue;
        }

        // 📨 傳送回應（EXPR_REQ）
        std::string serialized = response.serialize();
        sendto(sock, serialized.c_str(), serialized.size(), 0, (sockaddr*)&client_addr, len);
    }


    close(sock);
    return 0;
}
