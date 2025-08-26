#include "protocol.hpp"
#include "packet.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>


class ExpressionParser
{
public:
    ExpressionParser(const std::string &expr) : input(expr), pos(0) {}

    double parse() { return parseExpression(); }

private:
    std::string input;
    size_t pos;

    double parseExpression()
    {
        double value = parseTerm();
        while (match('+') || match('-')) {
            char op = input[pos - 1];
            double rhs = parseTerm();
            value = (op == '+') ? value + rhs : value - rhs;
        }
        return value;
    }

    double parseTerm()
    {
        double value = parseFactor();
        while (match('*') || match('/')) {
            char op = input[pos - 1];
            double rhs = parseFactor();
            value = (op == '*') ? value * rhs : value / rhs;
        }
        return value;
    }

    double parseFactor()
    {
        if (match('(')) {
            double value = parseExpression();
            match(')');
            return value;
        }
        return parseNumber();
    }

    double parseNumber()
    {
        size_t start = pos;
        while (pos < input.size() && (isdigit(input[pos]) || input[pos] == '.'))
            pos++;
        return std::stod(input.substr(start, pos - start));
    }

    bool match(char expected)
    {
        while (pos < input.size() && isspace(input[pos]))
            pos++;
        if (pos < input.size() && input[pos] == expected) {
            pos++;
            return true;
        }
        return false;
    }
};

Packet Protocol::handleHandshake(const Packet &pkt,
                                 ConnectionState &state,
                                 const std::string &client_key)
{
    Packet syn_ack;
    syn_ack.seq = 200;
    syn_ack.ack = pkt.seq;
    syn_ack.window = state.window_size;
    syn_ack.type = PacketType::SYN_ACK;

    // 使用 client_key 的 port 作為 payload
    std::string port = client_key.substr(client_key.find(':') + 1);
    syn_ack.payload = "client:" + port;

    return syn_ack;
}

Packet Protocol::handleExpression(const std::string &expr,
                                  ConnectionState &state)
{
    ExpressionParser parser(expr);
    double result = parser.parse();

    Packet response;
    response.seq = state.server_seq++;
    response.ack = state.client_seq;
    response.window = state.window_size;
    response.type = PacketType::EXPR_RES;
    response.payload = std::to_string(result);
    return response;
}


std::vector<Packet> Protocol::handleFileRequest(const std::string &filename,
                                                ConnectionState &state)
{
    std::vector<Packet> packets;
    std::string base_dir = "./files/";
    std::ifstream file(base_dir + filename);
    if (!file.is_open()) {
        Packet error;
        error.seq = state.server_seq++;
        error.ack = state.client_seq;
        error.window = state.window_size;
        error.type = PacketType::FILE_ERR;
        error.payload = "File not found";
        packets.push_back(error);
        return packets;
    }

    std::string line;
    while (std::getline(file, line)) {
        Packet p;
        p.seq = state.server_seq++;
        p.ack = state.client_seq;
        p.window = state.window_size;
        p.type = PacketType::FILE_DATA;
        p.payload = line;
        packets.push_back(p);
    }

    Packet eof;
    eof.seq = state.server_seq++;
    eof.ack = state.client_seq;
    eof.window = state.window_size;
    eof.type = PacketType::FILE_END;
    eof.payload = "";
    packets.push_back(eof);

    return packets;
}

Packet Protocol::makeErrorPacket(ConnectionState &state, const std::string &msg)
{
    Packet p;
    p.seq = state.server_seq++;
    p.ack = state.client_seq;
    p.window = state.window_size;
    p.type = PacketType::FILE_ERR;
    p.payload = msg;
    return p;
}

Packet Protocol::makeDataPacket(ConnectionState &state,
                                const std::string &payload)
{
    Packet p;
    p.seq = state.server_seq++;
    p.ack = state.client_seq;
    p.window = state.window_size;
    p.type = PacketType::FILE_DATA;
    p.payload = payload;
    return p;
}

Packet Protocol::makeEOFPacket(ConnectionState &state)
{
    Packet p;
    p.seq = state.server_seq++;
    p.ack = state.client_seq;
    p.window = state.window_size;
    p.type = PacketType::FILE_END;
    p.payload = "";
    return p;
}

void Protocol::sendPacket(int sockfd,
                          const Packet &pkt,
                          const sockaddr_in &client_addr)
{
    std::string raw = pkt.serialize();
    ssize_t n = sendto(sockfd, raw.c_str(), raw.size(), 0,
                       (sockaddr *) &client_addr, sizeof(client_addr));

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), ip, INET_ADDRSTRLEN);
    uint16_t port = ntohs(client_addr.sin_port);

    if (n >= 0 && static_cast<size_t>(n) == raw.size()) {
        std::cout << "📤 sendPacket 成功 → " << ip << ":" << port
                  << " type=" << to_string(pkt.type)
                  << " seq=" << pkt.seq << " ack=" << pkt.ack
                  << " size=" << raw.size() << "\n";
    } else {
        std::cerr << "❌ sendPacket 失敗 → " << ip << ":" << port
                  << " type=" << to_string(pkt.type)
                  << " seq=" << pkt.seq << " ack=" << pkt.ack
                  << " errno=" << strerror(errno)
                  << " size=" << raw.size() << "\n";
    }
}

