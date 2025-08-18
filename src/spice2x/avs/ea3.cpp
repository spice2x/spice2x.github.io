#include "ea3.h"

#include <optional>

#include "build/defs.h"
#include "easrv/easrv.h"
#include "easrv/smartea.h"
#include "games/mfc/mfc.h"
#include "hooks/avshook.h"
#include "util/detour.h"
#include "util/fileutils.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/utils.h"
#include "launcher/logger.h"

#include "core.h"
#include "game.h"

namespace avs {

    typedef void (*ssl_protocol_init_t)();
    typedef void (*ssl_protocol_fini_t)();
    static ssl_protocol_init_t ssl_protocol_init = nullptr;
    static ssl_protocol_fini_t ssl_protocol_fini = nullptr;

    // functions
    AVS_EA3_BOOT_STARTUP_T avs_ea3_boot_startup;
    AVS_EA3_SHUTDOWN_T avs_ea3_shutdown;

    namespace ea3 {

        // settings
        std::string CFG_PATH;
        std::string APP_PATH;
        std::string BOOTSTRAP_PATH;
        std::string PCBID_CUSTOM = "";
        std::string SOFTID_CUSTOM = "";
        std::string ACCOUNTID_CUSTOM = "";
        std::string URL_CUSTOM = "";
        int HTTP11 = -1;
        int URL_SLASH = -1;
        int PCB_TYPE = -1;

        // handle
        std::string VERSION_STR = "unknown";
        HINSTANCE DLL_INSTANCE = nullptr;
        std::string DLL_NAME = "";
        std::string EA3_BOOT_URL = "";
        std::string EA3_BOOT_PCBID = "";
        std::string EA3_BOOT_ACCOUNTID = "";

        // static fields
        static constexpr struct avs_ea3_import IMPORT_LEGACY {
            .version  = "legacy",
            .boot     = "ea3_boot",
            .shutdown = "ea3_shutdown",
        };
        static constexpr struct avs_ea3_import IMPORT_AVS21360 {
            .version  = "2.13.6.0",
            .boot     = "XEb552d500005d",
            .shutdown = "XEb552d5000060",
        };
        static constexpr struct avs_ea3_import IMPORT_AVS21430 {
            .version  = "2.14.3.0",
            .boot     = "XE7aee11000070",
            .shutdown = "XE7aee11000074",
        };
        static constexpr struct avs_ea3_import IMPORT_AVS21580 {
            .version  = "2.15.8.0",
            .boot     = "XE592acd00008c",
            .shutdown = "XE592acd00005a",
        };
        static constexpr struct avs_ea3_import IMPORT_AVS21610 {
            .version  = "2.16.1.0",
            .boot     = "XEyy2igh000006",
            .shutdown = "XEyy2igh000007",
        };
        static constexpr struct avs_ea3_import IMPORT_AVS21630 {
            .version  = "2.16.3.0",
            .boot     = "XEyy2igh000007",
            .shutdown = "XEyy2igh000008",
        };
        static constexpr struct avs_ea3_import IMPORT_AVS21651 {
            .version  = "2.16.5.1",
            .boot     = "XEyy2igh000007",
            .shutdown = "XEyy2igh000008",
        };
        static constexpr struct avs_ea3_import IMPORT_AVS21671 {
            .version  = "2.16.7.1",
            .boot     = "XEyy2igh000007",
            .shutdown = "XEyy2igh000008",
        };
        static constexpr struct avs_ea3_import IMPORT_AVS21681 {
            .version  = "2.16.8.1",
            .boot     = "XEyy2igh000007",
            .shutdown = "XEyy2igh000008",
        };
        static constexpr struct avs_ea3_import IMPORT_AVS21700 {
            .version  = "2.17.0.0",
            .boot     = "XEmdwapa000024",
            .shutdown = "XEmdwapa000025",
        };
        static constexpr struct avs_ea3_import IMPORT_AVS21730 {
            .version  = "2.17.3.0",
            .boot     = "XEmdwapa000024",
            .shutdown = "XEmdwapa000025",
        };
        static const struct avs_ea3_import IMPORTS[core::AVS_VERSION_COUNT] = {
            IMPORT_LEGACY,
            IMPORT_AVS21360,
            IMPORT_AVS21430,
            IMPORT_AVS21580,
            IMPORT_AVS21610,
            IMPORT_AVS21630,
            IMPORT_AVS21651,
            IMPORT_AVS21671,
            IMPORT_AVS21681,
            IMPORT_AVS21700,
            IMPORT_AVS21730,
        };


