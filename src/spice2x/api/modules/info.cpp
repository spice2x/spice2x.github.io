#include "info.h"
#include <functional>
#include <iomanip>
#include "external/rapidjson/document.h"
#include "avs/game.h"
#include "avs/ea3.h"
#include "util/logging.h"
#include "util/utils.h"
#include "util/memutils.h"
#include "build/defs.h"

using namespace std::placeholders;
using namespace rapidjson;


namespace api::modules {

    Info::Info() : Module("info") {
        functions["avs"] = std::bind(&Info::avs, this, _1, _2);
        functions["launcher"] = std::bind(&Info::launcher, this, _1, _2);
        functions["memory"] = std::bind(&Info::memory, this, _1, _2);
    }

    /**
     * avs()
     */
    void Info::avs(Request &req, Response &res) {

        // get allocator
        auto &alloc = res.doc()->GetAllocator();

        // build info object
        Value info(kObjectType);
        info.AddMember("model", StringRef(avs::game::MODEL, 3), alloc);
        info.AddMember("dest", StringRef(avs::game::DEST, 1), alloc);
        info.AddMember("spec", StringRef(avs::game::SPEC, 1), alloc);
        info.AddMember("rev", StringRef(avs::game::REV, 1), alloc);
        info.AddMember("ext", StringRef(avs::game::EXT, 10), alloc);
        info.AddMember("services", StringRef(avs::ea3::EA3_BOOT_URL.c_str()), alloc);

        // add info object
        res.add_data(info);
    }

    /**
     * launcher()
     */
    void Info::launcher(Request &req, Response &res) {

        // get allocator
        auto &alloc = res.doc()->GetAllocator();

        // build args
        Value args(kArrayType);
        for (int count = 0; count < LAUNCHER_ARGC; count++) {
            auto arg = LAUNCHER_ARGV[count];
            args.PushBack(StringRef(arg), alloc);
        }

        // get system time
        auto t_now = std::time(nullptr);
        auto tm_now = *std::gmtime(&t_now);
        auto tm_str = to_string(std::put_time(&tm_now, "%Y-%m-%dT%H:%M:%SZ"));
        Value system_time(tm_str.c_str(), alloc);

        // build info object
        Value info(kObjectType);
        info.AddMember("version", StringRef(VERSION_STRING), alloc);
        info.AddMember("compile_date", StringRef(__DATE__), alloc);
        info.AddMember("compile_time", StringRef(__TIME__), alloc);
        info.AddMember("system_time", system_time, alloc);
        info.AddMember("args", args, alloc);

        // add info object
        res.add_data(info);
    }

    /**
     * memory()
     */
    void Info::memory(Request &req, Response &res) {

        // get allocator
        auto &alloc = res.doc()->GetAllocator();

        // build info object
        Value info(kObjectType);
        info.AddMember("mem_total", memutils::mem_total(), alloc);
        info.AddMember("mem_total_used", memutils::mem_total_used(), alloc);
        info.AddMember("mem_used", memutils::mem_used(), alloc);
        info.AddMember("vmem_total", memutils::vmem_total(), alloc);
        info.AddMember("vmem_total_used", memutils::vmem_total_used(), alloc);
        info.AddMember("vmem_used", memutils::vmem_used(), alloc);

        // add info object
        res.add_data(info);
    }
}
