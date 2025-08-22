#pragma once
#include <vector>
#include <netinet/in.h> 
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "connection.hpp"
#include "packet.hpp"

class Protocol {
public:
    Packet handleHandshake(const Packet &pkt, ConnectionState &state, const std::string &client_key);
    Packet handleExpression(const std::string &expr, ConnectionState &state);
    std::vector<Packet> handleFileRequest(const std::string &filename, ConnectionState &state);

    void sendFileWithCongestionControl(const std::string &filename,
                                       ConnectionState &state,
                                       int sockfd,
                                       const sockaddr_in &client_addr);

private:
    Packet makeErrorPacket(ConnectionState &state, const std::string &msg);
    Packet makeDataPacket(ConnectionState &state, const std::string &payload);
    Packet makeEOFPacket(ConnectionState &state);

    void sendPacket(int sockfd, const Packet &pkt, const sockaddr_in &client_addr);
    bool receivePacket(int sockfd, Packet &pkt);
};


