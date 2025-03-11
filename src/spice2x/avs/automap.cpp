#include "automap.h"
#include <fstream>
#include "external/tinyxml2/tinyxml2.h"
#include "util/logging.h"
#include "util/detour.h"
#include "util/utils.h"
#include "util/fileutils.h"
#include "./core.h"

// macro for easy hooking
#define AUTOMAP_HOOK(s) detour::iat_try_proc(avs::core::DLL_NAME.c_str(), avs::core::s, s)

namespace avs::automap {

    // state
    bool ENABLED = false;
    bool DUMP = false;
    bool PATCH = false;
    bool JSON = false;
    bool RESTRICT_NETWORK = false;
    std::string DUMP_FILENAME = "";

    // logging
    static std::ofstream LOGFILE;
    static std::vector<std::pair<AutomapHook_t, void*>> HOOKS;


    static bool property_is_network(avs::core::property_ptr prop, avs::core::node_ptr node = nullptr) {
        avs::core::node_ptr root_node = nullptr;

        /*
         * on failure to detect, we must return true
         * most of the time, no prop is specified
         */

        // get root node
        if (prop) {

            // when prop is specified, this is easy
            root_node = avs::core::property_search(prop, nullptr, "/");

        } else if (node) {


            // otherwise we need to traverse
            root_node = node;
            for (int i = 0; i < 1000; i++) {
                auto parent_node = avs::core::property_node_traversal(node, avs::core::TRAVERSE_PARENT);
                if (parent_node == root_node || parent_node == nullptr) {
                    break;
                } else {
                    root_node = parent_node;
                }
            }
        }

        // verify root node
        if (!root_node) {
            if (prop != nullptr) {
                avs::core::property_clear_error(prop);
            }
            return true;
        }

        // check name
        char root_name[256] {};
        if (avs::core::property_node_name(root_node, root_name, sizeof(root_name)) >= 0) {
            return !strcmp(root_name, "call") || !strcmp(root_name, "response");
        } else {
            return true;
        }
    }

    static bool property_patches_enabled(avs::core::property_ptr prop, avs::core::node_ptr node = nullptr,
            bool force = false) {
        if (!ENABLED) {
            return false;
        }

        // check if patches are enabled first
        if (!PATCH) {
            return false;
        } else if (force) {
            return true;
        }

        // check if restricted to network
        if (RESTRICT_NETWORK) {
            return property_is_network(prop, node);
        } else {
            return true;
        }
    }

    static bool property_dump_enabled(avs::core::property_ptr prop, avs::core::node_ptr node = nullptr) {
        if (!ENABLED) {
            return false;
        }

        // check if dumps are enabled first
        if (!DUMP && HOOKS.empty()) {
            return false;
        }

        // check if restricted to network
        if (RESTRICT_NETWORK) {
            return property_is_network(prop, node);
        } else {
            return true;
        }
    }

    uint32_t property_get_error(avs::core::property_ptr prop) {
        auto error = avs::core::property_get_error(prop);
        if (error != 0 && prop != nullptr) {
            log_misc("automap", "detected error: {}", error);
            avs::core::property_clear_error(prop);
            return avs::core::property_get_error(prop);
        }
        return error;
    }

    avs::core::node_ptr property_search(avs::core::property_ptr prop, avs::core::node_ptr node, const char *path) {

        // call original
        auto result = avs::core::property_search(prop, node, path);
        if (result != nullptr) {
            return result;
        } else if (!property_patches_enabled(prop, node)) {
            log_misc("automap", "property_search error: {}", path ? path : "?");
            return result;
        }

        // create node
        log_misc("automap", "property_search error: {}", path ? path : "?");
        static const uint8_t zero_buffer[64] {};
        avs::core::property_node_create(prop, node, avs::core::NODE_TYPE_node, path, zero_buffer);

        // clear error
        if (prop) {
            avs::core::property_clear_error(prop);
        }

        // now try again
        return avs::core::property_search(prop, node, path);
    }

