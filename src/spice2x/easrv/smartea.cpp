#include "smartea.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include "util/utils.h"
#include "util/logging.h"

namespace smartea {

    bool check_url(std::string url) {

        // WSA startup
        WSADATA wsa_data;
        int error;
        if ((error = WSAStartup(MAKEWORD(2, 2), &wsa_data)) != 0) {
            log_fatal("smartea", "WSAStartup returned {}", error);
        }

        // create hints
        addrinfo hints;
        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        // get port
        std::vector<std::string> url_split;
        strsplit(url, url_split, ':');
        int port = 80;
        if (url_split.size() >= 2 && string_begins_with(url_split[0], "http"))
            url_split.erase(std::begin(url_split));
        while (url_split.size() >= 1 && url_split[0].length() > 0 && url_split[0][0] == '/')
            url_split[0] = url_split[0].substr(1);
        if (url_split.size() >= 2)
            port = strtol(url_split[1].c_str(), nullptr, 10);

        // remove path from host
        auto host_slash = url_split[0].find("/");
        if (host_slash != std::string::npos) {
            url_split[0] = url_split[0].substr(0, host_slash);
        }

        // get address info
        addrinfo *result;
        if ((error = getaddrinfo(url_split[0].c_str(), to_string(port).c_str(), &hints, &result)) != 0) {
            log_info("smartea", "could not resolve {}:{}: {}", url_split[0], url_split[1], error);
            WSACleanup();
            return false;
        }

        // iterate list
        bool success = false;
        for (addrinfo *ptr = result; ptr != nullptr; ptr = ptr->ai_next) {

            // create socket
            SOCKET sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
            if (sock == INVALID_SOCKET) {
                log_info("smartea", "could not create socket: {}", WSAGetLastError());
                WSACleanup();
                return false;
            }

            // connect to server
            error = connect(sock, ptr->ai_addr, (int) ptr->ai_addrlen);
            if (error == SOCKET_ERROR) {
                closesocket(sock);
                continue;
            }

            // success
            success = true;
            closesocket(sock);
            break;
        }

        // WSA shutdown
        WSACleanup();

        // success
        if (success)
            log_info("smartea", "server seems to be available :)");
        else
            log_info("smartea", "server seems to be dead :(");
        return success;
    }
}
