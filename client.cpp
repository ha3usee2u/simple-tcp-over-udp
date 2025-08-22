#include "packet.hpp"
#include <iostream>
#include <string>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <vector>
#include <chrono>
#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;

void sendPacket(int sock, sockaddr_in& server_addr, const Packet& pkt) {
    std::string raw = pkt.serialize();
    sendto(sock, raw.c_str(), raw.size(), 0, (sockaddr*)&server_addr, sizeof(server_addr));
}

Packet receivePacket(int sock) {
    char buffer[4096];
    sockaddr_in from_addr;
    socklen_t len = sizeof(from_addr);
    ssize_t n = recvfrom(sock, buffer, sizeof(buffer), 0, (sockaddr*)&from_addr, &len);
    std::string raw(buffer, n);
    return Packet::deserialize(raw);
}

std::string performHandshake(int sock, sockaddr_in& server_addr) {
    // 傳送握手封包
    Packet syn = {100, 0, 1024, PacketType::SYN, "client"};
    sendPacket(sock, server_addr, syn);

    // 接收 server 回應
    Packet response = receivePacket(sock);
    if (response.type == PacketType::SYN_ACK) {
        std::cout << "🤝 完成握手：" << response.payload << "\n";
        return response.payload;
    }

    std::cerr << "❌ 握手失敗。\n";
    return "";
}

void handleExpression(int sock, sockaddr_in& server_addr) {
    std::string expr;
    std::cout << "請輸入運算式（例如 3+5*2）：";
    std::getline(std::cin, expr);

    Packet pkt = {101, 0, 1024, PacketType::EXPR_REQ, expr};
    sendPacket(sock, server_addr, pkt);

    Packet response = receivePacket(sock);
    if (response.type == PacketType::EXPR_RES) {
        std::cout << "📥 運算結果：" << response.payload << "\n";
    } else {
        std::cout << "❌ 錯誤：未收到運算結果。\n";
    }
}

void handleFileRequest(int sock, sockaddr_in& server_addr, const std::string& client_id) {
    std::string filename;
    std::cout << "請輸入檔案名稱（例如 example.txt）：";
    std::getline(std::cin, filename);
    
    Packet pkt = {102, 0, 1024, PacketType::FILE_REQ, filename};
    sendPacket(sock, server_addr, pkt);

    std::vector<std::string> file_chunks;
    auto start = std::chrono::steady_clock::now();

    while (true) {
        Packet p = receivePacket(sock);

        if (p.type == PacketType::FILE_ERR) {
            std::cerr << "❌ 錯誤：Server 找不到檔案：" << filename << "\n";
            return;
        }

        if (p.type == PacketType::FILE_END) {
            break;
        }

        if (p.type == PacketType::FILE_DATA) {
            file_chunks.push_back(p.payload);

            Packet ack = {
                static_cast<uint32_t>(p.seq + static_cast<uint32_t>(p.payload.size())),
                p.seq,
                1024,
                PacketType::DATA_ACK,
                ""
            };
            sendPacket(sock, server_addr, ack);
        }

        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
            std::cerr << "⚠️ 接收逾時，中斷傳輸。\n";
            break;
        }
    }

    // 儲存檔案
    std::filesystem::path download_dir = "./downloads/" + client_id;
    std::filesystem::create_directories(download_dir);

    std::filesystem::path output_file = download_dir / filename;
    std::ofstream outfile(output_file);
    for (const auto& chunk : file_chunks) {
        outfile << chunk << "\n";
    }
    outfile.close();

    std::cout << "✅ 檔案已儲存至：" << output_file << "\n";
}

int main() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in server_addr = {AF_INET, htons(9000)};
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    std::string client_key = performHandshake(sock, server_addr);
    if (client_key.empty()) {
        close(sock);
        return 1;
    }

    // 🔍 擷取冒號後的 client_id
    std::string client_id = "unknown";
    size_t pos = client_key.find(':');
    if (pos != std::string::npos && pos + 1 < client_key.size()) {
        client_id = client_key.substr(pos + 1);
    }

    while (true) {
        std::cout << "\n請選擇功能：\n";
        std::cout << "1. 傳送四則運算式\n";
        std::cout << "2. 請求檔案\n";
        std::cout << "0. 離開\n";
        std::cout << "輸入選項：";

        int choice;
        std::cin >> choice;
        std::cin.ignore();

        if (choice == 0) break;
        else if (choice == 1) handleExpression(sock, server_addr);
        else if (choice == 2) handleFileRequest(sock, server_addr, client_id);
        else std::cout << "❌ 無效選項，請重新輸入。\n";
    }

    close(sock);
    std::cout << "👋 已離開 client。\n";
    return 0;
}

