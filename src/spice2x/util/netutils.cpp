#include <winsock2.h>
#include <ws2tcpip.h>
#include "netutils.h"
#include <iphlpapi.h>
#include <windows.h>
#include "util/utils.h"


namespace netutils {

    std::vector<std::string> get_local_addresses() {
        std::vector<std::string> return_addresses;

        // use 16KB buffer and resize if needed
        DWORD buffer_size = 16 * 1024;
        IP_ADAPTER_ADDRESSES* adapter_addresses = nullptr;
        for (size_t attempt_no = 0; attempt_no < 3; attempt_no++) {

            // get adapter addresses
            adapter_addresses = (IP_ADAPTER_ADDRESSES*) malloc(buffer_size);
            DWORD error;
            if ((error = GetAdaptersAddresses(
                        AF_UNSPEC,
                        GAA_FLAG_SKIP_ANYCAST |
                        GAA_FLAG_SKIP_MULTICAST |
                        GAA_FLAG_SKIP_DNS_SERVER |
                        GAA_FLAG_SKIP_FRIENDLY_NAME,
                        NULL,
                        adapter_addresses,
                        &buffer_size)) == ERROR_SUCCESS) {

                // success
                break;

            } else if (error == ERROR_BUFFER_OVERFLOW) {

                // retry using new size
                free(adapter_addresses);
                adapter_addresses = NULL;
                continue;

            } else {

                // error - return empty list
                free(adapter_addresses);
                return return_addresses;
            }
        }

        // now iterate the adapters
        for (IP_ADAPTER_ADDRESSES* adapter = adapter_addresses; NULL != adapter; adapter = adapter->Next) {

            // check if loopback
            if (IF_TYPE_SOFTWARE_LOOPBACK == adapter->IfType) {
                continue;
            }

            // iterate all IPv4 and IPv6 addresses
            for (IP_ADAPTER_UNICAST_ADDRESS* adr = adapter->FirstUnicastAddress; adr; adr = adr->Next) {
                switch (adr->Address.lpSockaddr->sa_family) {
                    case AF_INET: {

                        // cast address
                        SOCKADDR_IN* ipv4 = reinterpret_cast<SOCKADDR_IN*>(adr->Address.lpSockaddr);

                        // convert to string
                        char str_buffer[INET_ADDRSTRLEN] = {0};
                        inet_ntop(AF_INET, &(ipv4->sin_addr), str_buffer, INET_ADDRSTRLEN);

                        // save result
                        return_addresses.push_back(str_buffer);
                        break;
                    }
                    case AF_INET6: {

                        // cast address
                        SOCKADDR_IN6* ipv6 = reinterpret_cast<SOCKADDR_IN6*>(adr->Address.lpSockaddr);

                        // convert to string
                        char str_buffer[INET6_ADDRSTRLEN] = {0};
                        inet_ntop(AF_INET6, &(ipv6->sin6_addr), str_buffer, INET6_ADDRSTRLEN);
                        std::string ipv6_str(str_buffer);

                        // skip non-external addresses
                        if (!ipv6_str.find("fe")) {
                            char c = ipv6_str[2];
                            if (c == '8' || c == '9' || c == 'a' || c == 'b') {

                                // link local address
                                continue;
                            }
                        }
                        else if (!ipv6_str.find("2001:0:")) {

                            // special use address
                            continue;
                        }

                        // save result
                        return_addresses.push_back(ipv6_str);
                        break;
                    }
                    default: {
                        continue;
                    }
                }
            }
        }

        // success
        free(adapter_addresses);
        return return_addresses;
    }

    /*!
    *
    * HTTP Status Codes - C++ Variant
    *
    * https://github.com/j-ulrich/http-status-codes-cpp
    *
    * \version 1.5.0
    * \author Jochen Ulrich <jochenulrich@t-online.de>
    * \copyright Licensed under Creative Commons CC0 (http://creativecommons.org/publicdomain/zero/1.0/)
    */
    std::string http_status_reason_phrase(int code) {
        switch (code) {

            //####### 1xx - Informational #######
            case 100: return "Continue";
            case 101: return "Switching Protocols";
            case 102: return "Processing";
            case 103: return "Early Hints";

            //####### 2xx - Successful #######
            case 200: return "OK";
            case 201: return "Created";
            case 202: return "Accepted";
            case 203: return "Non-Authoritative Information";
            case 204: return "No Content";
            case 205: return "Reset Content";
            case 206: return "Partial Content";
            case 207: return "Multi-Status";
            case 208: return "Already Reported";
            case 226: return "IM Used";

            //####### 3xx - Redirection #######
            case 300: return "Multiple Choices";
            case 301: return "Moved Permanently";
            case 302: return "Found";
            case 303: return "See Other";
            case 304: return "Not Modified";
            case 305: return "Use Proxy";
            case 307: return "Temporary Redirect";
            case 308: return "Permanent Redirect";

            //####### 4xx - Client Error #######
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 402: return "Payment Required";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 406: return "Not Acceptable";
            case 407: return "Proxy Authentication Required";
            case 408: return "Request Timeout";
            case 409: return "Conflict";
            case 410: return "Gone";
            case 411: return "Length Required";
            case 412: return "Precondition Failed";
            case 413: return "Content Too Large";
            case 414: return "URI Too Long";
            case 415: return "Unsupported Media Type";
            case 416: return "Range Not Satisfiable";
            case 417: return "Expectation Failed";
            case 418: return "I'm a teapot";
            case 421: return "Misdirected Request";
            case 422: return "Unprocessable Content";
            case 423: return "Locked";
            case 424: return "Failed Dependency";
            case 425: return "Too Early";
            case 426: return "Upgrade Required";
            case 428: return "Precondition Required";
            case 429: return "Too Many Requests";
            case 431: return "Request Header Fields Too Large";
            case 451: return "Unavailable For Legal Reasons";

            //####### 5xx - Server Error #######
            case 500: return "Internal Server Error";
            case 501: return "Not Implemented";
            case 502: return "Bad Gateway";
            case 503: return "Service Unavailable";
            case 504: return "Gateway Timeout";
            case 505: return "HTTP Version Not Supported";
            case 506: return "Variant Also Negotiates";
            case 507: return "Insufficient Storage";
            case 508: return "Loop Detected";
            case 510: return "Not Extended";
            case 511: return "Network Authentication Required";

            default: return std::string();
        }
    }
}
