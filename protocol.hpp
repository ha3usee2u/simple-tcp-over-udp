#pragma once
#include <vector>

#include "connection.hpp"
#include "packet.hpp"

class Protocol
{
public:
    Packet handleHandshake(const Packet &pkt,
                           ConnectionState &state,
                           const std::string &client_key);
    Packet handleData(const Packet &pkt, ConnectionState &state);
    std::vector<Packet> handleFileRequest(const std::string &filename,
                                          ConnectionState &state);
    Packet handleExpression(const std::string &expr, ConnectionState &state);
};