    int property_node_refer(avs::core::property_ptr prop, avs::core::node_ptr node, const char *path,
            avs::core::node_type type, void *data, uint32_t data_size) {
        auto result = avs::core::property_node_refer(prop, node, path, type, data, data_size);
        if (result >= 0) {
            return result;
        } else if (!property_patches_enabled(prop, node)) {
            log_misc("automap", "property_node_refer error: {}", path ? path : "?");
            return result;
        }

        // check if accessed by path
        if (!path) {

            // print log without path
            log_misc("automap", "node refer error: {}", result);

        } else {
            std::string path_str(path);

            // prevent infinite loop in cardmng
            if (string_begins_with(path_str, "/card_allow/card")) {
                return result;
            }

            // print log with path
            log_misc("automap", "node refer error ({}): {}", path, result);
            if (path_str == "/") {
                return result;
            }
        }

        // maybe it *does* exist, but with the wrong type
        auto find_node = avs::core::property_search(prop, node, path);
        if (find_node != nullptr) {

            // now get rid of it
            avs::core::property_node_remove(find_node);
        }

        // fake buffer if required
        uint8_t *buffer = reinterpret_cast<uint8_t *>(data);
        auto buffer_size = data_size;
        if (buffer == nullptr || buffer_size == 0) {
            static uint8_t zero_buffer[64];
            buffer = zero_buffer;
            buffer_size = sizeof(zero_buffer);
        }

        // initialize field
        memset(buffer, 0, buffer_size);
        if ((type == avs::core::NODE_TYPE_attr || type == avs::core::NODE_TYPE_str) && buffer_size > 1) {

            // init attributes and strings with "0" by default - useful for "status" or "error" fields
            buffer[0] = '0';
            buffer[1] = '\x00';
        }
        if (path) {

            // for numbers and counts we fake a 1 to get a single entry
            if (string_ends_with(path, "num") || string_ends_with(path, "count")) {
                if ((type == avs::core::NODE_TYPE_attr || type == avs::core::NODE_TYPE_str) && buffer_size > 1) {
                    buffer[0] = '1';
                    buffer[1] = '\x00';
                } else if (buffer_size > 0) {
                    buffer[0] = 0x01;
                }
            }
        }

        // add to property
        avs::core::property_node_create(prop, node, type, path, buffer);

        // clear error
        if (prop) {
            avs::core::property_clear_error(prop);
        }

        // try again
        return avs::core::property_node_refer(prop, node, path, type, data, data_size);
    }

    avs::core::avs_error_t property_get_attribute_u32(
            avs::core::property_ptr prop,
            avs::core::node_ptr node,
            const char *path,
            uint32_t *value)
    {
        auto result = avs::core::property_get_attribute_u32(prop, node, path, value);
        if (result < 0 && property_patches_enabled(prop, node, true)) {
            log_warning("automap", "property_get_attribute_u32 failed: {}", path ? path : "?");
            const char *data = "0";
            avs::core::property_node_create(prop, node, avs::core::NODE_TYPE_attr, path, data);
            return avs::core::property_get_attribute_u32(prop, node, path, value);
        } else {
            return result;
        }
    }

    avs::core::avs_error_t property_get_attribute_s32(
            avs::core::property_ptr prop,
            avs::core::node_ptr node,
            const char *path,
            int32_t *value)
    {
        auto result = avs::core::property_get_attribute_s32(prop, node, path, value);
        if (result < 0 && property_patches_enabled(prop, node, true)) {
            log_warning("automap", "property_get_attribute_s32 failed: {}", path ? path : "?");
            const char *data = "0";
            avs::core::property_node_create(prop, node, avs::core::NODE_TYPE_attr, path, data);
            return avs::core::property_get_attribute_s32(prop, node, path, value);
        } else {
            return result;
        }
    }

