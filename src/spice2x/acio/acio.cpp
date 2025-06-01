#include "acio.h"

#include <fstream>
#include <iostream>

#include <windows.h>

#include "avs/game.h"
#include "cfg/config.h"
#include "cfg/api.h"
#include "hooks/libraryhook.h"
#include "misc/eamuse.h"
#include "util/fileutils.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/utils.h"

#include "bi2a/bi2a.h"
#include "bmpu/bmpu.h"
#include "core/core.h"
#include "hbhi/hbhi.h"
#include "hdxs/hdxs.h"
#include "hgth/hgth.h"
#include "i36g/i36g.h"
#include "i36i/i36i.h"
#include "icca/icca.h"
#include "j32d/j32d.h"
#include "kfca/kfca.h"
#include "klpa/klpa.h"
#include "mdxf/mdxf.h"
#include "nddb/nddb.h"
#include "panb/panb.h"
#include "pix/pix.h"
#include "pjec/pjec.h"
#include "pjei/pjei.h"
#include "la9a/la9a.h"

#include "module.h"

// globals
namespace acio {
    HINSTANCE DLL_INSTANCE = nullptr;
    std::vector<acio::ACIOModule *> MODULES;
    std::atomic<bool> IO_INIT_IN_PROGRESS = false;
}

/*
 * decide on hook mode used
 * libacio compiled using ICC64 sometimes doesn't leave enough space to insert the inline hooks
 * in this case, we want to use IAT instead
 */
static inline acio::HookMode get_hookmode() {
#ifdef SPICE64
    return acio::HookMode::IAT;
#else
    return acio::HookMode::INLINE;
#endif
}

void acio::attach() {
    log_info("acio", "SpiceTools ACIO");
    IO_INIT_IN_PROGRESS = true;

    // load settings and instance
    acio::DLL_INSTANCE = LoadLibraryA("libacio.dll");

    /*
     * library hook
     * some games have a second DLL laying around which gets loaded dynamically
     * we just give it the same instance as the normal one so the hooks still work
     */
    libraryhook_hook_library("libacioex.dll", acio::DLL_INSTANCE);
    libraryhook_hook_library("libacio_ex.dll", acio::DLL_INSTANCE);
    libraryhook_hook_library("libacio_old.dll", acio::DLL_INSTANCE);
    // libacioEx.dll for Road Fighters 3D
    // needed as comparisons in LoadLibrary hooks are case-sensitive
    libraryhook_hook_library("libacioEx.dll", acio::DLL_INSTANCE);
    libraryhook_enable(avs::game::DLL_INSTANCE);

    // get hook mode
    acio::HookMode hook_mode = get_hookmode();

    // load modules
    MODULES.push_back(new acio::BI2AModule(acio::DLL_INSTANCE, hook_mode));
    MODULES.push_back(new acio::BMPUModule(acio::DLL_INSTANCE, hook_mode));
    MODULES.push_back(new acio::CoreModule(acio::DLL_INSTANCE, hook_mode));
    MODULES.push_back(new acio::HBHIModule(acio::DLL_INSTANCE, hook_mode));
    MODULES.push_back(new acio::HDXSModule(acio::DLL_INSTANCE, hook_mode));
    MODULES.push_back(new acio::HGTHModule(acio::DLL_INSTANCE, hook_mode));
    MODULES.push_back(new acio::I36GModule(acio::DLL_INSTANCE, hook_mode));
    MODULES.push_back(new acio::I36IModule(acio::DLL_INSTANCE, hook_mode));
    MODULES.push_back(new acio::ICCAModule(acio::DLL_INSTANCE, hook_mode));
    MODULES.push_back(new acio::J32DModule(acio::DLL_INSTANCE, hook_mode));
    MODULES.push_back(new acio::KFCAModule(acio::DLL_INSTANCE, hook_mode));
    MODULES.push_back(new acio::KLPAModule(acio::DLL_INSTANCE, hook_mode));
    MODULES.push_back(new acio::MDXFModule(acio::DLL_INSTANCE, hook_mode));
    MODULES.push_back(new acio::NDDBModule(acio::DLL_INSTANCE, hook_mode));
    MODULES.push_back(new acio::PANBModule(acio::DLL_INSTANCE, hook_mode));
    MODULES.push_back(new acio::PJECModule(acio::DLL_INSTANCE, hook_mode));
    MODULES.push_back(new acio::PJEIModule(acio::DLL_INSTANCE, hook_mode));
    MODULES.push_back(new acio::LA9AModule(acio::DLL_INSTANCE, hook_mode));

    /*
     * PIX is special and needs another DLL.
     * we load that module only if the file exists.
     */
    if (fileutils::file_exists(MODULE_PATH / "libacio_pix.dll")) {
        HINSTANCE pix_instance = libutils::load_library(MODULE_PATH / "libacio_pix.dll");
        MODULES.push_back(new acio::PIXModule(pix_instance, hook_mode));
    }

    // apply modules
    for (auto &module : MODULES) {
        module->attach();
    }

    IO_INIT_IN_PROGRESS = false;
}

void acio::attach_icca() {
    log_info("acio", "SpiceTools ACIO ICCA");

    // load instance if needed
    if (!acio::DLL_INSTANCE) {
        acio::DLL_INSTANCE = LoadLibraryA("libacio.dll");
    }

    // get hook mode
    acio::HookMode hook_mode = get_hookmode();

    // load single module
    auto icca_module = new acio::ICCAModule(acio::DLL_INSTANCE, hook_mode);
    icca_module->attach();
    MODULES.push_back(icca_module);
}

void acio::detach() {

    // clear modules
    while (!MODULES.empty()) {
        delete MODULES.back();
        MODULES.pop_back();
    }
}
