#pragma once
#include <string>
#include <sstream>
#include <cstdint> 

enum class PacketType {
    SYN,
    SYN_ACK,
    ACK,
    FILE_REQ,
    FILE_DATA,
    FILE_ERR,
    FILE_END,
    EXPR_REQ,
    EXPR_RES,
    DATA_ACK
};

static PacketType parsePacketType(int value) {
    if (value < static_cast<int>(PacketType::SYN) || value > static_cast<int>(PacketType::DATA_ACK)) {
        throw std::invalid_argument("Invalid PacketType value");
    }
    return static_cast<PacketType>(value);
}

struct Packet {
    uint32_t seq;
    uint32_t ack;
    uint16_t window;
    PacketType type;
    std::string payload;

    std::string serialize() const {
        std::ostringstream oss;
        oss << static_cast<int>(type) << "|" << seq << "|" << ack << "|" << window << "|" << payload;
        return oss.str();
    }

    static Packet deserialize(const std::string& raw) {
        std::istringstream iss(raw);
        std::string token;
        Packet pkt;

        std::getline(iss, token, '|');
        pkt.type = parsePacketType(std::stoi(token));

        std::getline(iss, token, '|');
        pkt.seq = std::stoi(token);

        std::getline(iss, token, '|');
        pkt.ack = std::stoi(token);

        std::getline(iss, token, '|');
        pkt.window = std::stoi(token);

        std::getline(iss, pkt.payload);

        return pkt;
    }
};

inline std::string to_string(PacketType type) {
    switch (type) {
        case PacketType::SYN: return "SYN";
        case PacketType::SYN_ACK: return "SYN_ACK";
        case PacketType::ACK: return "ACK";
        case PacketType::FILE_REQ: return "FILE_REQ";
        case PacketType::FILE_DATA: return "FILE_DATA";
        case PacketType::FILE_ERR: return "FILE_ERR";
        case PacketType::FILE_END: return "FILE_END";
        case PacketType::EXPR_REQ: return "EXPR_REQ";
        case PacketType::EXPR_RES: return "EXPR_RES";
        case PacketType::DATA_ACK: return "DATA_ACK";
        default: return "UNKNOWN";
    }
}
