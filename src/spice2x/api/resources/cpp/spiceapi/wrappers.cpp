#include "wrappers.h"
#include <random>
#include <string>

/*
 * RapidJSON dependency
 * You might need to adjust the paths when importing into your own project.
 */
#include "external/rapidjson/document.h"
#include "external/rapidjson/writer.h"
using namespace rapidjson;


namespace spiceapi {

    static inline std::string doc2str(Document &doc) {
        StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
        doc.Accept(writer);
        return sb.GetString();
    }

    static inline Document request_gen(const char *module, const char *function) {

        // create document
        Document doc;
        doc.SetObject();

        // add attributes
        auto &alloc = doc.GetAllocator();
        doc.AddMember("id", msg_gen_id(), alloc);
        doc.AddMember("module", StringRef(module), alloc);
        doc.AddMember("function", StringRef(function), alloc);

        // add params
        Value noparam(kArrayType);
        doc.AddMember("params", noparam, alloc);

        // return document
        return doc;
    }

    static inline Document *response_get(std::string json) {

        // parse document
        Document *doc = new Document();
        doc->Parse(json.c_str());

        // check for parse error
        if (doc->HasParseError()) {
            delete doc;
            return nullptr;
        }

        // check id
        auto it_id = doc->FindMember("id");
        if (it_id == doc->MemberEnd() || !(*it_id).value.IsUint64()) {
            delete doc;
            return nullptr;
        }

        // check errors
        auto it_errors = doc->FindMember("errors");
        if (it_errors == doc->MemberEnd() || !(*it_errors).value.IsArray()) {
            delete doc;
            return nullptr;
        }

        // check error count
        if ((*it_errors).value.Size() > 0) {
            delete doc;
            return nullptr;
        }

        // check data
        auto it_data = doc->FindMember("data");
        if (it_data == doc->MemberEnd() || !(*it_data).value.IsArray()) {
            delete doc;
            return nullptr;
        }

        // return document
        return doc;
    }
}

uint64_t spiceapi::msg_gen_id() {
    static uint64_t id_global = 0;

    // check if global ID was initialized
    if (id_global == 0) {

        // generate a new ID
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dist(1, (uint64_t) std::llround(std::pow(2, 63)));
        id_global = dist(gen);

    } else {

        // increase by one
        id_global++;
    }

    // return global ID
    return id_global;
}

bool spiceapi::analogs_read(spiceapi::Connection &con, std::vector<spiceapi::AnalogState> &states) {
    auto req = request_gen("analogs", "read");
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    auto &data = (*res)["data"];
    for (auto &val : data.GetArray()) {
        AnalogState state;
        state.name = val[0].GetString();
        state.value = val[1].GetFloat();
        states.push_back(state);
    }
    delete res;
    return true;
}

