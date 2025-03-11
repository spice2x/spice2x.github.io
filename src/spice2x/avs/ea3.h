#pragma once

#include <string>

#include <windows.h>

namespace avs {

    /*
     * helpers
     */
    namespace ea3 {

        // import mapping
        struct avs_ea3_import {
            const char *version;
            const char *boot;
            const char *shutdown;
        };

        // settings
        extern std::string CFG_PATH;
        extern std::string APP_PATH;
        extern std::string BOOTSTRAP_PATH;
        extern std::string PCBID_CUSTOM;
        extern std::string SOFTID_CUSTOM;
        extern std::string URL_CUSTOM;
        extern int HTTP11;
        extern int URL_SLASH;
        extern int PCB_TYPE;

        // handle
        extern std::string VERSION_STR;
        extern HINSTANCE DLL_INSTANCE;
        extern std::string DLL_NAME;
        extern std::string EA3_BOOT_URL;
        extern std::string EA3_BOOT_PCBID;

        // functions
        void load_dll();
        void boot(unsigned short easrv_port, bool easrv_maint, bool easrv_smart);
        void shutdown();
    }

    /*
     * library functions
     */

    typedef int (*AVS_EA3_BOOT_STARTUP_T)(void *);
    extern AVS_EA3_BOOT_STARTUP_T avs_ea3_boot_startup;

    typedef void (*AVS_EA3_SHUTDOWN_T)(void);
    extern AVS_EA3_SHUTDOWN_T avs_ea3_shutdown;
}