    int property_node_read(avs::core::node_ptr node, avs::core::node_type type, void *data, uint32_t data_size) {
        auto result = avs::core::property_node_read(node, type, data, data_size);
        if (result < 0 && property_patches_enabled(nullptr, node)) {
            log_warning("automap", "property_node_read failed ({}): {}", type, result);

            // probably wrong type
            memset(data, 0, data_size);
            if ((type == avs::core::NODE_TYPE_attr || type == avs::core::NODE_TYPE_str) && data_size > 1) {
                ((char *) data)[0] = '0';
                ((char *) data)[1] = '\x00';
                data_size = 2;
            }

            // recreate the node with the correct type (probably invalidates the pointer, how to fix?)
            auto parent_node = avs::core::property_node_traversal(node, avs::core::TRAVERSE_PARENT);
            if (parent_node && parent_node != node) {
                char name_buf[256] {};
                if (avs::core::property_node_name(node, name_buf, sizeof(name_buf)) >= 0) {
                    log_warning("automap", "property_node_read replace type ({}): {}", type, result);
                    avs::core::property_node_remove(node);
                    avs::core::property_node_create(nullptr, parent_node, type, name_buf, data);
                }
            }

            // return input data size
            return data_size;

        } else {
            return result;
        }
    }

    bool property_psmap_export(avs::core::property_ptr prop, avs::core::node_ptr node, uint8_t *data,
            avs::core::psmap_data_ptr psmap) {

        // check if enabled
        if (property_patches_enabled(prop, node)) {

            // iterate psmap
            for (size_t i = 0; psmap[i].type != 0xFF; i++) {
                auto &cur_entry = psmap[i];

                // determine node type
                avs::core::node_type node_type;
                switch (cur_entry.type) {
                    case avs::core::PSMAP_TYPE_s8:
                        node_type = avs::core::NODE_TYPE_s8;
                        break;
                    case avs::core::PSMAP_TYPE_u8:
                        node_type = avs::core::NODE_TYPE_u8;
                        break;
                    case avs::core::PSMAP_TYPE_s16:
                        node_type = avs::core::NODE_TYPE_s16;
                        break;
                    case avs::core::PSMAP_TYPE_u16:
                        node_type = avs::core::NODE_TYPE_u16;
                        break;
                    case avs::core::PSMAP_TYPE_s32:
                        node_type = avs::core::NODE_TYPE_s32;
                        break;
                    case avs::core::PSMAP_TYPE_u32:
                        node_type = avs::core::NODE_TYPE_u32;
                        break;
                    case avs::core::PSMAP_TYPE_s64:
                        node_type = avs::core::NODE_TYPE_s64;
                        break;
                    case avs::core::PSMAP_TYPE_u64:
                        node_type = avs::core::NODE_TYPE_u64;
                        break;
                    case avs::core::PSMAP_TYPE_str:
                    case avs::core::PSMAP_TYPE_str2:
                        node_type = avs::core::NODE_TYPE_str;
                        break;
                    case avs::core::PSMAP_TYPE_attr:
                        node_type = avs::core::NODE_TYPE_attr;
                        break;
                    case avs::core::PSMAP_TYPE_bool:
                        node_type = avs::core::NODE_TYPE_bool;
                        break;
                    default:
                        node_type = (avs::core::node_type) 0;
                        break;
                }

                // check if node type was found
                if (node_type != 0) {

                    // get rid of existing nodes with type mismatch
                    auto node_search = avs::core::property_search(prop, node, cur_entry.path);
                    if (node_search) {
                        auto node_search_type = avs::core::property_node_type(node_search);
                        if (node_type != node_search_type) {
                            log_misc("automap", "psmap type mismatch, replacing node: {}", cur_entry.path);
                            avs::core::property_node_remove(node_search);
                        } else {

                            // this one seems to be fine actually!
                            continue;
                        }
                    }

                    // generate node
                    log_misc("automap", "generating psmap node: {}", cur_entry.path);
                    uint8_t node_data[64]{};
                    if (node_type == avs::core::NODE_TYPE_attr || node_type == avs::core::NODE_TYPE_str) {
                        node_data[0] = '0';
                        node_data[1] = '\x00';
                    }
                    avs::core::property_node_create(prop, node, node_type, cur_entry.path, node_data);
                }
            }
        }

        // now we call the original
        return avs::core::property_psmap_export(prop, node, data, psmap);
    }

