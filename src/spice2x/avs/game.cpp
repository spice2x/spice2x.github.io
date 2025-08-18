#include "game.h"

#include "launcher/launcher.h"
#include "util/fileutils.h"
#include "util/libutils.h"
#include "util/logging.h"

namespace avs {

    namespace game {

        // function names
        const char ENTRY_INIT_NAME[] = "dll_entry_init";
        const char ENTRY_MAIN_NAME[] = "dll_entry_main";

        // functions
        typedef bool (*ENTRY_INIT_T)(char *, void *);
        typedef void (*ENTRY_MAIN_T)(void);
        ENTRY_INIT_T dll_entry_init;
        ENTRY_MAIN_T dll_entry_main;

        // properties
        char MODEL[4] = {'0', '0', '0', '\x00'};
        char DEST[2] = {'0', '\x00'};
        char SPEC[2] = {'0', '\x00'};
        char REV[2] = {'0', '\x00'};
        char EXT[11] = {'0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '\x00'};

        // handle
        HINSTANCE DLL_INSTANCE;
        std::string DLL_NAME;

        bool is_model(const char *model) {
            return _stricmp(MODEL, model) == 0;
        }

        bool is_model(const char *model, const char *ext) {
            return is_model(model) && is_ext(ext);
        }

        bool is_model(const std::initializer_list<const char *> model_list) {
            for (auto &model : model_list) {
                if (is_model(model)) {
                    return true;
                }
            }

            return false;
        }

        bool is_ext(const char *ext) {
            return _stricmp(EXT, ext) == 0;
        }

        bool is_ext(int datecode_min, int datecode_max) {

            // range check
            long datecode = strtol(EXT, NULL, 10);
            return datecode_min <= datecode && datecode <= datecode_max;
        }

        std::string get_identifier() {
            return fmt::format("{}:{}:{}:{}:{}",
                    avs::game::MODEL,
                    avs::game::DEST,
                    avs::game::SPEC,
                    avs::game::REV,
                    avs::game::EXT);
        }

        void load_dll() {
            log_info("avs-game", "loading DLL '{}'", DLL_NAME);

            // load game instance
            const auto dll_path = MODULE_PATH / DLL_NAME;
            const auto dll_path_s = dll_path.string();
            log_info("avs-game", "DLL path: {}", dll_path_s.c_str());

            // MAX_PATH is 260
            if (130 <= dll_path_s.length()) {
                log_warning(
                    "avs-game",
                    "PATH TOO LONG WARNING\n\n"
                    "-------------------------------------------------------------------\n"
                    "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
                    "WARNING - WARNING - WARNING - WARNING - WARNING - WARNING - WARNING\n"
                    "                           PATH TOO LONG                           \n"
                    "WARNING - WARNING - WARNING - WARNING - WARNING - WARNING - WARNING\n"
                    "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
                    "The path '{}'\n"
                    "    has a length of {}\n"
                    "Most of these games may behave unexpectedly when the path is too\n"
                    "long, often resulting in random crashes. Move the game contents to\n"
                    "a directory with shorter path.\n"
                    "-------------------------------------------------------------------\n\n",
                    dll_path_s, dll_path_s.length());
            }

            // ddr gamemdx.dll user error
            if (avs::game::is_model("MDX") && DLL_NAME == "gamemdx.dll") {
                log_fatal(
                    "ddr",
                    "BAD GAME DLL ERROR\n\n"
                    "!!!                                                            !!!\n"
                    "!!! -exec gamemdx.dll was specified                            !!!\n"
                    "!!! this is the wrong DLL; the game will not load              !!!\n"
                    "!!! remove -exec argument and try again.                       !!!\n"
                    "!!!                                                            !!!\n"
                    );
            }

            // file not found on disk
            if (!fileutils::file_exists(dll_path)) {
                log_warning("avs-game", "game DLL could not be found on disk: {}", dll_path.string().c_str());
                log_warning("avs-game", "double check -exec and -modules parameters; unless you know what you're doing, leave them blank");
            }

            if (fileutils::verify_header_pe(dll_path)) {
                DLL_INSTANCE = libutils::load_library(dll_path);
            }

            // load entry points
            dll_entry_init = (ENTRY_INIT_T) libutils::get_proc(DLL_INSTANCE, ENTRY_INIT_NAME);
            dll_entry_main = (ENTRY_MAIN_T) libutils::get_proc(DLL_INSTANCE, ENTRY_MAIN_NAME);
            log_info("avs-game", "loaded {} successfully ({})", DLL_NAME, fmt::ptr(DLL_INSTANCE));
        }

        bool entry_init(char *sid_code, void *app_param) {
            auto current_entry_init = (ENTRY_INIT_T) libutils::get_proc(DLL_INSTANCE, ENTRY_INIT_NAME);

            if (dll_entry_init != current_entry_init) {
                log_info("avs-game", "dll_entry_init is hooked");

                dll_entry_init = current_entry_init;
            }

            return dll_entry_init(sid_code, app_param);
        }

        void entry_main() {
            auto current_entry_main = (ENTRY_MAIN_T) libutils::get_proc(DLL_INSTANCE, ENTRY_MAIN_NAME);

            if (dll_entry_main != current_entry_main) {
                log_info("avs-game", "dll_entry_main is hooked");

                dll_entry_main = current_entry_main;
            }

            dll_entry_main();
        }
    }
}
