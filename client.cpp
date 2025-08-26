#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_set>

#include "packet.hpp"
namespace fs = std::filesystem;

void sendPacket(int sock, sockaddr_in &server_addr, const Packet &pkt)
{
    std::string raw = pkt.serialize();
    sendto(sock, raw.c_str(), raw.size(), 0, (sockaddr *) &server_addr,
           sizeof(server_addr));
}

bool receivePacket(int sockfd, Packet &pkt) {
    char buffer[4096];
    sockaddr_in from_addr;
    socklen_t len = sizeof(from_addr);
    ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr *) &from_addr, &len);
    if (n <= 0) return false;
    pkt = Packet::deserialize(std::string(buffer, n));
    return true;
}

std::string performHandshake(int sock, sockaddr_in &server_addr)
{
    Packet syn = {100, 0, 1024, PacketType::SYN, "client"};
    sendPacket(sock, server_addr, syn);

    Packet response;
    if (!receivePacket(sock, response)) {
        std::cerr << "❌ 握手失敗（未收到 SYN_ACK）。\n";
        return "";
    }

    if (response.type == PacketType::SYN_ACK) {
        std::cout << "🤝 完成握手：" << response.payload << "\n";
        return response.payload;
    }

    std::cerr << "❌ 握手失敗（收到非 SYN_ACK 封包）。\n";
    return "";
}

void handleExpression(int sock, sockaddr_in &server_addr)
{
    std::string expr;
    std::cout << "請輸入運算式（例如 3+5*2）：";
    std::getline(std::cin, expr);

    Packet pkt = {101, 0, 1024, PacketType::EXPR_REQ, expr};
    sendPacket(sock, server_addr, pkt);

    Packet response;
    if (!receivePacket(sock, response)) {
        std::cout << "❌ 錯誤：未收到運算結果（timeout 或接收失敗）。\n";
        return;
    }

    if (response.type == PacketType::EXPR_RES) {
        std::cout << "📥 運算結果：" << response.payload << "\n";
    } else {
        std::cout << "❌ 錯誤：收到非 EXPR_RES 封包。\n";
    }
}

void handleFileRequest(int sock, sockaddr_in &server_addr, const std::string &client_id) {
    // 🔰 使用者輸入檔案名稱
    std::string filename;
    std::cout << "請輸入檔案名稱（例如 example.txt）：";
    std::getline(std::cin, filename);

    // 📤 發送 FILE_REQ 封包
    Packet req = {102, 0, 1024, PacketType::FILE_REQ, filename};
    sendPacket(sock, server_addr, req);
    std::cout << "📤 發送 FILE_REQ：" << filename << "\n";

    std::vector<std::string> file_chunks;
    std::unordered_set<uint32_t> received_seqs;
    int retries = 0;
    const int max_retries = 5;

    // ⏱️ 設定 socket timeout（5 秒）
    struct timeval tv = {5, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (retries < max_retries) {
        Packet p;

        if (!receivePacket(sock, p)) {
            std::cerr << "⚠️ timeout 或接收失敗，重試中 (" << retries + 1 << "/" << max_retries << ")\n";
            retries++;
            continue;
        }

        // ❌ 錯誤回應處理
        if (p.type == PacketType::FILE_ERR) {
            std::cerr << "❌ Server 找不到檔案：" << filename << "\n";
            return;
        }

        // 📦 結束封包處理
        if (p.type == PacketType::FILE_END) {
            std::cout << "📦 收到 FILE_END：seq=" << p.seq << "\n";

            Packet ack = {
                p.seq + 1,
                p.seq,
                1024,
                PacketType::DATA_ACK,
                ""
            };
            for (int i = 0; i < 3; ++i) {
                sendPacket(sock, server_addr, ack);
                std::cout << "📤 傳送 FILE_END ACK（第 " << i + 1 << " 次）：seq=" << ack.seq << " ack=" << ack.ack << "\n";
            }
            break;
        }

        // 📥 資料封包處理
        if (p.type == PacketType::FILE_DATA) {
            std::cout << "📥 收到 FILE_DATA：seq=" << p.seq << "\n";

            if (received_seqs.count(p.seq) == 0) {
                file_chunks.push_back(p.payload);
                received_seqs.insert(p.seq);
                std::cout << "✅ 新資料已加入：seq=" << p.seq << "\n";
            } else {
                std::cout << "🔁 重複資料，已忽略：seq=" << p.seq << "\n";
            }

            Packet ack = {
                p.seq + 1,
                p.seq,
                1024,
                PacketType::DATA_ACK,
                std::string(16, 'A') // 加入 padding，避免 ACK 被丟棄
            };
            sendPacket(sock, server_addr, ack);
            std::cout << "📤 傳送 ACK：seq=" << ack.seq << " ack=" << ack.ack << "\n";

            retries = 0;
        }
    }

    // ❌ 超過重試次數仍未收到 FILE_END
    if (retries >= max_retries) {
        std::cerr << "❌ 多次 timeout，未收到 FILE_END，中斷傳輸。\n";
        return;
    }

    // 💾 儲存檔案至 ./downloads/{client_id}/{filename}
    std::filesystem::path download_dir = "./downloads/" + client_id;
    std::filesystem::create_directories(download_dir);

    std::filesystem::path output_file = download_dir / filename;
    std::ofstream outfile(output_file, std::ios::binary);
    for (const auto &chunk : file_chunks) {
        outfile << chunk << "\n";
    }
    outfile.close();

    std::cout << "✅ 檔案已儲存至：" << output_file << "\n";
}

int main()
{
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

        if (choice == 0)
            break;
        else if (choice == 1)
            handleExpression(sock, server_addr);
        else if (choice == 2)
            handleFileRequest(sock, server_addr, client_id);
        else
            std::cout << "❌ 無效選項，請重新輸入。\n";
    }

    close(sock);
    std::cout << "👋 已離開 client。\n";
    return 0;
}
