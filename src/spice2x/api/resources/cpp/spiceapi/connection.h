#ifndef SPICEAPI_CONNECTION_H
#define SPICEAPI_CONNECTION_H

#include <string>
#include <winsock2.h>
#include "rc4.h"

namespace spiceapi {

    class Connection {
    private:
        std::string host;
        uint16_t port;
        std::string password;
        SOCKET socket;
        RC4* cipher;

        void cipher_alloc();

    public:
        Connection(std::string host, uint16_t port, std::string password = "");
        ~Connection();

        bool check();
        void change_pass(std::string password);
        std::string request(std::string json);

    };
}

#endif //SPICEAPI_CONNECTION_H