        void load_dll() {
            log_info("avs-ea3", "loading DLL");

            // detect DLL name
            if (fileutils::file_exists(MODULE_PATH / "avs2-ea3.dll")) {
                DLL_NAME = "avs2-ea3.dll";
            } else {
#ifdef SPICE64
                DLL_NAME = "libavs-win64-ea3.dll";
#else
                DLL_NAME = "libavs-win32-ea3.dll";
#endif

                if (!fileutils::file_exists(MODULE_PATH / DLL_NAME)) {
                    std::string info_str { fmt::format(
                        "\n\n"
                        "Failed to find critical ea3 DLL on disk (avs2-ea3.dll OR {})\n"
                        "Looked in the following directory: {}\n"
                        "\n"
                        "One of these is required to boot the game. Spice found neither of them. You do not need both, just one, next to your game DLL.\n"
                        "\n"
                        "HOW TO FIX:\n"
                        "    * Avoid manually specifying DLL path (-exec) and module directory (-modules); let spice2x auto-detect unless you have a good reason not to\n"
                        "    * Ensure you do NOT have multiple copies of the game DLLs (e.g., in contents and in contents\\modules)\n"
                        "    * It's also possible that you have incomplete game data\n"
                        "    * Do NOT copy over random DLLs from another game installation; DLL must match game version\n"
                        "\n"
                    , DLL_NAME, MODULE_PATH.string()) };
                    log_fatal("avs-ea3", "{}", info_str);
                }
            }

            // load library
            DLL_INSTANCE = libutils::load_library(MODULE_PATH / DLL_NAME);

            // check by version string
            std::optional<size_t> ver;
            char version[32] {};
            if (fileutils::version_pe(MODULE_PATH / DLL_NAME, version)) {
                for (size_t i = 0; i < core::AVS_VERSION_COUNT; i++) {
                    if (strcmp(IMPORTS[i].version, version) == 0) {
                        ver = i;
                        break;
                    }
                }
            }

            // check version by brute force
            if (!ver.has_value()) {
                for (size_t i = 0; i < core::AVS_VERSION_COUNT; i++) {
                    if (GetProcAddress(DLL_INSTANCE, IMPORTS[i].boot) != nullptr) {
                        ver = i;
                        break;
                    }
                }
            }

            // check if version was found
            if (!ver.has_value()) {
                log_fatal("avs-ea3", "Unknown {}", DLL_NAME);
            }
            size_t i = ver.value();

            // print version
            VERSION_STR = IMPORTS[i].version;
            log_info("avs-ea3", "Found AVS2 EA3 {}", IMPORTS[i].version);

            // load functions
            avs_ea3_boot_startup = libutils::get_proc<AVS_EA3_BOOT_STARTUP_T>(
                    DLL_INSTANCE, IMPORTS[i].boot);
            avs_ea3_shutdown = libutils::get_proc<AVS_EA3_SHUTDOWN_T>(
                    DLL_INSTANCE, IMPORTS[i].shutdown);
        }

