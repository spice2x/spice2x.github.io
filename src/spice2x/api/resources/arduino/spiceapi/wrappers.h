#ifndef SPICEAPI_WRAPPERS_H
#define SPICEAPI_WRAPPERS_H

#define ARDUINOJSON_USE_LONG_LONG 1
#include "ArduinoJson.h"

#include <Arduino.h>
#include "connection.h"

// default buffer sizes
#ifndef SPICEAPI_WRAPPER_BUFFER_SIZE
#define SPICEAPI_WRAPPER_BUFFER_SIZE 256
#endif
#ifndef SPICEAPI_WRAPPER_BUFFER_SIZE_STR
#define SPICEAPI_WRAPPER_BUFFER_SIZE_STR 256
#endif

namespace spiceapi {
    
     /*
      * Structs
      */

    struct AnalogState {
        String name = "";
        float value = 0.f;
        bool enabled = false;
    };

    struct ButtonState {
        String name = "";
        float value = 0.f;
        bool enabled = false;
    };

    struct LightState {
        String name = "";
        float value = 0.f;
        bool enabled = false;
    };

    struct InfoAvs {
        String model, dest, spec, rev, ext;
    };

    struct InfoLauncher {
        String version;
        String compile_date, compile_time, system_time;
        String args;
    };

    struct InfoMemory {
        uint64_t mem_total, mem_total_used, mem_used;
        uint64_t vmem_total, vmem_total_used, vmem_used;
    };

    struct TouchState {
        uint64_t id;
        int64_t x, y;
    };
    
    // static storage
    char JSON_BUFFER_STR[SPICEAPI_WRAPPER_BUFFER_SIZE_STR];
    
     /*
      * Helpers
      */
    
    uint64_t msg_gen_id() {
        static uint64_t id_global = 0;
        return ++id_global;
    }

    char *doc2str(DynamicJsonDocument *doc) {
        char *buf = JSON_BUFFER_STR;
        serializeJson(*doc, buf, SPICEAPI_WRAPPER_BUFFER_SIZE_STR);
        return buf;
    }

    DynamicJsonDocument *request_gen(const char *module, const char *function) {

        // create document
        auto doc = new DynamicJsonDocument(SPICEAPI_WRAPPER_BUFFER_SIZE);

        // add attributes
        (*doc)["id"] = msg_gen_id();
        (*doc)["module"] = module;
        (*doc)["function"] = function;

        // add params
        (*doc).createNestedArray("params");

        // return document
        return doc;
    }

    DynamicJsonDocument *response_get(Connection &con, const char *json) {

        // parse document
        DynamicJsonDocument *doc = new DynamicJsonDocument(SPICEAPI_WRAPPER_BUFFER_SIZE);
        auto err = deserializeJson(*doc, (char *) json);

        // check for parse error
        if (err) {
            
            // reset cipher
            con.cipher_alloc();
            delete doc;
            return nullptr;
        }

        // check id
        if (!(*doc)["id"].is<int64_t>()) {
            delete doc;
            return nullptr;
        }

        // check errors
        auto errors = (*doc)["errors"];
        if (!errors.is<JsonArray>()) {
            delete doc;
            return nullptr;
        }

        // check error count
        if (errors.as<JsonArray>().size() > 0) {
            delete doc;
            return nullptr;
        }

        // check data
        if (!(*doc)["data"].is<JsonArray>()) {
            delete doc;
            return nullptr;
        }

        // return document
        return doc;
    }
    
    /*
     * Wrappers
     */

    size_t analogs_read(Connection &con, AnalogState *buffer, size_t buffer_elements) {
        auto req = request_gen("analogs", "read");
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return 0;
        
        auto data = (*res)["data"].as<JsonArray>();
        size_t buffer_count = 0;
        for (auto val : data) {
            if (buffer_count >= buffer_elements) {
                delete res;
                return buffer_count;
            }
            buffer[buffer_count].name = (const char*) val[0];
            buffer[buffer_count].value = val[1];
            buffer[buffer_count].enabled = val[2];
            buffer_count++;
        }
        delete res;
        return buffer_count;
    }

