#ifndef SPICEAPI_CONNECTION_H
#define SPICEAPI_CONNECTION_H

#include <stdint.h>
#include "rc4.h"

#ifndef SPICEAPI_INTERFACE
#define SPICEAPI_INTERFACE Serial
#endif

namespace spiceapi {

    class Connection {
    private:
        uint8_t* receive_buffer;
        size_t receive_buffer_size;
        const char* password;
        RC4* cipher;

    public:
        Connection(size_t receive_buffer_size, const char* password = "");
        ~Connection();

        void reset();

        bool check();
        void cipher_alloc(const char *session_key = nullptr);
        void change_pass(const char* password, bool session = false);
        const char* request(const char* json, size_t timeout = 1000);
        const char* request(char* json, size_t timeout = 1000);
    };
}

spiceapi::Connection::Connection(size_t receive_buffer_size, const char* password) {
    this->receive_buffer = new uint8_t[receive_buffer_size];
    this->receive_buffer_size = receive_buffer_size;
    this->password = password;
    this->cipher = nullptr;
    this->reset();
}

spiceapi::Connection::~Connection() {

    // clean up
    if (this->cipher != nullptr)
        delete this->cipher;
}

void spiceapi::Connection::reset() {

    // drop all input
    while (SPICEAPI_INTERFACE.available()) {
        SPICEAPI_INTERFACE.read();
    }

#ifdef SPICEAPI_INTERFACE_WIFICLIENT
    // reconnect TCP client
    SPICEAPI_INTERFACE.stop();
    this->check();
#else
    // 8 zeroes reset the password/session on serial
    for (size_t i = 0; i < 8; i++) {
        SPICEAPI_INTERFACE.write((int) 0);
    }
#endif

    // reset password
    this->cipher_alloc();
}

void spiceapi::Connection::cipher_alloc(const char *session_key) {

    // delete old cipher
    if (this->cipher != nullptr) {
        delete this->cipher;
        this->cipher = nullptr;
    }

    // create new cipher if password is set
    session_key = session_key ? session_key : this->password;
    if (strlen(session_key) > 0) {
        this->cipher = new RC4(
                (uint8_t *) session_key,
                strlen(session_key));
    }
}

bool spiceapi::Connection::check() {
#ifdef SPICEAPI_INTERFACE_WIFICLIENT
    if (!SPICEAPI_INTERFACE.connected()) {
        return SPICEAPI_INTERFACE.connect(
            SPICEAPI_INTERFACE_WIFICLIENT_HOST,
            SPICEAPI_INTERFACE_WIFICLIENT_PORT);
    } else {
        return true;
    }
#else
    // serial is always valid
    return true;
#endif
}

void spiceapi::Connection::change_pass(const char* password, bool session) {
    if (!session) {
        this->password = password;
    }
    this->cipher_alloc(password);
}

const char* spiceapi::Connection::request(const char* json, size_t timeout) {
    auto json_len = strlen(json);
    strncpy((char*) receive_buffer, json, receive_buffer_size);
    return request((char*) receive_buffer, timeout);
}

const char* spiceapi::Connection::request(char* json_data, size_t timeout) {

    // check connection
    if (!this->check())
        return "";

    // crypt
    auto json_len = strlen(json_data) + 1;
    if (this->cipher != nullptr)
        this->cipher->crypt((uint8_t*) json_data, json_len);

    // send
    auto send_result = SPICEAPI_INTERFACE.write((const char*) json_data, (int) json_len);
    SPICEAPI_INTERFACE.flush();
    if (send_result < (int) json_len) {
        return "";
    }

    // receive
    size_t receive_data_len = 0;
    auto t_start = millis();
    while (SPICEAPI_INTERFACE) {

        // check for timeout
        if (millis() - t_start > timeout) {
            this->reset();
            return "";
        }

        // read single byte
        auto b = SPICEAPI_INTERFACE.read();
        if (b < 0) continue;
        receive_buffer[receive_data_len++] = b;

        // check for buffer overflow
        if (receive_data_len >= receive_buffer_size) {
            this->reset();
            return "";
        }

        // crypt
        if (this->cipher != nullptr)
            this->cipher->crypt(&receive_buffer[receive_data_len - 1], 1);

        // check for message end
        if (receive_buffer[receive_data_len - 1] == 0)
            break;
    }

    // return resulting json
    return (const char*) &receive_buffer[0];
}

#endif //SPICEAPI_CONNECTION_H
