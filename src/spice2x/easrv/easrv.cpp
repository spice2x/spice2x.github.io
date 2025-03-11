#include "easrv.h"

#include <sstream>
#include <vector>
#include <thread>

#include <winsock2.h>

#include "avs/game.h"
#include "avs/automap.h"
#include "util/logging.h"
#include "util/utils.h"

extern "C" {
#include "external/http-parser/http_parser.h"
}

#include "responses/bs_info2_common.h"
#include "responses/bs_pcb2_boot.h"
#include "responses/bs_pcb2_error.h"
#include "responses/op2_common_get_music_info.h"
#include "responses/pcbtracker_alive.h"

static const size_t RECEIVED_BUFFER_LENGTH = 2048*1024;
static const size_t URL_BUFFER_SIZE = 1024;
static const size_t HEADER_BUFFER_SIZE = 1024;

static SOCKET SERVER_SOCKET;
static bool SERVER_RUNNING;
static bool SERVER_MAINTENANCE;

// worker state
static thread_local char URL_BUFFER[URL_BUFFER_SIZE] {};
static thread_local size_t URL_BUFFER_LENGTH = 0;
static thread_local char HEADER_BUFFER[URL_BUFFER_SIZE] {};
static thread_local size_t HEADER_BUFFER_LENGTH = 0;
static thread_local bool HEADER_CRYPT = false;
static thread_local enum header_state_t {
    HS_UNKNOWN,
    HS_EAMUSE_INFO,
    HS_COMPRESS,
} HEADER_STATE = HS_UNKNOWN;

static std::string HTTP_DEFAULT;
static std::string EA_HEADER;
static std::string EA_HEADER_PLAIN;
static std::string EA_EMPTY;
static std::string EA_EMPTY_CRYPT;
static std::string EA_SERVICES_GET_FULL;
static std::string EA_MESSAGE_GET;
static std::string EA_MESSAGE_GET_MAINTENANCE;
static std::string EA_FACILITY_GET;
static std::string EA_PCBEVENT_PUT;
static std::string EA_PACKAGE_LIST;
static std::string EA_TAX_GET_PHASE;
static std::string EA_EVENTLOG_WRITE;
static std::string EA_MACHINE_GET_CONTROL;
static std::string EA_I36_SYSTEM_GETMASTER;
static std::string EA_KGG_SYSTEM_GETMASTER;
static std::string EA_KGG_HDKOPERATION_GET;

static inline void easrv_init_messages();

static int on_url(http_parser *, const char *data, size_t length) {

    // prevent buffer overflow
    if (URL_BUFFER_LENGTH + length >= URL_BUFFER_SIZE) {
        return 1;
    }

    // append to buffer
    memcpy(&URL_BUFFER[URL_BUFFER_LENGTH], data, length);
    URL_BUFFER_LENGTH += length;
    URL_BUFFER[URL_BUFFER_LENGTH] = '\0';
    return 0;
}

static int on_header_field(http_parser *, const char *data, size_t length) {

    // prevent buffer overflow
    if (HEADER_BUFFER_LENGTH + length >= HEADER_BUFFER_SIZE) {
        return 1;
    }

    // append to buffer
    memcpy(&HEADER_BUFFER[HEADER_BUFFER_LENGTH], data, length);
    HEADER_BUFFER_LENGTH += length;
    HEADER_BUFFER[HEADER_BUFFER_LENGTH] = '\0';

    // check value
    auto field = std::string(data, length);
    if (field == "X-Eamuse-Info") {
        HEADER_STATE = HS_EAMUSE_INFO;
    } else if (field == "X-Compress") {
        HEADER_STATE = HS_COMPRESS;
    } else {
        HEADER_STATE = HS_UNKNOWN;
    }

    // success
    return 0;
}

static int on_header_value(http_parser *, const char *data, size_t length) {

    // decide what to do with the value based on current header state
    switch (HEADER_STATE) {
        case HS_EAMUSE_INFO:
            HEADER_CRYPT = true;
        case HS_UNKNOWN:
        default:
            break;
    }

    // success
    return 0;
}

static inline bool check_url(const char *module, const char *method) {
    std::string url(URL_BUFFER, URL_BUFFER_LENGTH);
    std::ostringstream check1;
    check1 << "module=" << module << "&method=" << method;
    if (url.find(check1.str()) != std::string::npos) {
        return true;
    }
    std::ostringstream check2;
    check2 << '/' << module << '/' << method;
    return url.find(check2.str()) != std::string::npos;
}

static inline void easrv_add_data(std::vector<char> &send, const std::string &data, bool crypt = true) {

    // copy header
    if (crypt) {
        send.reserve(EA_HEADER.size());

        for (size_t i = 0; i < EA_HEADER.size(); i++) {
            send.push_back(EA_HEADER[i]);
        }
    } else {
        send.reserve(EA_HEADER_PLAIN.size());

        for (size_t i = 0; i < EA_HEADER_PLAIN.size(); i++) {
            send.push_back(EA_HEADER_PLAIN[i]);
        }
    }

    // calculate length from encoded data because of null characters
    // if 1 follows after 1 its a 1
    // if 2 follows after 1 its a 0
    bool escape = false;
    size_t size = 0;
    for (char c : data) {
        if (escape) {
            escape = false;
            size++;
        } else if (c == 1) {
            escape = true;
        } else {
            size++;
        }
    }

    // add content length
    auto content_length = fmt::format("Content-Length: {}\r\n\r\n", size);
    send.reserve(content_length.size());
    for (char c : content_length) {
        send.push_back(c);
    }

    // add encoded data
    escape = false;
    for (char c : data) {
        if (escape) {
            if (c == 2) {
                c = 0;
            }
            escape = false;
            send.push_back(c);
        } else if (c == 1) {
            escape = true;
        } else {
            send.push_back(c);
        }
    }
}