    bool analogs_write(Connection &con, AnalogState *buffer, size_t buffer_elements) {
        auto req = request_gen("analogs", "write");
        auto params = (*req)["params"].as<JsonArray>();
        for (size_t i = 0; i < buffer_elements; i++) {
            auto &state = buffer[i];
            auto data = params.createNestedArray();
            data.add(state.name);
            data.add(state.value);
        }
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool analogs_write_reset(Connection &con, AnalogState *buffer, size_t buffer_elements) {
        auto req = request_gen("analogs", "write_reset");
        auto params = (*req)["params"].as<JsonArray>();
        for (size_t i = 0; i < buffer_elements; i++) {
            auto &state = buffer[i];
            auto data = params.createNestedArray();
            data.add(state.name);
        }
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    size_t buttons_read(Connection &con, ButtonState *buffer, size_t buffer_elements) {
        auto req = request_gen("buttons", "read");
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return 0;
        
        auto data = (*res)["data"].as<JsonArray>();
        size_t buffer_count = 0;
        for (auto val : data) {
            if (buffer_count >= buffer_elements) {
                delete res;
                return buffer_count;
            }
            buffer[buffer_count].name = (const char*) val[0];
            buffer[buffer_count].value = val[1];
            buffer[buffer_count].enabled = val[2];
            buffer_count++;
        }
        delete res;
        return buffer_count;
    }

    bool buttons_write(Connection &con, ButtonState *buffer, size_t buffer_elements) {
        auto req = request_gen("buttons", "write");
        auto params = (*req)["params"].as<JsonArray>();
        for (size_t i = 0; i < buffer_elements; i++) {
            auto &state = buffer[i];
            auto data = params.createNestedArray();
            data.add(state.name);
            data.add(state.value);
        }
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool buttons_write_reset(Connection &con, ButtonState *buffer, size_t buffer_elements) {
        auto req = request_gen("buttons", "write_reset");
        auto params = (*req)["params"].as<JsonArray>();
        for (size_t i = 0; i < buffer_elements; i++) {
            auto &state = buffer[i];
            auto data = params.createNestedArray();
            data.add(state.name);
        }
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool card_insert(Connection &con, size_t index, const char *card_id) {
        auto req = request_gen("card", "insert");
        auto params = (*req)["params"].as<JsonArray>();
        params.add(index);
        params.add(card_id);
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }
    
    bool coin_get(Connection &con, int &coins) {
        auto req = request_gen("coin", "insert");
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        coins = (*res)["data"][0];
        delete res;
        return true;
    }

    bool coin_set(Connection &con, int coins) {
        auto req = request_gen("coin", "set");
        auto params = (*req)["params"].as<JsonArray>();
        params.add(coins);
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool coin_insert(Connection &con, int coins=1) {
        auto req = request_gen("coin", "insert");
        if (coins != 1) {
            auto params = (*req)["params"].as<JsonArray>();
            params.add(coins);
        }
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool control_raise(Connection &con, const char *signal) {
        auto req = request_gen("control", "raise");
        auto params = (*req)["params"].as<JsonArray>();
        params.add(signal);
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool control_exit(Connection &con) {
        auto req = request_gen("control", "exit");
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool control_exit(Connection &con, int exit_code) {
        auto req = request_gen("control", "exit");
        auto params = (*req)["params"].as<JsonArray>();
        params.add(exit_code);
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool control_restart(Connection &con) {
        auto req = request_gen("control", "restart");
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool control_session_refresh(Connection &con) {
        auto req = request_gen("control", "session_refresh");
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        const char *key = (*res)["data"][0];
        con.change_pass(key, true);
        delete res;
        return true;
    }

    bool control_shutdown(Connection &con) {
        auto req = request_gen("control", "shutdown");
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool control_reboot(Connection &con) {
        auto req = request_gen("control", "reboot");
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool iidx_ticker_get(Connection &con, char *ticker) {
        auto req = request_gen("iidx", "ticker_get");
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        const char *data = (*res)["data"][0];
        strncpy(ticker, data, 9);
        ticker[9] = 0x00;
        delete res;
        return true;
    }

    bool iidx_ticker_set(Connection &con, const char *ticker) {
        auto req = request_gen("iidx", "ticker_set");
        auto params = (*req)["params"].as<JsonArray>();
        params.add(ticker);
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool iidx_ticker_reset(Connection &con) {
        auto req = request_gen("iidx", "ticker_reset");
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool iidx_tapeled_get(Connection &con) {
        auto req = request_gen("iidx", "tapeled_get");
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool info_avs(Connection &con, InfoAvs &info) {
        auto req = request_gen("info", "avs");
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        auto data = (*res)["data"][0];
        info.model = (const char*) data["model"];
        info.dest = (const char*) data["dest"];
        info.spec = (const char*) data["spec"];
        info.rev = (const char*) data["rev"];
        info.ext = (const char*) data["ext"];
        delete res;
        return true;
    }
    
    bool info_launcher(Connection &con, InfoLauncher &info) {
        auto req = request_gen("info", "launcher");
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        auto data = (*res)["data"][0];
        info.version = (const char*) data["version"];
        info.compile_date = (const char*) data["compile_date"];
        info.compile_time = (const char*) data["compile_time"];
        info.system_time = (const char*) data["system_time"];
        for (auto arg : data["args"].as<JsonArray>()) {
            info.args += (const char*) arg;
            info.args += " ";
            // TODO: remove last space
        }
        delete res;
        return true;
    }

    bool info_memory(Connection &con, InfoMemory &info) {
        auto req = request_gen("info", "memory");
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        auto data = (*res)["data"][0];
        info.mem_total = data["mem_total"];
        info.mem_total_used = data["mem_total_used"];
        info.mem_used = data["mem_used"];
        info.vmem_total = data["vmem_total"];
        info.vmem_total_used = data["vmem_total_used"];
        info.vmem_used = data["vmem_used"];
        delete res;
        return true;
    }

    bool keypads_write(Connection &con, unsigned int keypad, const char *input) {
        auto req = request_gen("keypads", "write");
        auto params = (*req)["params"].as<JsonArray>();
        params.add(keypad);
        params.add(input);
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str, 1000 + strlen(input) * 300));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool keypads_set(Connection &con, unsigned int keypad, const char *keys) {
        auto keys_len = strlen(keys);
        auto req = request_gen("keypads", "set");
        auto params = (*req)["params"].as<JsonArray>();
        params.add(keypad);
        for (size_t i = 0; i < keys_len; i++) {
            char buf[] = {keys[i], 0x00};
            params.add(buf);
        }
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool keypads_get(Connection &con, unsigned int keypad, char *keys, size_t keys_len) {
        auto req = request_gen("keypads", "get");
        auto params = (*req)["params"].as<JsonArray>();
        params.add(keypad);
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        for (auto key : (*res)["data"].as<JsonArray>()) {
            const char *key_str = key;
            if (key_str != nullptr && keys_len > 0) {
                *(keys++) = key_str[0];
                keys_len--;
            }
        }
        delete res;
        return true;
    }

    size_t lights_read(Connection &con, LightState *buffer, size_t buffer_elements) {
        auto req = request_gen("lights", "read");
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return 0;
        
        auto data = (*res)["data"].as<JsonArray>();
        size_t buffer_count = 0;
        for (auto val : data) {
            if (buffer_count >= buffer_elements) {
                delete res;
                return buffer_count;
            }
            buffer[buffer_count].name = (const char*) val[0];
            buffer[buffer_count].value = val[1];
            buffer[buffer_count].enabled = val[2];
            buffer_count++;
        }
        delete res;
        return buffer_count;
    }

    bool lights_write(Connection &con, LightState *buffer, size_t buffer_elements) {
        auto req = request_gen("lights", "write");
        auto params = (*req)["params"].as<JsonArray>();
        for (size_t i = 0; i < buffer_elements; i++) {
            auto &state = buffer[i];
            auto data = params.createNestedArray();
            data.add(state.name);
            data.add(state.value);
        }
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool lights_write_reset(Connection &con, LightState *buffer, size_t buffer_elements) {
        auto req = request_gen("lights", "write_reset");
        auto params = (*req)["params"].as<JsonArray>();
        for (size_t i = 0; i < buffer_elements; i++) {
            auto &state = buffer[i];
            auto data = params.createNestedArray();
            data.add(state.name);
        }
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool memory_write(Connection &con, const char *dll_name, const char *hex, uint32_t offset) {
        auto req = request_gen("memory", "write");
        auto params = (*req)["params"].as<JsonArray>();
        params.add(dll_name);
        params.add(hex);
        params.add(offset);
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool memory_read(Connection &con, const char *dll_name, uint32_t offset, uint32_t size, String &hex) {
        auto req = request_gen("memory", "read");
        auto params = (*req)["params"].as<JsonArray>();
        params.add(dll_name);
        params.add(offset);
        params.add(size);
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        hex = (const char*) (*res)["data"][0];
        delete res;
        return true;
    }

    bool memory_signature(Connection &con, const char *dll_name, const char *signature,
                                    const char *replacement, uint32_t offset, uint32_t usage, uint32_t &file_offset) {
        auto req = request_gen("memory", "signature");
        auto params = (*req)["params"].as<JsonArray>();
        params.add(dll_name);
        params.add(signature);
        params.add(replacement);
        params.add(offset);
        params.add(usage);
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        file_offset = (*res)["data"][0];
        delete res;
        return true;
    }

    size_t touch_read(Connection &con, TouchState *buffer, size_t buffer_elements) {
        auto req = request_gen("touch", "read");
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return 0;
        
        auto data = (*res)["data"].as<JsonArray>();
        size_t buffer_count = 0;
        for (auto val : data) {
            if (buffer_count >= buffer_elements) {
                delete res;
                return buffer_count;
            }
            buffer[buffer_count].id = val[0];
            buffer[buffer_count].x = val[1];
            buffer[buffer_count].y = val[2];
            buffer_count++;
        }
        delete res;
        return buffer_count;
    }

    bool touch_write(Connection &con, TouchState *buffer, size_t buffer_elements) {
        auto req = request_gen("touch", "write");
        auto params = (*req)["params"].as<JsonArray>();
        for (size_t i = 0; i < buffer_elements; i++) {
            auto &state = buffer[i];
            auto data = params.createNestedArray();
            data.add(state.id);
            data.add(state.x);
            data.add(state.y);
        }
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }

    bool touch_write_reset(Connection &con, TouchState *buffer, size_t buffer_elements) {
        auto req = request_gen("touch", "write_reset");
        auto params = (*req)["params"].as<JsonArray>();
        for (size_t i = 0; i < buffer_elements; i++) {
            auto &state = buffer[i];
            auto data = params.createNestedArray();
            data.add(state.id);
        }
        auto req_str = doc2str(req);
        delete req;
        auto res = response_get(con, con.request(req_str));
        if (!res)
            return false;
        delete res;
        return true;
    }
}

#endif //SPICEAPI_WRAPPERS_H
