#include "controller.h"
#include "serial.h"

#include <string>
#include <utility>

#include "util/logging.h"
#include "util/utils.h"


namespace api {

    SerialController::SerialController(Controller *controller, std::string port, DWORD baud)
    : controller(controller), port(std::move(port)), baud(baud) {
        this->state = new ClientState();
        controller->init_state(this->state);
        this->thread = new std::thread([this] () {
            log_warning("api::serial", "listening on {} (baud: {})", this->port, this->baud);

            // read buffer
            uint8_t read_buffer[16*1024];
            size_t read_buffer_len = 0;

            // serial retry loop
            while (this->running) {

                // try to open port
                if (this->handle == INVALID_HANDLE_VALUE) {
                    this->open_port();
                }

                // reset in-buffer
                read_buffer_len = 0;

                // connection loop
                DWORD retry_time = 1000;
                while (this->handle != INVALID_HANDLE_VALUE) {
                    retry_time = 1000;
                    DWORD bytes_read = 0;

                    // check if we need to wait for incoming data first
                    bool messages_in_buffer = false;
                    for (size_t i = 0; i < read_buffer_len; i++) {
                        if (read_buffer[i] == 0x00) {
                            messages_in_buffer = true;
                            break;
                        }
                    }

                    // read data
                    if (!messages_in_buffer && (!ReadFile(
                            this->handle,
                            &read_buffer[read_buffer_len],
                            sizeof(read_buffer) - read_buffer_len, &bytes_read,
                            nullptr) || bytes_read == 0)) {

                        // open new connection
                        log_warning("api::serial", "read error on {}", this->port);
                        this->free_port();
                        break;

                    } else {
                        //log_info("api::serial::in", "{}", bin2hex(read_buffer + read_buffer_len, bytes_read));

                        // check for reset
                        if (read_buffer_len + bytes_read > 7) {
                            size_t zero_counter = 0;
                            for (size_t i = 0; i < read_buffer_len + bytes_read; i++) {
                                if (read_buffer[i] == 0x00) {
                                    if (++zero_counter == 8) {

                                        // reset password
                                        state->password = this->controller->get_password();
                                        state->password_change = true;
                                        Controller::process_password_change(state);

                                        // drop input
                                        size_t new_length = 0;
                                        while (++i < read_buffer_len + bytes_read) {
                                            read_buffer[new_length++] = read_buffer[i];
                                        }
                                        bytes_read = new_length;
                                        read_buffer_len = 0;
                                        log_info("api::serial", "session reset, remaining bytes: {} {}", bytes_read, bin2hex(read_buffer, bytes_read));
                                        break;
                                    }
                                } else {
                                    zero_counter = 0;
                                }
                            }
                        }

                        // crypt in-data
                        if (state->cipher) {
                            state->cipher->crypt(&read_buffer[read_buffer_len], bytes_read);
                        }

                        // adjust size
                        read_buffer_len += bytes_read;

                        // check if message complete
                        for (size_t i = 0; i < read_buffer_len; ++i) {
                            if (read_buffer[i] != 0x00) {
                                continue;
                            } else {

                                // process request
                                std::vector<char> out;
                                if (!this->controller->process_request(
                                        state,
                                        (const char*) &read_buffer[0], read_buffer_len, &out)) {

                                    // open new connection
                                    log_warning("api::serial", "process error on {} (length {})",
                                                this->port, i - 1);
                                    this->free_port();
                                    retry_time = 5;
                                    break;
                                }

                                // adjust in-buffer
                                if (i == read_buffer_len - 1) {
                                    read_buffer_len = 0;
                                } else {
                                    size_t new_length = 0;
                                    while (i < read_buffer_len) {
                                        read_buffer[new_length++] = read_buffer[i++];
                                    }
                                    read_buffer_len = new_length;
                                }

                                // crypt out-data
                                if (state->cipher) {
                                    state->cipher->crypt((uint8_t*) out.data(), out.size());
                                }

                                // send answer
                                DWORD bytes_written = 0;
                                if (!WriteFile(
                                        this->handle,
                                        out.data(),
                                        out.size(),
                                        &bytes_written,
                                        nullptr) || bytes_written != out.size()) {

                                    // open new connection
                                    log_warning("api::serial", "write error on {}", this->port);
                                    this->free_port();
                                    break;
                                }

                                // check for password change
                                Controller::process_password_change(state);
                                //log_info("api::serial::out", "{}", bin2hex(out));
                                read_buffer_len = 0;
                            }
                        }
                    }
                }

                // slow down on reconnect
                if (this->running) {
                    Sleep(retry_time);
                }
            }
        });
    }

    SerialController::~SerialController() {
        this->free_port();
        this->running = false;
        this->thread->join();
        delete this->thread;
        delete this->state;
    }

    void SerialController::open_port() {

        // free resources
        this->free_port();

        // open port
        this->handle = CreateFile(
                this->port.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr);

        // check if open failed
        if (this->handle == INVALID_HANDLE_VALUE) {
            log_warning("api::serial", "failed to open port {}", port);
            return;
        }

        // settings
        DCB serial_params{};
        serial_params.DCBlength = sizeof(serial_params);
        if (!GetCommState(this->handle, &serial_params)) {
            log_warning("api::serial", "{}: unable to get COM port state: 0x{:x}", port, GetLastError());
            return;
        }

        // set params
        serial_params.BaudRate = this->baud;
        serial_params.ByteSize = 8;
        serial_params.StopBits = ONESTOPBIT;
        serial_params.Parity = NOPARITY;
        if (!SetCommState(this->handle, &serial_params)) {
            log_warning("api::serial", "{}: unable to set COM port state: 0x{:x}", port, GetLastError());
            return;
        }

        // timeouts
        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = 5;
        timeouts.ReadTotalTimeoutConstant = 0;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 30;
        timeouts.WriteTotalTimeoutMultiplier = 5;
        if (!SetCommTimeouts(this->handle, &timeouts)) {
            log_warning("api::serial", "{}: unable to set COM port timeouts: 0x{:x}", port, GetLastError());
            return;
        }

        // reset password
        state->password_change = true;
        Controller::process_password_change(state);
        log_info("api::serial", "listening on {}/{}", port, baud);
    }

    void SerialController::free_port() {
        if (this->handle != INVALID_HANDLE_VALUE) {
            CloseHandle(this->handle);
            this->handle = INVALID_HANDLE_VALUE;
        }
    }
}