bool Protocol::receivePacket(int sockfd, Packet &pkt, sockaddr_in *sender) {
    char buffer[4096];
    sockaddr_in from_addr;
    socklen_t len = sizeof(from_addr);

    ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                         (sockaddr *) &from_addr, &len);

    if (n <= 0) {
        std::cerr << "⚠️ receivePacket 失敗或 timeout → errno=" << strerror(errno) << "\n";
        return false;
    }

    pkt = Packet::deserialize(std::string(buffer, n));

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(from_addr.sin_addr), ip, INET_ADDRSTRLEN);
    uint16_t port = ntohs(from_addr.sin_port);

    std::cout << "📥 receivePacket 成功 ← " << ip << ":" << port
              << " type=" << to_string(pkt.type)
              << " seq=" << pkt.seq << " ack=" << pkt.ack
              << " size=" << n << "\n";

    if (sender) {
        *sender = from_addr;
    }

    return true;
}

std::vector<Packet> Protocol::collectAckPackets(int sockfd, size_t expected_ack_count) {
    std::vector<Packet> acks;
    int max_attempts = 5;
    for (int i = 0; i < max_attempts; ++i) {
        Packet p;
        if (receivePacket(sockfd, p) && p.type == PacketType::DATA_ACK) {
            acks.push_back(p);
        }
        if (acks.size() >= expected_ack_count) break;
        usleep(100000); // 100ms
    }
    return acks;
}

void Protocol::sendFileWithCongestionControl(const std::string &filename,
                                             ConnectionState &state,
                                             int sockfd,
                                             const sockaddr_in &client_addr)
{
    std::ifstream file("./files/" + filename);
    if (!file.is_open()) {
        Packet error = makeErrorPacket(state, "File not found");
        sendPacket(sockfd, error, client_addr);
        return;
    }

    struct timeval tv = {5, 0};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    size_t cwnd = 1;
    size_t ssthresh = 64;
    size_t flow_window = state.window_size;
    size_t duplicate_ack_count = 0;
    uint32_t last_ack_seq = 0;
    bool in_fast_recovery = false;

    std::string line;
    bool eof_reached = false;
    std::vector<Packet> inFlight;
    std::unordered_set<uint32_t> acked_seqs;

    while (!eof_reached) {
        size_t send_limit = std::min(cwnd, flow_window);
        size_t sent = 0;

        while (sent < send_limit) {
            if (!std::getline(file, line)) {
                eof_reached = true;
                break;
            }

            Packet p = makeDataPacket(state, line);
            sendPacket(sockfd, p, client_addr);
            std::cout << "📤 傳送封包 seq=" << p.seq << " cwnd=" << cwnd << "\n";
            inFlight.push_back(p);
            sent++;
        }

        std::vector<Packet> received_acks = collectAckPackets(sockfd, inFlight.size()); // ✅ 支援輪詢多次直到收滿 ACK
        std::vector<Packet> unackedPackets;

        for (Packet &p : inFlight) {
            bool acked = false;

            for (const Packet &ack : received_acks) {
                if (ack.seq == p.seq + 1) {
                    acked_seqs.insert(ack.seq);
                    state.client_seq = ack.seq;
                    std::cout << "✅ ACK received for seq=" << p.seq << "\n";
                    acked = true;

                    if (ack.seq == last_ack_seq) {
                        duplicate_ack_count++;
                        std::cout << "🔁 Duplicate ACK #" << duplicate_ack_count << "\n";
                        if (duplicate_ack_count == 3 && !in_fast_recovery) {
                            std::cout << "🚨 Fast Retransmit triggered for seq=" << p.seq << "\n";
                            ssthresh = std::max(cwnd / 2, size_t(1));
                            cwnd = ssthresh;
                            in_fast_recovery = true;
                            sendPacket(sockfd, p, client_addr);
                        }
                    } else {
                        duplicate_ack_count = 0;
                        last_ack_seq = ack.seq;
                        cwnd = (cwnd < ssthresh) ? cwnd * 2 : cwnd + 1;
                        std::cout << "📈 cwnd 成長為 " << cwnd << "（ssthresh=" << ssthresh << "）\n";
                        if (in_fast_recovery) {
                            std::cout << "🎯 Fast Recovery complete\n";
                            in_fast_recovery = false;
                        }
                    }

                    break;
                }
            }

            if (!acked) {
                if (acked_seqs.count(p.seq + 1) == 0) {
                    std::cout << "⚠️ Timeout or loss for seq=" << p.seq << "\n";
                    unackedPackets.push_back(p);

                    ssthresh = std::max(cwnd / 2, size_t(1));
                    cwnd = 1;
                    std::cout << "📉 cwnd 退回至 1（ssthresh=" << ssthresh << "）\n";
                    in_fast_recovery = false;
                    duplicate_ack_count = 0;
                } else {
                    std::cout << "⏭️ 已 ACK，跳過重傳 seq=" << p.seq << "\n";
                }
            }
        }

        for (Packet &p : unackedPackets) {
            std::cout << "🔁 重傳未 ACK 封包 seq=" << p.seq << "\n";
            sendPacket(sockfd, p, client_addr);
        }

        inFlight.clear();
    }

    Packet eof = makeEOFPacket(state);
    sendPacket(sockfd, eof, client_addr);
    std::cout << "📤 傳送 FILE_END 給 client\n";

    int attempts = 0;
    while (attempts < 5) {
        std::vector<Packet> acks = collectAckPackets(sockfd, 1); // 最多收 1 個 ACK
        for (const Packet &ack : acks) {
            if (ack.type == PacketType::DATA_ACK && ack.seq == eof.seq + 1) {
                std::cout << "✅ FILE_END 被 ACK\n";
                return;
            }
        }

        std::cout << "🔁 重傳 FILE_END（第 " << attempts + 1 << " 次）\n";
        sendPacket(sockfd, eof, client_addr);
        attempts++;
    }


    std::cout << "❌ FILE_END 未被 ACK，傳輸可能不完整\n";
}