        void boot(unsigned short easrv_port, bool easrv_maint, bool easrv_smart) {
            // detect ea3-config file name
            const char *ea3_config_name;
            if (CFG_PATH.size()) {
                ea3_config_name = CFG_PATH.c_str();
            } else if (fileutils::file_exists("prop/ea3-config.xml")) {
                ea3_config_name = "prop/ea3-config.xml";
            } else if (fileutils::file_exists("prop/ea3-cfg.xml")) {
                ea3_config_name = "prop/ea3-cfg.xml";
            } else if (avs::game::DLL_NAME == "beatstream1.dll" && fileutils::file_exists("prop/ea3-config-1.xml")) {
                ea3_config_name = "prop/ea3-config-1.xml";
            } else if (avs::game::DLL_NAME == "beatstream2.dll" && fileutils::file_exists("prop/ea3-config-2.xml")) {
                ea3_config_name = "prop/ea3-config-2.xml";
            } else {
                ea3_config_name = "prop/eamuse-config.xml";
            }

            log_info("avs-ea3", "booting (using {})", ea3_config_name);

            // remember new config path
            CFG_PATH = ea3_config_name;

            // detect app-config file name
            APP_PATH = APP_PATH.size() ? APP_PATH : "prop/app-config.xml";

            // detect bootstrap file name
            BOOTSTRAP_PATH = BOOTSTRAP_PATH.size() ? BOOTSTRAP_PATH : "prop/bootstrap.xml";

            // read configuration
            auto ea3_config = avs::core::config_read(ea3_config_name, 0x40000);
            auto app_config = fileutils::file_exists(APP_PATH.c_str())
                    ? avs::core::config_read(APP_PATH.c_str())
                    : avs::core::config_read_string("<param/>");

            // get nodes
            auto ea3 = avs::core::property_search_safe(ea3_config, nullptr, "/ea3");
            auto ea3_id = avs::core::property_search(ea3_config, nullptr, "/ea3/id");
            auto ea3_id_hard = avs::core::property_search(ea3_config, nullptr, "/ea3/id/hardid");
            auto ea3_id_soft = avs::core::property_search(ea3_config, nullptr, "/ea3/id/softid");
            auto ea3_id_account = avs::core::property_search(ea3_config, nullptr, "/ea3/id/accountid");
            auto ea3_soft = avs::core::property_search(ea3_config, nullptr, "/ea3/soft");
            auto ea3_network = avs::core::property_search_safe(ea3_config, nullptr, "/ea3/network");

            // node values
            char EA3_PCBID[21] { 0 };
            char EA3_HARDID[21] { 0 };
            char EA3_SOFTID[21] { 0 };
            char EA3_ACCOUNTID[21] { 0 };
            char EA3_MODEL[4] { 0 };
            char EA3_DEST[2] { 0 };
            char EA3_SPEC[2] { 0 };
            char EA3_REV[2] { 0 };
            char EA3_EXT[11] { 0 };

            // read id nodes
            if (ea3_id != nullptr) {
                avs::core::property_node_refer(ea3_config, ea3_id, "pcbid",
                        avs::core::NODE_TYPE_str, EA3_PCBID, 21);
                avs::core::property_node_refer(ea3_config, ea3_id, "hardid",
                        avs::core::NODE_TYPE_str, EA3_HARDID, 21);
                avs::core::property_node_refer(ea3_config, ea3_id, "softid",
                        avs::core::NODE_TYPE_str, EA3_SOFTID, 21);
                avs::core::property_node_refer(ea3_config, ea3_id, "accountid",
                        avs::core::NODE_TYPE_str, EA3_ACCOUNTID, 21);
            }

            // set hard ID
            if (ea3_id_hard == nullptr) {
                strncpy(EA3_HARDID, "0100DEADBEEF", sizeof(EA3_HARDID));
                EA3_HARDID[20] = '\0';
                avs::core::property_node_create(ea3_config, nullptr,
                        avs::core::NODE_TYPE_str, "/ea3/id/hardid", EA3_HARDID);
            }

            // set soft ID
            if (ea3_id_soft == nullptr) {
                strncpy(EA3_SOFTID, "012199999999", sizeof(EA3_SOFTID));
                EA3_SOFTID[20] = '\0';
                avs::core::property_node_create(ea3_config, nullptr,
                        avs::core::NODE_TYPE_str, "/ea3/id/softid", EA3_SOFTID);
            }

            // read software nodes
            if (ea3_soft != nullptr) {
                avs::core::property_node_refer(ea3_config, ea3_soft, "model",
                        avs::core::NODE_TYPE_str, EA3_MODEL, 4);
                avs::core::property_node_refer(ea3_config, ea3_soft, "dest",
                        avs::core::NODE_TYPE_str, EA3_DEST, 2);
                avs::core::property_node_refer(ea3_config, ea3_soft, "spec",
                        avs::core::NODE_TYPE_str, EA3_SPEC, 2);
                avs::core::property_node_refer(ea3_config, ea3_soft, "rev",
                        avs::core::NODE_TYPE_str, EA3_REV, 2);
                avs::core::property_node_refer(ea3_config, ea3_soft, "ext",
                        avs::core::NODE_TYPE_str, EA3_EXT, 11);
            } else if (fileutils::file_exists("prop/ea3-ident.xml")) {

                // read ident config
                auto ea3_ident = avs::core::config_read("prop/ea3-ident.xml");
                if (ea3_ident == nullptr) {
                    log_fatal("avs-ea3", "'prop/ea3-ident.xml' could not be read as property list");
                }

                ea3_soft = avs::core::property_search_safe(ea3_ident, nullptr, "/ea3_conf/soft");

                avs::core::property_node_refer(ea3_ident, ea3_soft, "model",
                        avs::core::NODE_TYPE_str, EA3_MODEL, 4);
                avs::core::property_node_refer(ea3_ident, ea3_soft, "dest",
                        avs::core::NODE_TYPE_str, EA3_DEST, 2);
                avs::core::property_node_refer(ea3_ident, ea3_soft, "spec",
                        avs::core::NODE_TYPE_str, EA3_SPEC, 2);
                avs::core::property_node_refer(ea3_ident, ea3_soft, "rev",
                        avs::core::NODE_TYPE_str, EA3_REV, 2);
                avs::core::property_node_refer(ea3_ident, ea3_soft, "ext",
                        avs::core::NODE_TYPE_str, EA3_EXT, 11);

                // clean up
                avs::core::config_destroy(ea3_ident);
            } else {
                log_fatal("avs-ea3", "node not found in '{}': /ea3/soft", ea3_config_name);
            }

            // set account id (`EA3_PCBID` is valid if and only if `/ea3/id` is present)
            if (ea3_id != nullptr && ea3_id_account == nullptr) {
                const char *id = strcmp(EA3_MODEL, "M32") == 0 ? EA3_PCBID : "012018008135";

                strncpy(EA3_ACCOUNTID, id, sizeof(EA3_ACCOUNTID));
                EA3_ACCOUNTID[20] = '\0';

                avs::core::property_node_create(ea3_config, nullptr,
                        avs::core::NODE_TYPE_str, "/ea3/id/accountid", EA3_ACCOUNTID);
            }

            // replace ext code with release_code from bootstrap.xml if it is a newer date
            if (fileutils::file_exists(BOOTSTRAP_PATH.c_str())) {

                // read config
                auto bootstrap = avs::core::config_read(BOOTSTRAP_PATH.c_str(), 0, true);
                if (bootstrap == nullptr) {

                    // bootstrap.xml may be encrypted
                    log_warning("avs-ea3", "'{}' could not be read as property list", BOOTSTRAP_PATH);

                } else {

                    // get release code
                    char release_code[11] { 0 };
                    avs::core::property_node_refer(bootstrap, nullptr, "/config/release_code",
                            avs::core::NODE_TYPE_str, release_code, 11);

                    // compare dates
                    if (atoi(release_code) > atoi(EA3_EXT)) {
                        log_info("avs-ea3", "overwriting ext {} with {} from {}", EA3_EXT,
                                 release_code, BOOTSTRAP_PATH);
                        strncpy(EA3_EXT, release_code, 11);
                    }

                    // clean up
                    avs::core::config_destroy(bootstrap);
                }
            }

            // fall back to default PCBID if node is not found
            if (!EA3_PCBID[0] && PCBID_CUSTOM.empty()) {
                log_warning("avs-ea3", "no PCBID set, falling back to default PCBID value (04040000000000000000)");
                PCBID_CUSTOM = "04040000000000000000";
            }

            // custom PCBID
            if (!PCBID_CUSTOM.empty()) {

                // copy ID
                strncpy(EA3_PCBID, PCBID_CUSTOM.c_str(), sizeof(EA3_PCBID));
                EA3_PCBID[20] = '\0';

                // set nodes
                avs::core::property_search_remove_safe(ea3_config, nullptr, "/ea3/id/pcbid");
                avs::core::property_node_create(ea3_config, nullptr,
                        avs::core::NODE_TYPE_str, "/ea3/id/pcbid", &EA3_PCBID);

                if (ACCOUNTID_CUSTOM.empty() && strcmp(EA3_MODEL, "M32") == 0) {
                    ACCOUNTID_CUSTOM = PCBID_CUSTOM;
                }
            }

            // custom SOFTID
            if (!SOFTID_CUSTOM.empty()) {

                // copy ID
                strncpy(EA3_SOFTID, SOFTID_CUSTOM.c_str(), sizeof(EA3_SOFTID));
                EA3_SOFTID[20] = '\0';

                // set nodes
                avs::core::property_search_remove_safe(ea3_config, nullptr, "/ea3/id/softid");
                avs::core::property_node_create(ea3_config, nullptr,
                        avs::core::NODE_TYPE_str, "/ea3/id/softid", &EA3_SOFTID);
            }

            // custom ACCOUNTID
            if (!ACCOUNTID_CUSTOM.empty()) {

                // copy ID
                strncpy(EA3_ACCOUNTID, ACCOUNTID_CUSTOM.c_str(), sizeof(EA3_ACCOUNTID));
                EA3_ACCOUNTID[20] = '\0';

                // set nodes
                avs::core::property_search_remove_safe(ea3_config, nullptr, "/ea3/id/accountid");
                avs::core::property_node_create(ea3_config, nullptr,
                        avs::core::NODE_TYPE_str, "/ea3/id/accountid", &EA3_ACCOUNTID);
            }

            // check if PCBID is defined - should never hit, left for sanity check only
            if (avs::core::property_search(ea3_config, nullptr, "/ea3/id/pcbid") == nullptr) {
                log_fatal("avs-ea3", "node not found: /ea3/id/pcbid (try using -p to specify PCBID)");
            } else if (strlen(EA3_PCBID) == 0) {
                log_fatal("avs-ea3", "no PCBID set (try using -p to specify PCBID)");
            }

            // remember IDs
            char pcbid_buffer[256] { 0 };
            char accountid_buffer[256] { 0 };
            avs::core::property_node_refer(ea3_config, nullptr, "/ea3/id/pcbid",
                    avs::core::NODE_TYPE_str, pcbid_buffer, sizeof(pcbid_buffer));
            avs::core::property_node_refer(ea3_config, nullptr, "/ea3/id/accountid",
                    avs::core::NODE_TYPE_str, accountid_buffer, sizeof(accountid_buffer));
            EA3_BOOT_PCBID = std::string(pcbid_buffer);
            EA3_BOOT_ACCOUNTID = std::string(accountid_buffer);

            // build security code
            std::ostringstream security_code;
            security_code << "G";
            if (strcmp(EA3_MODEL, "KK9") == 0) {
                // Q (Spec F) is FullHD cabinet, E is OLD cabinet.
                security_code << (strcmp(EA3_SPEC, "F") == 0 ? "Q" : "E");
            } else if (strcmp(EA3_MODEL, "NCG") == 0) {
                // we have Q or E as choice again, see prop/code-config.xml
                security_code << "Q";
            } else if (strcmp(EA3_MODEL, "KBI") == 0) {
                // seems to be required to be set to E
                security_code << "E";
            } else if (strcmp(EA3_MODEL, "KCK") == 0 || strcmp(EA3_MODEL, "NCK") == 0) {
                // unsure if it really makes a difference
                security_code << "E";
            } else if (strcmp(EA3_MODEL, "LA9") == 0) {
                // GQ---J(spec)- in bootstrap.xml
                security_code << "Q";
            } else {
                security_code << "*";
            }
            security_code << EA3_MODEL;
            security_code << EA3_DEST;
            security_code << EA3_SPEC;
            security_code << EA3_REV;
            std::string security_code_str = security_code.str();
            log_info("avs-ea3", "security code: {}", security_code_str);

            // pre game init soft ID code
            std::ostringstream soft_id_code_pre_init;
            soft_id_code_pre_init << EA3_MODEL << ":";
            soft_id_code_pre_init << EA3_DEST << ":";
            soft_id_code_pre_init << EA3_SPEC << ":";
            soft_id_code_pre_init << EA3_REV << ":";
            soft_id_code_pre_init << EA3_EXT;
            std::string soft_id_code_pre_init_str = soft_id_code_pre_init.str();

            // set env variables
            avs::core::avs_std_setenv("/env/boot/build", VERSION_STRING);
            avs::core::avs_std_setenv("/env/boot/version", "SPICETOOLS");
            avs::core::avs_std_setenv("/env/boot/tag", "SPICETOOLS");
            avs::core::avs_std_setenv("/env/profile/security_code", security_code_str.c_str());
            avs::core::avs_std_setenv("/env/profile/secplug_b_security_code", security_code_str.c_str());
            avs::core::avs_std_setenv("/env/profile/system_id", EA3_PCBID);
            avs::core::avs_std_setenv("/env/profile/hardware_id", EA3_HARDID);
            avs::core::avs_std_setenv("/env/profile/license_id", EA3_SOFTID);
            avs::core::avs_std_setenv("/env/profile/software_id", EA3_SOFTID);
            avs::core::avs_std_setenv("/env/profile/account_id", EA3_ACCOUNTID);
            avs::core::avs_std_setenv("/env/profile/soft_id_code", soft_id_code_pre_init_str.c_str());

            // build game init code
            std::ostringstream init_code;
            init_code << EA3_MODEL;
            init_code << EA3_DEST;
            init_code << EA3_SPEC;
            init_code << EA3_REV;
            init_code << EA3_EXT;
            std::string init_code_str = init_code.str();

            // save game info
            memcpy(avs::game::MODEL, EA3_MODEL, 4);
            memcpy(avs::game::DEST, EA3_DEST, 2);
            memcpy(avs::game::SPEC, EA3_SPEC, 2);
            memcpy(avs::game::EXT, EA3_EXT, 11);

            // hook AVS functions
            hooks::avs::init();

            // update pcb_type in app-config
            if (PCB_TYPE >= 0) {
                if (strcmp(EA3_MODEL, "K39") == 0 || strcmp(EA3_MODEL, "L39") == 0) {
                    avs::core::property_search_remove_safe(app_config, nullptr, "/param/pcb_type_e");
                    avs::core::property_node_create(app_config, nullptr,
                        avs::core::NODE_TYPE_u8, "/param/pcb_type_e", PCB_TYPE);
                } else {
                    avs::core::property_search_remove_safe(app_config, nullptr, "/param/pcb_type");
                    avs::core::property_node_create(app_config, nullptr,
                        avs::core::NODE_TYPE_u8, "/param/pcb_type", PCB_TYPE);
                }
            }

            auto app_param = avs::core::property_search_safe(app_config, nullptr, "/param");

            // call the game init
            log_info("avs-ea3", "calling entry init");
            if (!avs::game::entry_init(init_code_str.data(), app_param)) {
                log_fatal("avs-ea3", "entry init failed :(");
            }

            // accommodate changes to soft id code
            //
            // TODO(felix): test this with other games, feature gating at the moment
            // for proper reporting of Omnimix and other song packs
            if (_stricmp(EA3_MODEL, "LDJ") == 0 ||
                _stricmp(EA3_MODEL, "L44") == 0 ||
                _stricmp(EA3_MODEL, "M39") == 0 ||
                _stricmp(EA3_MODEL, "KFC") == 0)
            {
                //memcpy(EA3_MODEL, init_code_str.c_str(), 3);
                //EA3_DEST[0] = init_code_str[3];
                //EA3_SPEC[0] = init_code_str[4];
                EA3_REV[0] = init_code_str[5];
                //memcpy(EA3_EXT, init_code_str.c_str() + 6, 10);
            }

            // remove nodes
            avs::core::property_search_remove_safe(ea3_config, nullptr, "/ea3/soft/model");
            avs::core::property_search_remove_safe(ea3_config, nullptr, "/ea3/soft/dest");
            avs::core::property_search_remove_safe(ea3_config, nullptr, "/ea3/soft/spec");
            avs::core::property_search_remove_safe(ea3_config, nullptr, "/ea3/soft/rev");
            avs::core::property_search_remove_safe(ea3_config, nullptr, "/ea3/soft/ext");

            // create nodes
            avs::core::property_node_create(ea3_config, nullptr,
                    avs::core::NODE_TYPE_str, "/ea3/soft/model", EA3_MODEL);
            avs::core::property_node_create(ea3_config, nullptr,
                    avs::core::NODE_TYPE_str, "/ea3/soft/dest", EA3_DEST);
            avs::core::property_node_create(ea3_config, nullptr,
                    avs::core::NODE_TYPE_str, "/ea3/soft/spec", EA3_SPEC);
            avs::core::property_node_create(ea3_config, nullptr,
                    avs::core::NODE_TYPE_str, "/ea3/soft/rev", EA3_REV);
            avs::core::property_node_create(ea3_config, nullptr,
                    avs::core::NODE_TYPE_str, "/ea3/soft/ext", EA3_EXT);

            // create soft ID code
            std::ostringstream soft_id_code;
            soft_id_code << EA3_MODEL << ":";
            soft_id_code << EA3_DEST << ":";
            soft_id_code << EA3_SPEC << ":";
            soft_id_code << EA3_REV << ":";
            soft_id_code << EA3_EXT;
            std::string soft_id_code_str = soft_id_code.str();
            log_info("avs-ea3", "soft id code: {}", soft_id_code_str);

            // set soft ID code
            avs::core::avs_std_setenv("/env/profile/soft_id_code", soft_id_code_str.c_str());

            // save new rev
            memcpy(avs::game::REV, EA3_REV, 2);

            // http11
            if (HTTP11 >= 0) {
                avs::core::property_search_remove_safe(ea3_config, ea3_network, "http11");
                avs::core::property_node_create(ea3_config, ea3_network,
                        avs::core::NODE_TYPE_bool, "http11", HTTP11);
            }

            // url slash
            if (URL_SLASH >= 0) {
                avs::core::property_search_remove_safe(ea3_config, ea3_network, "url_slash");
                avs::core::property_node_create(ea3_config, ea3_network,
                        avs::core::NODE_TYPE_bool, "url_slash", URL_SLASH);
            }

            // custom service url
            if (!URL_CUSTOM.empty()) {
                avs::core::property_search_remove_safe(ea3_config, ea3_network, "services");
                avs::core::property_node_create(ea3_config, ea3_network,
                        avs::core::NODE_TYPE_str, "services", URL_CUSTOM.c_str());
            }

            // server - replace URL on the fly
            if (easrv_port != 0u) {
                std::ostringstream url;
                url << "http://localhost:" << easrv_port << "/";
                std::string url_str = url.str();

                avs::core::property_search_remove_safe(ea3_config, ea3_network, "services");
                avs::core::property_node_create(ea3_config, ea3_network,
                        avs::core::NODE_TYPE_str, "services", url_str.c_str());
            }

            // remember URL
            char url_buffer[512] {};
            avs::core::property_node_refer(ea3_config, nullptr, "/ea3/network/services",
                    avs::core::NODE_TYPE_str, url_buffer, sizeof(url_buffer));
            EA3_BOOT_URL = std::string(url_buffer);

            // ssl initialization
            if (string_begins_with(url_buffer, "https")) {

                // load ssl module
                HMODULE kws = libutils::try_library("kws.dll");
                if (kws != nullptr) {

                    // get functions
                    ssl_protocol_init = libutils::try_proc<ssl_protocol_init_t>(kws, "ssl_protocol_init");
                    ssl_protocol_fini = libutils::try_proc<ssl_protocol_fini_t>(kws, "ssl_protocol_fini");

                    // initialize
                    if (ssl_protocol_init != nullptr) {
                        log_info("avs-ea3", "initializing SSL protocol handler");
                        ssl_protocol_init();
                    }
                }
            }

            // smartea logic: check if services are dead
            if (easrv_smart && !smartea::check_url(EA3_BOOT_URL)) {
                log_info("avs-ea3", "starting smartea local server on port 8080");

                // start server
                easrv_start(8080, easrv_maint, 4, 8);

                // fix URL
                EA3_BOOT_URL = "http://localhost:8080";
                avs::core::property_search_remove_safe(ea3_config, ea3_network, "services");
                avs::core::property_node_create(ea3_config, ea3_network,
                        avs::core::NODE_TYPE_str, "services", EA3_BOOT_URL.c_str());

                // fix URL slash
                URL_SLASH = 1;
                avs::core::property_search_remove_safe(ea3_config, ea3_network, "url_slash");
                avs::core::property_node_create(ea3_config, ea3_network,
                        avs::core::NODE_TYPE_bool, "url_slash", &URL_SLASH);
            }

            // boot EA3
            logger::PCBIDFilter filter;
            log_info("avs-ea3", "calling ea3 boot");
            avs_ea3_boot_startup(ea3);

            // clean up
            avs::core::config_destroy(app_config);
            avs::core::config_destroy(ea3_config);

            // print avs mountpoints
            // it does not exist in VERY old legacy AVS versions (like 2.10.2)
            if (avs::core::avs_fs_dump_mountpoint) {
                avs::core::avs_fs_dump_mountpoint();
            }

            // success
            log_info("avs-ea3", "boot done");
        }

        void shutdown() {

            // SSL shutdown
            if (ssl_protocol_fini != nullptr) {
                log_info("avs-ea3", "unregistering SSL protocol handler");
                ssl_protocol_fini();
            }

            // EA3 shutdown
            log_info("avs-ea3", "shutdown");
            avs_ea3_shutdown();
        }
    }
}