static inline void easrv_add_data_raw(std::vector<char> &send, const unsigned char *data, size_t size, bool crypt = true) {

    // copy header
    if (crypt) {
        send.reserve(EA_HEADER.size());

        for (size_t i = 0; i < EA_HEADER.size(); i++) {
            send.push_back(EA_HEADER[i]);
        }
    } else {
        send.reserve(EA_HEADER_PLAIN.size());

        for (size_t i = 0; i < EA_HEADER_PLAIN.size(); i++) {
            send.push_back(EA_HEADER_PLAIN[i]);
        }
    }

    // add content length
    auto content_length = fmt::format("Content-Length: {}\r\n\r\n", size);
    send.reserve(content_length.size());
    for (char c : content_length) {
        send.push_back(c);
    }

    send.reserve(size);

    for (size_t i = 0; i < size; i++) {
        send.push_back(data[i]);
    }
}

static SOCKET easrv_worker_method() {

    // get connection
    sockaddr_in client_address;
    int socket_in_size = sizeof(sockaddr_in);
    SOCKET connected = accept(SERVER_SOCKET, (sockaddr *) &client_address, &socket_in_size);
    if (connected == INVALID_SOCKET) {
        return connected;
    }

    // receive data
    std::vector<char> received(RECEIVED_BUFFER_LENGTH);
    int received_length = recv(connected, &received[0], received.size(), 0);
    if (received_length == -1) {
        return connected;
    }

    // create parser
    http_parser parser {};
    http_parser_init(&parser, HTTP_REQUEST);

    // reset state
    URL_BUFFER_LENGTH = 0;
    URL_BUFFER[0] = 0x00;
    HEADER_BUFFER_LENGTH = 0;
    HEADER_BUFFER[0] = 0x00;
    HEADER_STATE = HS_UNKNOWN;

    // parser settings
    http_parser_settings parser_settings;
    http_parser_settings_init(&parser_settings);
    parser_settings.on_url = on_url;
    parser_settings.on_header_field = on_header_field;
    parser_settings.on_header_value = on_header_value;

    // execute
    size_t parsed_length = http_parser_execute(&parser, &parser_settings, &received[0], (size_t) received_length);
    if (parsed_length != (size_t) received_length)
        return connected;

    // check if protocol is changed
    if (parser.upgrade)
        return connected;

    // check for unsupported method
    if (parser.method != HTTP_GET && parser.method != HTTP_POST)
        return connected;

    // send data
    std::vector<char> send_data;
    switch (parser.method) {
        case HTTP_GET: {

            // this is probably a browser, so display the HTTP default message
            for (size_t i = 0; i < HTTP_DEFAULT.size(); i++) {
                send_data.push_back(HTTP_DEFAULT[i]);
            }
            break;
        }
        case HTTP_POST: {

            // services
            if (check_url("services", "get")) {
                easrv_add_data(send_data, EA_SERVICES_GET_FULL);
            } else if (check_url("pcbtracker", "alive")) {
                easrv_add_data_raw(
                        send_data,
                        PCBTRACKER_ALIVE_BIN,
                        PCBTRACKER_ALIVE_BIN_LEN
                );
            } else if (check_url("message", "get")) {
                if (SERVER_MAINTENANCE) {
                    easrv_add_data(send_data, EA_MESSAGE_GET_MAINTENANCE);
                } else {
                    easrv_add_data(send_data, EA_MESSAGE_GET);
                }
            } else if (check_url("facility", "get")) {
                easrv_add_data(send_data, EA_FACILITY_GET);
            } else if (check_url("pcbevent", "put")) {
                easrv_add_data(send_data, EA_PCBEVENT_PUT);
            } else if (check_url("package", "list")) {
                easrv_add_data(send_data, EA_PACKAGE_LIST, false);
            } else if (check_url("tax", "get_phase")) {
                easrv_add_data(send_data, EA_TAX_GET_PHASE);
            } else if (check_url("eventlog", "write")) {
                easrv_add_data(send_data, EA_EVENTLOG_WRITE);
            } else if (check_url("machine", "get_control")) {
                easrv_add_data(send_data, EA_MACHINE_GET_CONTROL);
            } else if (check_url("info2", "common")) {
                easrv_add_data_raw(
                        send_data,
                        BS_INFO2_COMMON_BIN,
                        BS_INFO2_COMMON_BIN_LEN,
                        false
                );
            } else if (check_url("pcb2", "boot")) {
                easrv_add_data_raw(
                        send_data,
                        BS_PCB2_BOOT_BIN,
                        BS_PCB2_BOOT_BIN_LEN,
                        false
                );
            } else if (check_url("pcb2", "error")) {
                easrv_add_data_raw(
                        send_data,
                        BS_PCB2_ERROR_BIN,
                        BS_PCB2_ERROR_BIN_LEN,
                        false
                );
            } else if (check_url("system", "getmaster")) {
                if (avs::game::is_model("KGG")) {
                    easrv_add_data(send_data, EA_KGG_SYSTEM_GETMASTER);
                } else if (avs::game::is_model("I36")) {
                    easrv_add_data(send_data, EA_I36_SYSTEM_GETMASTER);
                } else {
                    log_warning("easrv", "system.getmaster not available for this game model");
                    easrv_add_data(send_data, HEADER_CRYPT ? EA_EMPTY_CRYPT : EA_EMPTY);
                }
            } else if (check_url("hdkoperation", "get")) {
                easrv_add_data(send_data, EA_KGG_HDKOPERATION_GET);
            } else if (check_url("op2_common", "get_music_info")) {
                easrv_add_data_raw(
                        send_data,
                        OP2_COMMON_GET_MUSIC_INFO_BIN,
                        OP2_COMMON_GET_MUSIC_INFO_BIN_LEN
                );
            } else if (string_begins_with(std::string(URL_BUFFER), "//")) {
                log_warning("easrv", "unknown URL: {}", std::string(URL_BUFFER, URL_BUFFER_LENGTH));
                easrv_add_data(send_data, HEADER_CRYPT ? EA_EMPTY_CRYPT : EA_EMPTY);
            }
            break;
        }
        default:
            return connected;
    }

    // send data
    const char *send_ptr = send_data.data();
    auto send_size = send_data.size();
    while (send_size > 0) {
        int bytes_sent = send(connected, send_ptr, send_size, 0);
        if (bytes_sent == SOCKET_ERROR) {
            return connected;
        } else if (bytes_sent > 0) {
            send_size -= bytes_sent;
            send_ptr += bytes_sent;
        } else {
            return connected;
        }
    }

    // flush data
    shutdown(connected, SD_SEND);

    // exit
    return connected;
}

