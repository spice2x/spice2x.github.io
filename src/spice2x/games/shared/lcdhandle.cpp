#include "lcdhandle.h"
#include "util/utils.h"
#include "util/time.h"

// globals
namespace games::shared {

    // current state for easy access
    bool LCD_ENABLED = false;
    std::string LCD_CSM = "USER";
    uint8_t LCD_BRI = 27;
    uint8_t LCD_CON = 48;
    uint8_t LCD_BL = 100;
    uint8_t LCD_RED = 137;
    uint8_t LCD_GREEN = 132;
    uint8_t LCD_BLUE = 132;
}

void games::shared::LCDHandle::answer(std::string s) {
    for (auto c : s) {
        this->read_buffer.push_back((uint8_t) c);
    }
    this->read_buffer.push_back('\r');
    this->read_buffer.push_back('\n');
}

bool games::shared::LCDHandle::open(LPCWSTR lpFileName) {
    if (wcscmp(lpFileName, L"COM1")) {
        return false;
    }
    log_info("lcdhandle", "opened COM1");
    LCD_ENABLED = true;
    return true;
}

int games::shared::LCDHandle::read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) {

    // check time
    if (get_system_milliseconds() < this->read_time_next) {
        return 0;
    }

    // return buffer
    if (read_buffer.size()) {
        size_t write_count = MIN(nNumberOfBytesToRead, read_buffer.size());
        memcpy(lpBuffer, &read_buffer[0], write_count);
        read_buffer.erase(read_buffer.begin(), read_buffer.begin() + write_count);
        return write_count;
    }

    // no data
    return 0;
}

int games::shared::LCDHandle::write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {

    // check maximum size
    if (nNumberOfBytesToWrite >= 256)
        nNumberOfBytesToWrite = 255;

    // get string
    char data[256]{};
    memcpy(data, lpBuffer, nNumberOfBytesToWrite);
    std::string cmd_frame(data);

    // check frame
    if (string_begins_with(cmd_frame, "0")) {

        // process frame
        std::string cmd = cmd_frame.substr(1);
        std::vector<std::string> cmd_split;
        strsplit(cmd, cmd_split, ' ');
        std::string param_in = "";
        if (cmd_split.size() > 1) {
            param_in = cmd_split[1];
            if (string_ends_with(param_in.c_str(), "\r\n")) {
                param_in = param_in.substr(0, param_in.size() - 2);
            }
            if (cmd_split.size() > 2) {
                log_warning("lcdhandle", "too many parameters: {}", cmd_frame);
            }
        }

        // get parameter
        std::string param_out = "";
        try {
            if (string_begins_with(cmd_split[0], "MODEL?")) {
                //param_out = "SPICE";
            } else if (string_begins_with(cmd_split[0], "CSM")) {
                LCD_CSM = param_in;
            } else if (string_begins_with(cmd_split[0], "BRI")) {
                LCD_BRI = std::stoi(param_in);
            } else if (string_begins_with(cmd_split[0], "CON")) {
                LCD_CON = std::stoi(param_in);
            } else if (string_begins_with(cmd_split[0], "BL")) {
                LCD_BL = std::stoi(param_in);
            } else if (string_begins_with(cmd_split[0], "RED")) {
                LCD_RED = std::stoi(param_in);
            } else if (string_begins_with(cmd_split[0], "GREEN")) {
                LCD_GREEN = std::stoi(param_in);
            } else if (string_begins_with(cmd_split[0], "BLUE")) {
                LCD_BLUE = std::stoi(param_in);
            } else if (string_begins_with(cmd_split[0], "DFLIP")) {
                // TODO
            } else if (string_begins_with(cmd_split[0], "OFLIP")) {
                // TODO
            } else {
                log_warning("lcdhandle", "unknown cmd: {}", cmd_frame);
            }
        } catch (std::invalid_argument&) {
            log_warning("lcdhandle", "couldn't parse cmd: {}", cmd_frame);
        }

        // respond
        answer(fmt::format("9{} {}", cmd_split[0], param_in));
        answer(fmt::format("9OK {}", param_out));

        // delay next read by 32ms
        read_time_next = get_system_milliseconds() + 32;
    }

    // log unhandled commands
    if (this->read_buffer.empty()) {
        log_warning("lcdhandle", "unhandled cmd: {}", cmd_frame);
    }

    // return all bytes written
    return (int) nNumberOfBytesToWrite;
}

int games::shared::LCDHandle::device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
        LPVOID lpOutBuffer, DWORD nOutBufferSize) {
    return -1;
}

bool games::shared::LCDHandle::close() {
    log_info("lcdhandle", "closed COM1");
    LCD_ENABLED = false;
    return true;
}