    avs::core::avs_error_t property_destroy(avs::core::property_ptr prop) {

        // we definitely need a property for this to work
        if (prop == NULL) {
            log_warning("automap", "property_destroy called on NULL");
            return 0;
        }

        // check if dump is enabled
        if (property_dump_enabled(prop)) {

            // convert to XML
            avs::core::property_set_flag(prop, avs::core::PROP_XML, avs::core::PROP_BINARY);

            // optionally reconvert to JSON
            if (JSON) {
                avs::core::property_set_flag(prop, avs::core::PROP_JSON, avs::core::PROP_XML);
            }

            // query size
            auto size = avs::core::property_query_size(prop);
            if (size < 0) {
                log_warning("automap", "couldn't query property size");
            } else {
                log_misc("automap", "writing property to file: {} bytes", size);

                // get XML
                std::vector<uint8_t> data(size);
                if (avs::core::property_mem_write(prop, &data[0], data.size()) >= 0) {

                    // initialize log
                    if (DUMP && !LOGFILE.is_open()) {

                        // try filenames with IDs starting at 0
                        for (int i = 0; i < 10000 && !LOGFILE.is_open(); i++) {
                            std::string path = "automap_" + to_string(i) + ".xml";

                            // check if this one is available to use
                            if (!fileutils::file_exists(path)) {

                                // try creating the file
                                LOGFILE.open(path, std::ios::out | std::ios::binary);
                                if (LOGFILE.is_open()) {
                                    DUMP_FILENAME = path;
                                    log_info("automap", "using logfile: {}", path);
                                }
                            }
                        }
                    }

                    // prettify
                    tinyxml2::XMLDocument document;
                    if (!JSON && document.Parse((const char*) &data[0],
                            data.size()) == tinyxml2::XMLError::XML_SUCCESS) {

                        // write pretty output to log
                        tinyxml2::XMLPrinter xml_printer;
                        document.Print(&xml_printer);
                        for (auto &hook : HOOKS) {
                            hook.first(hook.second, xml_printer.CStr());
                        }
                        if (DUMP && LOGFILE.is_open()) {
                            LOGFILE << xml_printer.CStr() << std::endl;
                        }

                    } else {

                        // write avs output to log
                        for (auto &hook : HOOKS) {
                            hook.first(hook.second, std::string((const char *) &data[0], data.size()).c_str());
                        }
                        if (DUMP && LOGFILE.is_open()) {
                            LOGFILE << std::string((const char *) &data[0], data.size());
                        }
                    }

                    // flush
                    LOGFILE.flush();

                } else {
                    log_warning("automap", "couldn't write property to memory");
                }
            }
        }

        // kill it with fire
        return avs::core::property_destroy(prop);
    }

    void enable() {
        log_info("automap", "enable");
        ENABLED = true;

        // check if optional imports are supported for this avs version
        if (!avs::core::property_node_read || !avs::core::property_get_error) {
            log_fatal("automap", "missing optional avs imports which are required for this module to work");
        }

        // apply hooks
        AUTOMAP_HOOK(property_get_error);
        AUTOMAP_HOOK(property_search);
        AUTOMAP_HOOK(property_node_refer);
        AUTOMAP_HOOK(property_get_attribute_u32);
        AUTOMAP_HOOK(property_get_attribute_s32);
        AUTOMAP_HOOK(property_node_read);
        AUTOMAP_HOOK(property_psmap_export);
        AUTOMAP_HOOK(property_destroy);
    }

    void disable() {
        log_info("automap", "disable");
        ENABLED = false;
    }

    void hook_add(AutomapHook_t hook, void *user) {
        HOOKS.push_back(std::pair(hook, user));
    }

    void hook_remove(AutomapHook_t hook, void *user) {
        HOOKS.erase(std::remove(HOOKS.begin(), HOOKS.end(), std::pair(hook, user)), HOOKS.end());
    }
}