static void easrv_worker() {

    // exit when running flag is cleared
    while (SERVER_RUNNING) {

        // call worker method and close socket afterwards
        SOCKET connected = easrv_worker_method();
        if (connected != INVALID_SOCKET) {
            closesocket(connected);
        }
    }
}

void easrv_start(unsigned short port, bool maintenance, int backlog, int thread_count) {
    if (avs::game::is_model("UJK")) {
        log_fatal("easrv", "easrv is currently non-functional for Chase Chase Jokers; turn off -ea");
    }

    // WSA startup
    WSADATA wsa_data;
    int error;
    if ((error = WSAStartup(MAKEWORD(2, 2), &wsa_data)) != 0) {
        log_fatal("easrv", "WSAStartup returned {}", error);
    }

    // create socket
    SERVER_SOCKET = socket(AF_INET, SOCK_STREAM, 0);
    if (SERVER_SOCKET == INVALID_SOCKET) {
        log_fatal("easrv", "could not create socket");
    }

    // configure socket
    if (setsockopt(SERVER_SOCKET, SOL_SOCKET, SO_REUSEADDR, (const char *) &error, sizeof(int)) == -1) {
        log_fatal("easrv", "could not set socket options");
    }

    // create address
    sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = INADDR_ANY;
    memset(&server_address.sin_zero, 0, sizeof(server_address.sin_zero));

    // bind socket to address
    if (bind(SERVER_SOCKET, (sockaddr *) &server_address, sizeof(sockaddr)) == -1) {
        log_fatal("easrv", "Could not bind socket. The port might be blocked, try restarting your PC or stopping background programs");
    }

    // set socket to listen
    if (listen(SERVER_SOCKET, backlog) == -1) {
        log_fatal("easrv", "could not listen to socket");
    }

    // set server maintenance
    SERVER_MAINTENANCE = maintenance;

    // init messages
    easrv_init_messages();

    // create workers
    SERVER_RUNNING = true;
    for (int i = 0; i < thread_count; i++) {
        new std::thread(easrv_worker);
    }

    // information
    log_info("easrv", "EASRV running on port {}", port);
}

void easrv_shutdown() {

    // don't shutdown if not running
    if (!SERVER_RUNNING) {
        return;
    }

    // set running to false
    SERVER_RUNNING = false;
    closesocket(SERVER_SOCKET);

    // give workers time to exit
    Sleep(100);
}

