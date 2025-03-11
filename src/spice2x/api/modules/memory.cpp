#include "memory.h"

#include <functional>
#include <mutex>

#include "external/rapidjson/document.h"
#include "util/fileutils.h"
#include "util/libutils.h"
#include "util/memutils.h"
#include "util/sigscan.h"
#include "util/utils.h"

using namespace std::placeholders;
using namespace rapidjson;


namespace api::modules {

    // global lock to prevent simultaneous access to memory
    static std::mutex MEMORY_LOCK;

    Memory::Memory() : Module("memory", true) {
        functions["write"] = std::bind(&Memory::write, this, _1, _2);
        functions["read"] = std::bind(&Memory::read, this, _1, _2);
        functions["signature"] = std::bind(&Memory::signature, this, _1, _2);
    }

    /**
     * write(dll_name: str, data: hex, offset: uint)
     */
    void Memory::write(Request &req, Response &res) {
        std::lock_guard<std::mutex> lock(MEMORY_LOCK);

        // check params
        if (req.params.Size() < 3) {
            return error_params_insufficient(res);
        }
        if (!req.params[0].IsString()) {
            return error_type(res, "dll_name", "str");
        }
        if (!req.params[1].IsString() || (req.params[1].GetStringLength() & 1)) {
            return error_type(res, "data", "hex string");
        }
        if (!req.params[2].IsUint()) {
            return error_type(res, "offset", "uint");
        }

        // get params
        auto dll_name = req.params[0].GetString();
        auto dll_path = MODULE_PATH / dll_name;
        auto data = req.params[1].GetString();
        intptr_t offset = req.params[2].GetUint();

        // convert data to bin
        size_t data_bin_size = strlen(data) / 2;
        auto data_bin = std::make_unique<uint8_t[]>(data_bin_size);
        hex2bin(data, data_bin.get());

        // check if file exists in modules
        if (!fileutils::file_exists(dll_path)) {
            return error(res, "Couldn't find " + dll_path.string());
        }

        // get module
        auto module = libutils::try_module(dll_name);
        if (!module) {
            return error(res, "Couldn't find module.");
        }

        // convert offset to RVA
        offset = libutils::offset2rva(dll_path, offset);
        if (offset == ~0) {
            return error(res, "Couldn't convert offset to RVA.");
        }

        // get module information
        MODULEINFO module_info {};
        if (!GetModuleInformation(GetCurrentProcess(), module, &module_info, sizeof(MODULEINFO))) {
            return error(res, "Couldn't get module information.");
        }

        // check bounds
        if (offset + data_bin_size >= (size_t) module_info.lpBaseOfDll + module_info.SizeOfImage) {
            return error(res, "Data out of bounds.");
        }
        auto data_pos = reinterpret_cast<uint8_t *>(module_info.lpBaseOfDll) + offset;

        // replace data
        memutils::VProtectGuard guard(data_pos, data_bin_size);
        memcpy(data_pos, data_bin.get(), data_bin_size);
    }

    /**
     * read(dll_name: str, offset: uint, size: uint)
     */
    void Memory::read(Request &req, Response &res) {
        std::lock_guard<std::mutex> lock(MEMORY_LOCK);

        // check params
        if (req.params.Size() < 3) {
            return error_params_insufficient(res);
        }
        if (!req.params[0].IsString()) {
            return error_type(res, "dll_name", "str");
        }
        if (!req.params[1].IsUint()) {
            return error_type(res, "offset", "uint");
        }
        if (!req.params[2].IsUint()) {
            return error_type(res, "size", "uint");
        }

        // get params
        auto dll_name = req.params[0].GetString();
        auto dll_path = MODULE_PATH / dll_name;
        intptr_t offset = req.params[1].GetUint();
        auto size = req.params[2].GetUint();

        // check if file exists in modules
        if (!fileutils::file_exists(dll_path)) {
            return error(res, "Couldn't find " + dll_path.string());
        }

        // get module
        auto module = libutils::try_module(dll_name);
        if (!module) {
            return error(res, "Couldn't find module.");
        }

        // convert offset to RVA
        offset = libutils::offset2rva(dll_path, offset);
        if (offset == ~0) {
            return error(res, "Couldn't convert offset to RVA.");
        }

        // get module information
        MODULEINFO module_info {};
        if (!GetModuleInformation(GetCurrentProcess(), module, &module_info, sizeof(MODULEINFO))) {
            return error(res, "Couldn't get module information.");
        }

        // check bounds
        auto max = offset + size;
        if ((size_t) max >= (size_t) module_info.lpBaseOfDll + module_info.SizeOfImage) {
            return error(res, "Data out of bounds.");
        }

        // read memory to hex (without virtual protect)
        std::string hex = bin2hex((uint8_t*) module_info.lpBaseOfDll + offset, size);
        Value hex_val(hex.c_str(), res.doc()->GetAllocator());
        res.add_data(hex_val);
    }

    /**
     * signature(
     *     dll_name: str,
     *     signature: hex,
     *     replacement: hex,
     *     offset: uint,
     *     usage: uint)
     *
     * Both signature and replacement will ignore bytes specified as "??" in the hex string.
     * The offset specifies the offset between the found signature and the position to write the replacement to.
     * The resulting integer is the file offset where the replacement was written to.
     */
    void Memory::signature(Request &req, Response &res) {
        std::lock_guard<std::mutex> lock(MEMORY_LOCK);

        // check params
        if (req.params.Size() < 5) {
            return error_params_insufficient(res);
        }
        if (!req.params[0].IsString()) {
            return error_type(res, "dll_name", "string");
        }
        if (!req.params[1].IsString() || (req.params[1].GetStringLength() & 1)) {
            return error_type(res, "signature", "hex string");
        }
        if (!req.params[2].IsString() || (req.params[2].GetStringLength() & 1)) {
            return error_type(res, "replacement", "hex string");
        }
        if (!req.params[3].IsUint()) {
            return error_type(res, "offset", "uint");
        }
        if (!req.params[4].IsUint()) {
            return error_type(res, "usage", "uint");
        }

        // get params
        auto dll_name = req.params[0].GetString();
        auto dll_path = MODULE_PATH / dll_name;
        auto signature = req.params[1].GetString();
        auto replacement = req.params[2].GetString();
        auto offset = req.params[3].GetUint();
        auto usage = req.params[4].GetUint();

        // check if file exists in modules
        if (!fileutils::file_exists(dll_path)) {
            return error(res, "Couldn't find " + dll_path.string());
        }

        // get module
        auto module = libutils::try_module(dll_name);
        if (!module) {
            return error(res, "Couldn't find module.");
        }

        // execute
        auto result = replace_pattern(
                module,
                signature,
                replacement,
                offset,
                usage
        );

        // check result
        if (!result) {
            return error(res, std::string("Pattern not found in memory of ") + dll_name);
        }

        // convert to offset
        auto rva = result - reinterpret_cast<intptr_t>(module);
        result = libutils::rva2offset(dll_path, rva);
        if (result == -1) {
            return error(res, "Couldn't convert RVA to file offset.");
        }

        // add result
        Value result_val(result);
        res.add_data(result_val);
    }
}
