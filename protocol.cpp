#include "protocol.hpp"
#include "packet.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>


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
    sendto(sockfd, raw.c_str(), raw.size(), 0, (sockaddr *) &client_addr,
           sizeof(client_addr));
}

bool Protocol::receivePacket(int sockfd, Packet &pkt)
{
    char buffer[4096];
    sockaddr_in from_addr;
    socklen_t len = sizeof(from_addr);
    ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                         (sockaddr *) &from_addr, &len);
    if (n <= 0)
        return false;
    pkt = Packet::deserialize(std::string(buffer, n));
    return true;
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

    size_t cwnd = 1;
    size_t ssthresh = 64;
    size_t flow_window = state.window_size;
    std::string line;
    std::vector<Packet> inFlight;

    while (std::getline(file, line)) {
        size_t send_limit = std::min(cwnd, flow_window);
        for (size_t i = 0; i < send_limit && std::getline(file, line); ++i) {
            Packet p = makeDataPacket(state, line);
            sendPacket(sockfd, p, client_addr);
            inFlight.push_back(p);
        }

        // 等待 ACKs 並顯示 debug 資訊
        for (Packet &p : inFlight) {
            std::cout << "📤 傳送封包 seq=" << p.seq << " cwnd=" << cwnd
                      << "\n";

            Packet ack;

            if (receivePacket(sockfd, ack) && ack.type == PacketType::ACK) {
                std::cout << "✅ ACK received for seq=" << p.seq << "\n";
                state.client_seq = ack.seq;
            } else {
                std::cout << "⚠️ Timeout or loss for seq=" << p.seq << "\n";
                ssthresh = std::max(cwnd / 2, size_t(1));
                cwnd = 1;
                break;
            }
        }


        // 慢啟動或壅塞避免
        if (cwnd < ssthresh)
            cwnd *= 2;
        else
            cwnd += 1;

        inFlight.clear();
    }

    Packet eof = makeEOFPacket(state);
    sendPacket(sockfd, eof, client_addr);
}