static inline void easrv_init_messages() {
    HTTP_DEFAULT = std::string(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: 204\r\n"
            "Connection: close\r\n"
            "\r\n<HTML><HEAD><TITLE>SpiceTools EASRV</TITLE></HEAD>"
            "<BODY><H1>SpiceTools EASRV</H1>\r\n<IMG src="
            "\"https://upload.wikimedia.org/wikipedia/commons/thumb/1/19/Felfel-e_t.JPG/800px-Felfel-e_t.JPG\""
            "></BODY></HTML>"
    );
    EA_HEADER = std::string(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Server: SpiceTools\r\n"
            "X-Eamuse-Info: 1-53d121c7-a8b3\r\n"
            "X-Compress: none\r\n"
            "Connection: close\r\n"
    );
    EA_HEADER_PLAIN = std::string(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Server: SpiceTools\r\n"
            "X-Compress: none\r\n"
            "Connection: close\r\n"
    );
    EA_EMPTY = std::string(
            "\xA0\x42\x80\x7F\x01\x02\x01\x02\x01\x02\x10\x01\x01\x08\xDE\xAE\x35\xD3\x3E\x2A\x01\x01\x04\xA6\x6E\x66"
            "\xFE\xFE\xFF\x01\x02\x01\x02\x01\x02\x01\x02"
    );
    EA_EMPTY_CRYPT = std::string(
            "\x17\x7E\xFC\x8E\x1A\x15\x41\x0F\xAE\xD5\xA0\xF5\x2C\xA1\xB9\xD2\x53\xC6\xA2\xEA\x0D\x24\x89\x92\x87\x2E"
            "\x37\x9D"
    );
    EA_SERVICES_GET_FULL = std::string(
            "\x17\x7E\xFC\x8E\x1A\x15\x40\x2B\xAE\xD5\xA0\xF5\x2C\xA1\xB9\xD2\x53\xCA\xE6\x29\x90\x60\xFD\xD5\x86\x2A"
            "\x8C\x07\x07\xD5\xEB\x59\x63\x6D\x03\xBB\x49\x06\xBF\x7B\x0B\xBF\xE4\x91\x9E\x9F\x85\xAB\x5F\x7F\x54\xF5"
            "\x44\xB6\x51\xA4\x16\x50\xBE\xCC\x4B\x7D\xD7\xF0\xC1\xDC\xB9\xAE\x4E\x5B\x4C\x56\xA6\x46\xD9\x7A\x61\x8E"
            "\x21\xBA\x24\x27\xFB\xAF\xC5\xA6\xC8\xF1\x69\x2B\xDC\x2B\xF0\x26\x20\xD2\x46\xF7\x87\x3C\x2D\x3D\x64\xD9"
            "\xF5\x5F\xD7\xFB\xDC\x53\xF2\x1A\x21\x2B\x2D\xD4\xE2\x56\xAE\x0F\xB5\x29\x0F\xB3\x9F\x02\x71\x5B\xFB\x7F"
            "\xC1\xBA\xF9\xA1\xE8\x61\x4F\x02\x9D\x47\x19\xDD\x79\xC5\x1A\x36\x19\x3B\x02\x60\xB2\x6A\x71\xAE\xE9\xA7"
            "\x8B\x43\x74\x23\xC0\x3E\x1D\x77\x2C\x60\xC9\x15\x05\x89\x73\x82\x5E\x71\xB9\xD3\xBB\x76\x30\xDE\x69\x5F"
            "\x95\x8F\xC0\x1C\xDB\x6D\x7C\x24\xC2\x2A\x17\xC4\xA0\xCF\x46\x14\xF5\x64\x1C\x48\xFE\xF2\xD0\xF2\x01\x02"
            "\x39\x8D\x1B\x1B\xC0\xAD\x5D\xCD\xDE\x16\x3E\xF6\x2F\x9D\x6A\x53\x11\x0B\xC6\x6E\xAA\xB7\xFC\x28\x48\x6B"
            "\xAF\xD5\x6A\xD9\x79\xCE\x4F\xD9\x9E\xF3\x54\xE1\x0D\xB8\x92\xA1\xE0\xBD\x6B\x50\x8B\xD5\x19\x9C\xB7\x86"
            "\xF7\x43\x99\x8B\x53\x2A\x7A\x1B\x5F\x96\x04\xB7\x60\x80\x42\xB7\x85\xE0\xCB\x15\x11\x81\x6E\x95\x97\x50"
            "\x43\x8D\xE1\x5C\x84\x21\x3A\x8B\x1E\xD8\xAB\x64\x07\x27\x64\x31\x90\x7D\xEF\x75\xAB\x36\x0D\x4A\x0C\xC5"
            "\x27\x58\xFE\xA7\xCC\xD5\xF9\x8F\xA3\x19\xBE\x3B\xBD\x6A\x98\xA5\xDD\x05\xE5\x51\xA1\xE9\x39\xD7\x29\x64"
            "\x60\x4D\x94\x40\xB8\xDB\x91\x93\x80\x10\x33\xAB\xEA\xFB\xD4\x5E\x71\x02\x66\xE6\x33\xDD\xCF\x65\x60\x47"
            "\x7F\xD4\x1E\x8D\xF8\xB1\xB9\xEA\x44\xFA\xB3\xCA\xCD\x50\x09\x6C\x7D\xB4\xAC\x01\x01\xFB\x7B\xCC\xCF\xA3"
            "\x51\xA8\x4C\x97\x8B\x20\xE3\x29\x82\x9A\x68\xB4\xDA\x01\x02\xD8\x12\xCF\xCF\x04\x68\x3C\xE7\x2C\xFB\x2D"
            "\x9A\xB8\xE5\xA1\x78\xFA\xF4\xE2\x99\x10\x03\xBE\x95\x3F\x62\x81\x1A\x81\x26\x99\xBA\x12\x7C\x16\x93\xFF"
            "\x33\x73\x39\x96\xAC\x50\xBA\x53\x8B\xF1\xCF\x7C\x1C\x63\xB8\x69\x71\x01\x02\x5A\x52\x82\xE9\xEC\xEF\x5D"
            "\x27\x88\xEA\x43\x2D\xC4\x93\xCB\xE2\x70\x9B\xE0\x03\x46\xC6\x40\x0E\xC5\xDB\x76\x2A\xD2\x68\x0C\xA7\x14"
            "\xE5\x41\x28\xDC\x38\x18\x0C\x2B\x3D\x69\x73\x15\xAD\x98\x81\xB4\x9E\x62\x95\xD5\x60\x0B\x98\x12\xEB\x42"
            "\x27\xBA\xAC\xE6\x5C\x7F\x3C\x9E\xB6\xCB\xF7\xB1\x35\x54\x03\xE1\xD1\xE6\xB6\xF1\x60\x94\xFA\x03\xEE\xF9"
            "\xB9\x52\x33\xC2\xF5\xFB\x66\xE3\xF1\x23\x12\x9E\xD8\xEE\x8A\x83\x66\xB0\x40\x42\x12\x3E\xC9\x61\x84\x39"
            "\x68\xFD\x5A\x9D\x6F\x23\x7B\x79\xFC\x79\xA3\xD8\x0B\xB4\x7C\xEA\x33\x83\x18\xCA\x0E\x44\x90\xC3\x9E\xAE"
            "\x90\xBE\xCC\xB8\x44\x1C\xB8\xD5\xD1\x61\xE1\x8F\xA6\x99\x34\xDA\xC4\xB5\x9B\xE2\x75\x77\x70\xE6\x71\xAD"
            "\xE6\x34\xDB\x81\x09\x6A\xDB\x66\xC4\x1C\x01\x02\x97\x90\x9B\xE8\x73\x9D\xAF\x6E\x45\x28\xD6\x18\xB5\x81"
            "\x35\xF2\xE6\x90\x03\x83\xF7\x0D\x19\xFB\x82\x7D\x4D\xDE\x23\x0F\x5F\x20\x74\x68\x37\xFE\xEE\x22\x99\x01"
            "\x01\xB7\x94\x10\x65\x6E\xD4\xF6\x46\x95\xC7\xC4\x17\x04\x81\x42\x4C\xE2\x90\xD3\x92\x85\x81\x24\xB6\x83"
            "\xF5\x7F\x5D\x1F\x4B\xCF\x81\xF9\x05\x68\xC7\xD7\x1A\xE7\x5A\xD9\x4E\x2B\x51\x3D\x97\x55\xD1\xFD\x36\x1C"
            "\x1F\xA6\x29\x59\x09\x8F\xE5\x7F\x7B\x71\xB0\xF2\x98\x89\xB8\x97\x11\x29\x67\xA6\x1B\xD3\x39\x20\x35\x4E"
            "\xB6\xEF\x40\xB8\x6C\x2E\x5F\x6E\xBB\xD7\xCE\x18\xE3\x96\xC9\x52\xD6\x6B\x5C\x5F\x29\x5F\x93\x3A\x53\x3E"
            "\xF0\x0A\xC6\xD9\xD5\xC2\x50\xEE\x43\xD3\x3C\xBC\x08\xCD\x36\x76\xBE\x26\x3C\x22\xE2\x4E\x53\xBD\x8A\xAF"
            "\x2F\xCA\xCC\x03\x53\xF9\x87\xD0\xCB\xD4\x4F\xEA\x41\xC6\x17\x6F\xA6\xF0\x7E\x3E\xA8\x11\x98\xC0\x17\x34"
            "\x54\xF4\x13\x63\x45\xEC\x88\xD5\x6E\xB8\x07\xD7\xAA\x72\xF4\xB3\x6F\x59\x51\xDC\x22\x9B\xEA\x65\x72\x65"
            "\x14\xEF\x1C\x2F\x07\xED\xCF\xED\xDD\xA6\x5E\xEF\x19\xAF\x0D\x74\x6B\xAB\x04\xB0\x51\xA3\x05\x54\x37\x7F"
            "\x04\xD4\x53\xDC\x9B\x4C\x40\xB6\x3F\xE8\x2D\x54\x27\x12\x72\xFF\x23\x78\x84\xD6\xD5\x0B\x9D\x45\x8B\xEC"
            "\x8F\xF8\xE9\x9F\x9D\x51\x8C\x42\x47\x38\xEE\x53\xAE\x9D\x04\xAC\xDA\x3F\x0C\xDE\x48\x8C\x8C\xAA\xBB\xE1"
            "\x70\x73\x5F\x57\xDB\xA7\x21\xAD\x79\x13\x82\xA6\xCE\xB9\xF0\x2A\x4F\xB8\x48\x01\x01\xBE\x1F\x5E\x49\x23"
            "\xC9\x0A\x89\xC7\xF3\x63\x1A\xAC\x75\xF1\x14\xCA\x5C\x62\xED\x9F\xA4\xAA\x44\xE5\xC0\x8C\x7C\x59\x5A\xB1"
            "\x3B\x9B\xE0\x14\x48\xDE\xB1\x96\x1E\x44\x7E\xF9\xF2\xB2\x20\x14\x0C\x81\xF2\xEA\xB5\x6A\x55\xC9\xE2\x2B"
            "\x3C\xC8\x2A\x33\x98\xA9\xF2\xEB\x59\xDF\x29\x14\x6F\xB0\x39\x43\xEF\xE1\xC4\x66\x07\xCD\xE8\xD0\x9D\xE8"
            "\xDE\x12\xDD\x34\x73\xDD\xDF\x36\x85\x7B\xAD\x69\x1B\xE3\xEC\x28\xAD\x3C\xC0\x88\x06\x04\x4A\xCB\xB5\xEA"
            "\x1B\x81\xBA\xC1\x1C\x9D\x5D\xD7\x53\xD7\x62\x34\x0E\xB3\xFB\x32\xF9\xC8\xCB\xFB\x8F\x9B\x10\x96\xFD\xAD"
            "\x21\x2A\x90\x83\x74\x22\xBA\xB1\x95\x70\xD5\x7C\xB2\x13\xCB\xCC\x9C\x56\x71\xCC\x92\xDB\x6C\x5F\x18\x0F"
            "\x1D\x9C\xC3\x15\x75\x01\x01\xD4\x42\x64\xD7\x6D\xB2\xF1\x64\xC7\xF1\x12\xCB\xA6\x5F\x7A"
    );
    EA_MESSAGE_GET = std::string(
            "\x17\x7e\x7c\x0e\x1a\x15\x41\x03\xae\xd5\xa0\xf5\x2c\xa1\xb9\xd2\x53\xc5\xce\x2a\x53\x40\xbd\xed\xa9\x28"
            "\xd4\x04\x0c\x10\x6f\x69\xf1\x38\x2d\xb8\xa2\x7a\xff\x8d\x0a\xbb\x5f\x09\x1c\xb1\x81\x65");
    EA_MESSAGE_GET_MAINTENANCE = std::string(
            "\x17\x7E\xFC\x8E\x1A\x15\x41\x4F\xAE\xD5\xA0\xF5\x2C\xA1\xB9\xD2\x53\xC5\xCE\x2A\x53\x40\xBD\xED\xA9\x28"
            "\x9C\x40\xDB\x25\x4F\xB9\x09\x24\xB4\x01\x01\x49\xFA\xFE\x81\xB1\x21\xED\x25\x2F\x1A\xBB\x25\x1D\xD1\xB4"
            "\x9A\x05\xE4\x14\xB9\x8E\xE3\xE1\xA8\xF8\x57\x68\xA4\x1F\x58\x94\x06\x9F\x67\x22\xAC\x69\x2E\xC8\xCE\xD6"
            "\x43\xBC\xC3\xAC\x73\x2B\x52\xD1\xDA\x88\x0F\x68\x2F\x67\xD1\x42\x08\x24\x18\x19\x6D\x99\x3F\xC6\x41\x24"
            "\x25\xC4\x5B\x6C\x61\x6E\x7D\xF6\xD2\x75\xB7\x37\xE7\x39\x2A\xEE\xF1\xB4\x2D\xB4\x22\x5E\x55\x06\xBB\xFA"
            "\xB4\x86\xD7\x66\xB8\xA8\x9F\x4E\x06\x26\xDF\x9B\xF3\x7D\x0B\x76\x9C\x37\x3E\xD1\x2A\xC6\xA4\x40\xAA\x52"
            "\x3D\x39\x6D\x70\xFF\xDF\xED\x40\x5A\xA2\x7D\xEA\x84\x6D\xE3\xE6\x75\x8D\x36\xD3\x69\xB2\xDC\x1E\xDD\x82"
            "\x23\xD5\x73\xF1\x18\x60\xF7"
    );
    EA_FACILITY_GET = std::string(
            "\x17\x7e\x7c\x0e\x1a\x15\x40\x17\xae\xd5\xa0\xf5\x2c\xa1\xb9\xd2\x53\xca\xaa\xee\x45\x1c\x99\x13\x86\x26"
            "\xf0\xd7\x93\x1d\x02\xa4\x04\xc5\x97\x28\x5c\x71\xf8\x26\x44\x08\xb8\x74\xac\x4f\x8a\x63\xed\x7e"
            "\x54\x25\x9f\x34\x1a\x5e\xd9\x38\xaf\xa8\xfa\x57\x34\xd3\xc7\x88\x69\xac\xa1\xe1\xe0\x42\xac\x40"
            "\xd8\x70\x2d\xa3\x20\xd7\x51\x35\x35\x52\xd0\xdb\x8e\xd8\x25\xd6\xc8\x71\x4e\x01\x02\x97\x01\x01"
            "\x0d\xc6\xb2\x8a\x38\x44\x2e\x94\xe9\x7c\xf7\x7c\x1a\xa2\x66\x2a\x48\x8a\xd4\xac\xb2\xb1\x74\x44"
            "\x67\x53\xf4\xd7\xd3\x2d\x73\x42\x3e\x24\x55\x39\x11\xd9\x07\x86\xe2\xf8\x2d\xd9\x65\x9f\xd7\xf5"
            "\x7d\x94\xf1\x56\x87\xfb\x58\xfe\x8e\xa1\x5b\xfa\x75\x81\xcb\x73\x05\x3c\xcd\x8a\xc6\x19\x6b\x32"
            "\xb3\x25\xc9\x1e\x46\x35\x89\xa6\xe7\xda\x15\x12\xcc\x4b\x63\x1d\x75\xf3\x5e\xb7\x30\x0c\xcc\x2b"
            "\x35\x06\x38\x41\x97\xa7\x25\x64\x1b\x47\xa6\xc5\x17\x1b\xe8\x99\x2b\x5f\x75\x3d\xb8\x20\x8e\xcb"
            "\x6b\x7a\x46\xb3\x85\x29\x31\x3c\xe7\x92\x4b\xa0\xc7\xfa\x4e\x10\xc0\x4d\xa2\x0e\x60\x5c\x09\xcd"
            "\x53\xfe\x31\xbd\xec\xee\xb8\x92\xab\xec\xc5\xbd\x04\x6f\x3a\x7e\x0e\x16\xae\x5e\xc3\xc3\x2d\x0d"
            "\xd5\x80\x5e\x3b\xda\xd5\xb3\xae\xec\xe8\x99\x66\x0b\xb7\x55\xe9\xd5\x39\x03\x3d\xd3\x6d\x89\x2f"
            "\x30\x2e\x0f\x3a\x35\x31\x98\x55\x65\x03\x9c\xfc\xad\xbe\x79\x21\x19\x01\x01\x18\x03\x20\x35\x2b"
            "\x58\xe3\xaf\xd9\x89\x7b\x78\xc9\x2a\x19\xbe\x3b\xb5\x09\xf9\xd6\xb9\x68\x8b\x36\xa3\xc7\x39\xd7"
            "\x3e\x0d\x14\x39\x9b\x58\x2f\xd6\x45\xfc\xe3\x71\x5d\xed\x85\x88\xa0\x64\x49\x32\x5c\xe6\x1c\xdd"
            "\xcf\x65\x60\x47\x74\x82\x7f\xee\x91\xdd\xd0\x9e\x3d\xfa\xb3\xca\xcd\x50\x06\x2e\x2a\xdc\xd8\x75"
            "\x9a\x29\x97\x94\xbf\x04\xe4\x02\x97\x8c\x2c\xf1\x31\xd0\xcd\x2b\xf8\xea\x2f\xd8\x12\xcf\xcf\x04"
            "\x71\x39\xf6\x2b\xf8\x76\xd2\xf2\x89\xce\x1b\x9b\x8f\xe2\x82\x17\x07\x84\xba\x10\x0e\xee\x79\xe0"
            "\x5b\x99\xa1\x15\x78\x16\x84\xe0\x67\x2c\x75\xf7\xc0\x38\xd5\x20\xf7\x81\xae\x1f\x77\x02\xdf\x0c"
            "\x60\x68\x2e\x26\xe5\xbb\xb7\xb4\x41\x72\xc4\xa4\x43\x2a\xc8\x81\xd3\x8a\x1f\xe8\x94\x39\x7e\xf6"
            "\x69\x56\x9e\xaf\x06\x10\xfd\x47\x69\xb8\x14\xe6\x48\x36\xd6\x25\x18\x0c\x2b\x3d\x69");
    EA_PCBEVENT_PUT = std::string(
            "\x17\x7e\x7c\x0e\x1a\x15\x41\x0b\xae\xd5\xa0\xf5\x2c\xa1\xb9\xd2\x53\xca\xd2\x0d\x81\x34\xdb\x94\x79\xd0"
            "\xc8\x9d\xb5\xfb\xef\x97");
    EA_PACKAGE_LIST = std::string(
            "\xA0\x42\x80\x7F\x01\x02\x01\x02\x01\x02\x24\x01\x01\x08\xDE\xAE\x35\xD3\x3E\x2A\x01\x01\x07\xD6\x6A\x30"
            "\x9A\xCA\x80\x2E\x06\xAB\xDD\x6E\xDE\xA0\x2E\x06\xE3\x99\xB9\xEB\x80\xFE\xFE\xFF\x01\x02\x01\x02\x01\x02"
            "\x01\x02\x01\x02\x01\x02\x14\x01\x02\x01\x02\x01\x02\x05\x31\x32\x30\x30\x01\x02\x01\x02\x01\x02\x01\x02"
            "\x01\x02\x01\x02\x01\x02\x02\x30\x01\x02\x01\x02\x01\x02"
    );
    EA_TAX_GET_PHASE = std::string(
            "\x17\x7E\xFC\x8E\x1A\x15\x41\x07\xAE\xD5\xA0\xF5\x2C\xA1\xB9\xD2\x53\xC1\xE2\xEB\x2B\xDC\x72\xBB\x5E\x96"
            "\x9F\x63\x4B\x05\x10\x97\x0F\xC7\x2D\xBC\xA2\x7A\xFF\x85"
    );
    EA_EVENTLOG_WRITE = std::string(
            "\x17\x7E\xFC\x8E\x1A\x15\x41\x5B\xAE\xD5\xA0\xF5\x2C\xA1\xB9\xD2\x53\xCA\xAF\x3E\xD8\x3D\x6A\x41\x8F\x25"
            "\x85\xF1\x1F\x19\x41\xAF\xB4\x8B\xED\x46\xA4\x70\x38\xCE\x32\x10\x65\x60\xEA\x71\x7F\x63\x38\x12\x31\xDC"
            "\x70\xB6\x7B\xB4\xBB\x14\xFB\x50\xF7\xF8\x6D\x50\x0A\x4B\xAF\x06\x9F\x4C\xCA\x68\x59\xBC\x9C\x1F\xD3\xA0"
            "\x25\x60\x48\x8D\xD5\xAC\x2E\xDA\x88\x0E\x68\x2F\x67\xB1\x42\x08\x24\x1C\x2A\x5D\xA9\x3F"
    );
    EA_MACHINE_GET_CONTROL = std::string(
            "\x17\x7E\xFC\x8E\x1A\x15\x41\x3B\xAE\xD5\xA0\xF5\x2C\xA1\xB9\xD2\x53\xC5\xCE\xEE\x46\x61\x4D\xED\x86\x29"
            "\x94\xD1\x07\x60\xD5\xD7\x04\xC4\xB6\xC3\xA2\x84\x01\x01\x7B\xF4\x44\x5F\x0B\x2C\xB1\x81\x6D\x33\xD5\x7A"
            "\xF2\xC1\xA5\x61\x5A"
    );
    EA_KGG_SYSTEM_GETMASTER = std::string(
            "\x17\x7E\xFC\x8E\x1A\x15\x41\x27\xAE\xD5\xA0\xF5\x2C\xA1\xB9\xD2\x53\xC4\xE7\x6A\x52\x71\x57\x6B\x81\xF0"
            "\x99\xA7\x72\x6B\x11\x9C\x07\x24\xB0\x51\x39\xE3\x7E\x7B\x01\x01\xB3\xBC\x96\xC5\x2A\x18\xE7\xCD\xDC\x70"
            "\x1D\xF5\xAC\xF7\xF0\x71\xB2\xA5\xA8\x07\xAD\x2C\x3E\xAD\x76\x97\xC9\xA5\x27\x0C\xA9\xA7\x42\x62\xC5\x9E"
            "\xF3\x52\x0C\x04\xC9\x90\xDF\x63\x89\xFF\x77\x24\x6B\x22\xC2\x0F\x5B\x53\x64\x66\x19\xEC\x4C\x8B\x12\x53"
            "\x5F\xB8\x1F\x29\x12\x23\x2E\x81\xAC\x4D\x81\x03\xD7\x09\x2A\xEE\xD4\xF9\x7E\xC3\x51\x61\x68\x30\xE6\xDA"
            "\x86\x98\xC1\x5E\x99\xED\xEC\x03\x55\x51\xA5\xE7\xB7\x38\x78\x3B\xCF\x40\x40\xA5\x58\xB7\xE7\x3D\xF9\x25"
            "\x45\x39\x6D\x70\xED\xAC\x94\x33\x74\x85\xDB\xFB\x9C");
    EA_I36_SYSTEM_GETMASTER = std::string(
            "\x17\x7E\xFC\x8E\x1A\x15\x41\x27\xAE\xD5\xA0\xF5\x2C\xA1\xB9\xD2\x53\xC4\xE7\x6A\x52\x71\x57\x6B\x81\xF0"
            "\x99\xA7\x72\x6B\x11\x9C\x07\x24\xB0\x51\x39\xE3\x7E\x7B\x01\x01\xB3\xBC\x96\xC5\x2A\x18\xE7\xCD\xDC\x70"
            "\x1D\xF5\xAC\xF7\xF0\x71\xB2\xA5\xA8\x07\xAD\x2C\x3E\xAD\x76\x97\x65\xA5\x27\x0C\xA9\xA7\x42\x62\x69\x9E"
            "\xCA\x64\x0C\x05\xD9\x94\x98\x63\x8E\xC9\x78\x25\x6B\x08\xC9\x0D\x62\x61\x2A\x67\x09\xC6\x47\x89\x2B\x61"
            "\x11\xB9\x0F\x03\x19\x21\x17\xB3\xE2\x01\x02\xD5\x6C\xAF\x46\x40\xAB\xC7\xF9\x79\xDB\x51\x62\x46\x30\xA3"
            "\xDA\x81\x80\xC1\x5D\xB7\xED\xA9\x03\x52\x49\xA5\xE4\x99\x38\x3D\x3B\xC8\x58\x40\xA6\x76\xB7\xA2\x3D\xFE"
            "\x3D\x45\x76\x07\x35\xDB\xE1\xC0\x5C\x0C\x88\x76\xCC\xDD\x49\xD9\xA7\x60\xA3\x35\xF8\x2B\x9A\x88\x71\xA5"
            "\xCD\x49\x90\x47\x8C\x4C\x0F\x8F\x81\x60\x83\xD2\x36\x3A\xE1\xB4\xE2\x02\xF0\xAC\x50\x18\x2A\x10\x2D\xB6"
            "\x41\xC1\xAC\xE0\x08\xFE\x46\x21\x8D\x20\x17\x3A\x4D\x90\x62\x17\x20\xA7\x2B\x28\x05\xDA\x11\xD3\x1F\xCE"
            "\x26\xEF\x2B\x83\x26\x04\xBD\xF9\xF9\x70\x98\x24\x47\x22\x8F\x14\xEC\xA0\x43\x82\xAF\xF6\xA6\xAF\xF0\x1D"
            "\xA8\xF4\xA8\xE5\xCB\xAD\x69\xB9\xD2\xB2"
    );
    EA_KGG_HDKOPERATION_GET = std::string(
            "\x17\x7E\xFC\x8E\x1A\x15\x41\x37\xAE\xD5\xA0\xF5\x2C\xA1\xB9\xD2\x53\xCE\xB2\x18\x5F\x0C\xDA\x8B\x61\xC3"
            "\x04\x9B\xBD\x34\x96\xFD\xC0\x5A\xD3\x46\xA9\x7F\x29\xE8\xEC\x73\xA1\xF5\xD2\x4E\x81\x65\x33\xD5\x7A\xEA"
            "\xAF\xCA\x11\x5B\x17\x54\x05\x44\xC9\x7F\xE3\x12\x9D\x5A\xA7\x81\x95\x0B\x3C\x84\x97\x6E\x52\xCC\xE3\xA0"
            "\x25\x74"
    );
}
