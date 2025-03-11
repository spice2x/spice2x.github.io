#pragma once

#include <initializer_list>
#include <string>

#include <windows.h>

namespace avs {

    /*
     * helpers
     */
    namespace game {

        // properties
        extern char MODEL[4];
        extern char DEST[2];
        extern char SPEC[2];
        extern char REV[2];
        extern char EXT[11];

        // handle
        extern HINSTANCE DLL_INSTANCE;
        extern std::string DLL_NAME;

        // helpers
        bool is_model(const char *model);
        bool is_model(const char *model, const char *ext);
        bool is_model(const std::initializer_list<const char *> model_list);
        bool is_ext(const char *ext);
        bool is_ext(int datecode_min, int datecode_max);
        std::string get_identifier();

        // functions
        void load_dll();

        /*
         * library functions
         */

        bool entry_init(char *sid_code, void *app_param);
        void entry_main();
    }
}
