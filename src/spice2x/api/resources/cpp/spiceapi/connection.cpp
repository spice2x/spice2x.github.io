#include <iostream>
#include <ws2tcpip.h>
#include "connection.h"

namespace spiceapi {

    // settings
    static const size_t RECEIVE_BUFFER_SIZE = 64 * 1024;
    static const int RECEIVE_TIMEOUT = 1000;
}

spiceapi::Connection::Connection(std::string host, uint16_t port, std::string password) {
    this->host = host;
    this->port = port;
    this->password = password;
    this->socket = INVALID_SOCKET;
    this->cipher = nullptr;

    // WSA startup
    WSADATA wsa_data;
    int error = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (error) {
        std::cerr << "Failed to start WSA: " << error << std::endl;
        exit(1);
    }
}

spiceapi::Connection::~Connection() {

    // clean up
    if (this->cipher != nullptr)
        delete this->cipher;

    // cleanup WSA
    WSACleanup();
}

void spiceapi::Connection::cipher_alloc() {

    // delete old cipher
    if (this->cipher != nullptr) {
        delete this->cipher;
        this->cipher = nullptr;
    }

    // create new cipher if password is set
    if (this->password.length() > 0) {
        this->cipher = new RC4(
                (uint8_t *) this->password.c_str(),
                strlen(this->password.c_str()));
    }
}

bool spiceapi::Connection::check() {
    int result = 0;

    // check if socket is invalid
    if (this->socket == INVALID_SOCKET) {

        // get all addresses
        addrinfo *addr_list;
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        if ((result = getaddrinfo(
                this->host.c_str(),
                std::to_string(this->port).c_str(),
                &hints,
                &addr_list))) {
            std::cerr << "getaddrinfo failed: " << result << std::endl;
            return false;
        }

        // check all addresses
        for (addrinfo *addr = addr_list; addr != NULL; addr = addr->ai_next) {

            // try open socket
            this->socket = ::socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
            if (this->socket == INVALID_SOCKET) {
                std::cerr << "socket failed: " << WSAGetLastError() << std::endl;
                freeaddrinfo(addr_list);
                return false;
            }

            // try connect
            result = connect(this->socket, addr->ai_addr, (int) addr->ai_addrlen);
            if (result == SOCKET_ERROR) {
                closesocket(this->socket);
                this->socket = INVALID_SOCKET;
                continue;
            }

            // configure socket
            int opt_val;
            opt_val = 1;
            setsockopt(this->socket, IPPROTO_TCP, TCP_NODELAY, (const char*) &opt_val, sizeof(opt_val));
            opt_val = RECEIVE_TIMEOUT;
            setsockopt(this->socket, SOL_SOCKET, SO_RCVTIMEO, (const char*) &opt_val, sizeof(opt_val));

            // connection successful
            this->cipher_alloc();
            break;
        }

        // check if successful
        freeaddrinfo(addr_list);
        if (this->socket == INVALID_SOCKET) {
            return false;
        }
    }

    // socket probably still valid
    return true;
}

void spiceapi::Connection::change_pass(std::string password) {
    this->password = password;
    this->cipher_alloc();
}

std::string spiceapi::Connection::request(std::string json) {

    // check connection
    if (!this->check())
        return "";

    // crypt
    auto json_len = strlen(json.c_str()) + 1;
    uint8_t* json_data = new uint8_t[json_len];
    memcpy(json_data, json.c_str(), json_len);
    if (this->cipher != nullptr)
        this->cipher->crypt(json_data, json_len);

    // send
    auto send_result = send(this->socket, (const char*) json_data, (int) json_len, 0);
    delete[] json_data;
    if (send_result == SOCKET_ERROR || send_result < (int) json_len) {
        closesocket(this->socket);
        this->socket = INVALID_SOCKET;
        return "";
    }

    // receive
    uint8_t receive_data[RECEIVE_BUFFER_SIZE];
    size_t receive_data_len = 0;
    int receive_result;
    while ((receive_result = recv(
            this->socket,
            (char*) &receive_data[receive_data_len],
            sizeof(receive_data) - receive_data_len, 0)) > 0) {

        // check for buffer overflow
        if (receive_data_len + receive_result >= sizeof(receive_data)) {
            closesocket(this->socket);
            this->socket = INVALID_SOCKET;
            return "";
        }

        // crypt
        if (this->cipher != nullptr)
            this->cipher->crypt(&receive_data[receive_data_len], (size_t) receive_result);

        // increase received data length
        receive_data_len += receive_result;

        // check for message end
        if (receive_data[receive_data_len - 1] == 0)
            break;
    }

    // return resulting json
    if (receive_data_len > 0) {
        return std::string((const char *) &receive_data[0], receive_data_len - 1);
    } else {

        // receive error
        this->socket = INVALID_SOCKET;
        return "";
    }
}