bool spiceapi::analogs_write(spiceapi::Connection &con, std::vector<spiceapi::AnalogState> &states) {
    auto req = request_gen("analogs", "write");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    for (auto &state : states) {
        Value state_val(kArrayType);
        state_val.PushBack(StringRef(state.name.c_str()), alloc);
        state_val.PushBack(state.value, alloc);
        params.PushBack(state_val, alloc);
    }
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::analogs_write_reset(spiceapi::Connection &con, std::vector<spiceapi::AnalogState> &states) {
    auto req = request_gen("analogs", "write_reset");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    for (auto &state : states) {
        Value state_val(kArrayType);
        state_val.PushBack(StringRef(state.name.c_str()), alloc);
        params.PushBack(state_val, alloc);
    }
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::buttons_read(spiceapi::Connection &con, std::vector<spiceapi::ButtonState> &states) {
    auto req = request_gen("buttons", "read");
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    auto &data = (*res)["data"];
    for (auto &val : data.GetArray()) {
        ButtonState state;
        state.name = val[0].GetString();
        state.value = val[1].GetFloat();
        states.push_back(state);
    }
    delete res;
    return true;
}

bool spiceapi::buttons_write(spiceapi::Connection &con, std::vector<spiceapi::ButtonState> &states) {
    auto req = request_gen("buttons", "write");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    for (auto &state : states) {
        Value state_val(kArrayType);
        state_val.PushBack(StringRef(state.name.c_str()), alloc);
        state_val.PushBack(state.value, alloc);
        params.PushBack(state_val, alloc);
    }
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::buttons_write_reset(spiceapi::Connection &con, std::vector<spiceapi::ButtonState> &states) {
    auto req = request_gen("buttons", "write_reset");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    for (auto &state : states) {
        Value state_val(kArrayType);
        state_val.PushBack(StringRef(state.name.c_str()), alloc);
        params.PushBack(state_val, alloc);
    }
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::card_insert(spiceapi::Connection &con, size_t index, const char *card_id) {
    auto req = request_gen("card", "insert");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    params.PushBack(index, alloc);
    params.PushBack(StringRef(card_id), alloc);
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::coin_get(Connection &con, int &coins) {
    auto req = request_gen("coin", "get");
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    coins = (*res)["data"][0].GetInt();
    delete res;
    return true;
}

bool spiceapi::coin_set(Connection &con, int coins) {
    auto req = request_gen("coin", "set");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    params.PushBack(coins, alloc);
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::coin_insert(Connection &con, int coins) {
    auto req = request_gen("coin", "insert");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    params.PushBack(coins, alloc);
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::coin_blocker_get(Connection &con, bool &closed) {
    auto req = request_gen("coin", "blocker_get");
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    closed = (*res)["data"][0].GetBool();
    delete res;
    return true;
}

bool spiceapi::control_raise(spiceapi::Connection &con, const char *signal) {
    auto req = request_gen("control", "raise");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    params.PushBack(StringRef(signal), alloc);
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::control_exit(spiceapi::Connection &con) {
    auto req = request_gen("control", "exit");
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::control_exit(spiceapi::Connection &con, int exit_code) {
    auto req = request_gen("control", "exit");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    params.PushBack(exit_code, alloc);
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::control_restart(spiceapi::Connection &con) {
    auto req = request_gen("control", "restart");
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::control_session_refresh(spiceapi::Connection &con) {
    auto req = request_gen("control", "session_refresh");
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    auto key = (*res)["data"][0].GetString();
    con.change_pass(key);
    delete res;
    return true;
}

bool spiceapi::control_shutdown(spiceapi::Connection &con) {
    auto req = request_gen("control", "shutdown");
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::control_reboot(spiceapi::Connection &con) {
    auto req = request_gen("control", "reboot");
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::iidx_ticker_get(spiceapi::Connection &con, char *ticker) {
    auto req = request_gen("iidx", "ticker_get");
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    auto data = (*res)["data"][0].GetString();
    strncpy(ticker, data, 9);
    ticker[9] = 0x00;
    delete res;
    return true;
}

bool spiceapi::iidx_ticker_set(spiceapi::Connection &con, const char *ticker) {
    auto req = request_gen("iidx", "ticker_set");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    params.PushBack(StringRef(ticker), alloc);
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::iidx_ticker_reset(spiceapi::Connection &con) {
    auto req = request_gen("iidx", "ticker_reset");
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::iidx_tapeled_get(spiceapi::Connection &con) {
    auto req = request_gen("iidx", "tapeled_get");
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::info_avs(spiceapi::Connection &con, spiceapi::InfoAvs &info) {
    auto req = request_gen("info", "avs");
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    auto &data = (*res)["data"][0];
    info.model = data["model"].GetString();
    info.dest = data["dest"].GetString();
    info.spec = data["spec"].GetString();
    info.rev = data["rev"].GetString();
    info.ext = data["ext"].GetString();
    delete res;
    return true;
}

bool spiceapi::info_launcher(spiceapi::Connection &con, spiceapi::InfoLauncher &info) {
    auto req = request_gen("info", "launcher");
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    auto &data = (*res)["data"][0];
    info.version = data["version"].GetString();
    info.compile_date = data["compile_date"].GetString();
    info.compile_time = data["compile_time"].GetString();
    info.system_time = data["system_time"].GetString();
    for (auto &arg : data["args"].GetArray())
        info.args.push_back(arg.GetString());
    delete res;
    return true;
}

bool spiceapi::info_memory(spiceapi::Connection &con, spiceapi::InfoMemory &info) {
    auto req = request_gen("info", "memory");
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    auto &data = (*res)["data"][0];
    info.mem_total = data["mem_total"].GetUint64();
    info.mem_total_used = data["mem_total_used"].GetUint64();
    info.mem_used = data["mem_used"].GetUint64();
    info.vmem_total = data["vmem_total"].GetUint64();
    info.vmem_total_used = data["vmem_total_used"].GetUint64();
    info.vmem_used = data["vmem_used"].GetUint64();
    delete res;
    return true;
}

bool spiceapi::keypads_write(spiceapi::Connection &con, unsigned int keypad, const char *input) {
    auto req = request_gen("keypads", "write");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    params.PushBack(keypad, alloc);
    params.PushBack(StringRef(input), alloc);
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::keypads_set(spiceapi::Connection &con, unsigned int keypad, std::vector<char> &keys) {
    auto req = request_gen("keypads", "set");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    params.PushBack(keypad, alloc);
    for (auto &key : keys)
        params.PushBack(StringRef(&key, 1), alloc);
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::keypads_get(spiceapi::Connection &con, unsigned int keypad, std::vector<char> &keys) {
    auto req = request_gen("keypads", "get");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    params.PushBack(keypad, alloc);
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    auto &data = (*res)["data"];
    for (auto &val : data.GetArray())
        keys.push_back(val.GetString()[0]);
    delete res;
    return true;
}

bool spiceapi::lights_read(spiceapi::Connection &con, std::vector<spiceapi::LightState> &states) {
    auto req = request_gen("lights", "read");
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    auto &data = (*res)["data"];
    for (auto &val : data.GetArray()) {
        LightState state;
        state.name = val[0].GetString();
        state.value = val[1].GetFloat();
        states.push_back(state);
    }
    delete res;
    return true;
}

bool spiceapi::lights_write(spiceapi::Connection &con, std::vector<spiceapi::LightState> &states) {
    auto req = request_gen("lights", "write");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    for (auto &state : states) {
        Value state_val(kArrayType);
        state_val.PushBack(StringRef(state.name.c_str()), alloc);
        state_val.PushBack(state.value, alloc);
        params.PushBack(state_val, alloc);
    }
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::lights_write_reset(spiceapi::Connection &con, std::vector<spiceapi::LightState> &states) {
    auto req = request_gen("lights", "write_reset");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    for (auto &state : states) {
        Value state_val(kArrayType);
        state_val.PushBack(StringRef(state.name.c_str()), alloc);
        params.PushBack(state_val, alloc);
    }
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::memory_write(spiceapi::Connection &con, const char *dll_name, const char *hex, uint32_t offset) {
    auto req = request_gen("memory", "write");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    params.PushBack(StringRef(dll_name), alloc);
    params.PushBack(StringRef(hex), alloc);
    params.PushBack(offset, alloc);
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::memory_read(spiceapi::Connection &con, const char *dll_name, uint32_t offset, uint32_t size,
        std::string &hex) {
    auto req = request_gen("memory", "read");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    params.PushBack(StringRef(dll_name), alloc);
    params.PushBack(offset, alloc);
    params.PushBack(size, alloc);
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    hex = (*res)["data"][0].GetString();
    delete res;
    return true;
}

bool spiceapi::memory_signature(spiceapi::Connection &con, const char *dll_name, const char *signature,
                                const char *replacement, uint32_t offset, uint32_t usage, uint32_t &file_offset) {
    auto req = request_gen("memory", "signature");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    params.PushBack(StringRef(dll_name), alloc);
    params.PushBack(StringRef(signature), alloc);
    params.PushBack(StringRef(replacement), alloc);
    params.PushBack(offset, alloc);
    params.PushBack(usage, alloc);
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    file_offset = (*res)["data"][0].GetUint();
    delete res;
    return true;
}

bool spiceapi::touch_read(spiceapi::Connection &con, std::vector<spiceapi::TouchState> &states) {
    auto req = request_gen("touch", "read");
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    auto &data = (*res)["data"];
    for (auto &val : data.GetArray()) {
        TouchState state;
        state.id = val[0].GetUint64();
        state.x = val[1].GetInt64();
        state.y = val[2].GetInt64();
        states.push_back(state);
    }
    delete res;
    return true;
}

bool spiceapi::touch_write(spiceapi::Connection &con, std::vector<spiceapi::TouchState> &states) {
    auto req = request_gen("touch", "write");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    for (auto &state : states) {
        Value state_val(kArrayType);
        state_val.PushBack(state.id, alloc);
        state_val.PushBack(state.x, alloc);
        state_val.PushBack(state.y, alloc);
        params.PushBack(state_val, alloc);
    }
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::touch_write_reset(spiceapi::Connection &con, std::vector<spiceapi::TouchState> &states) {
    auto req = request_gen("touch", "write_reset");
    auto &alloc = req.GetAllocator();
    Value params(kArrayType);
    for (auto &state : states)
        params.PushBack(state.id, alloc);
    req["params"] = params;
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    delete res;
    return true;
}

bool spiceapi::lcd_info(spiceapi::Connection &con, spiceapi::LCDInfo &info) {
    auto req = request_gen("lcd", "info");
    auto res = response_get(con.request(doc2str(req)));
    if (!res)
        return false;
    auto &data = (*res)["data"][0];
    info.enabled = data["enabled"].GetBool();
    info.csm = data["csm"].GetString();
    info.bri = data["bri"].GetInt();
    info.con = data["con"].GetInt();
    info.bl = data["bl"].GetInt();
    info.red = data["red"].GetInt();
    info.green = data["green"].GetInt();
    info.blue = data["blue"].GetInt();
    delete res;
    return true;
}
