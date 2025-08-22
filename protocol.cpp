#include "protocol.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include "packet.hpp"

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
