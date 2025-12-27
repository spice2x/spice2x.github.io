#include "avshook.h"

#include <cassert>
#include <mutex>
#include <map>
#include <optional>
#include <external/robin_hood.h>

#include "avs/core.h"
#include "avs/ea3.h"
#include "avs/game.h"
#include "util/detour.h"
#include "util/fileutils.h"
#include "util/logging.h"
#include "util/utils.h"

#ifdef min
#undef min
#endif

static bool FAKE_FILE_OPEN = false;

static std::map<std::string, std::string> ROM_FILE_MAP;
static std::string *ROM_FILE_CONTENTS = nullptr;

static std::mutex ldj_thumb_stat_cache_mutex;
static robin_hood::unordered_map<std::string, std::pair<int, struct avs::core::avs_stat>> ldj_thumb_stat_cache;

namespace hooks::avs::config {
    bool DISABLE_VFS_DRIVE_REDIRECTION = false;
    bool DISABLE_CACHE = false;
    bool LOG = false;
};

using namespace hooks::avs;

#define WRAP_DEBUG_FMT(format, ...) \
    if (config::LOG) { \
        log_misc("avshook", "{}: " format " = 0x{:x}",  __FUNCTION__, __VA_ARGS__, static_cast<unsigned>(value)); \
    }
#define AVS_HOOK(f) hook_function(::avs::core::IMPORT_NAMES.f, #f, &::avs::core::f, f)

template<typename T>
static void hook_function(const char *source_name, const char *target_name, T **source, T *target) {
    if (!detour::trampoline_try(avs::core::DLL_NAME.c_str(), source_name, target, source)) {
        log_warning("avshook", "could not hook {} ({})", target_name, source_name);
    }
}

static inline bool is_fake_fd(avs::core::avs_file_t fd) {
    return FAKE_FILE_OPEN && fd == 1337;
}

static bool is_dest_file(const char *name) {
    static std::string path_dest = fmt::format("/dev/raw/{}.dest", avs::game::DEST[0]);
    static std::string path_bin = fmt::format("/dev/raw/{}.bin", avs::game::DEST[0]);

    return !_stricmp(name, path_dest.c_str()) || !_stricmp(name, path_bin.c_str());
}

static bool is_dest_file(const char *name, uint16_t mode) {
    return mode == 1 && is_dest_file(name);
}

static bool is_dest_spec_ea3_config(const char *name) {
    static std::string path = fmt::format("/prop/ea3-config-{}{}.xml", avs::game::DEST[0], avs::game::SPEC[0]);

    return !_stricmp(name, path.c_str());
}

static bool is_spam_file(const char *file) {
    static const char *spam_prefixes[] = {
        "/mnt/bm2d/ngp",
        "/afp",
        "/dev/nvram/pm_eco.xml",
        "/dev/nvram/pm_gamesys.xml",
        "/dev/nvram/pm_clock.xml",
    };

    for (auto &spam : spam_prefixes) {
        if (string_begins_with(file, spam)) {
            return true;
        }
    }

    return false;
}

static bool is_ldj_cacheable_file(const char *file) {
    static const char *spam_prefixes[] = {
        // iidx33: profile background customization, spams on every frame during card entry
        "/data/graphic/entry_card",
        // iidx32+: movie thumbnails, spams on every frame during song select
        "/data/graphic/thumbnail/",
        "/data/graphic/movie_thumbnail/",
    };

    for (auto &spam : spam_prefixes) {
        if (string_begins_with(file, spam)) {
            return true;
        }
    }
    return false;
}

static int cached_avs_fs_lstat(const char *name, struct avs::core::avs_stat *st) {
    assert(name != nullptr);
    assert(st != nullptr);

    const std::string filename(name);
    int value = 0;
    std::lock_guard<std::mutex> lock(ldj_thumb_stat_cache_mutex);

    // if this was already seen, return cached result
    if (ldj_thumb_stat_cache.contains(filename)) {
        const auto &result = ldj_thumb_stat_cache.at(filename);
        value = result.first;
        memcpy(st, &result.second, sizeof(*st));
        WRAP_DEBUG_FMT(
            "(cached) file={}, a={}, m={}, c={}, size={}, u={}",
            name,
            st->st_atime,
            st->st_mtime,
            st->st_ctime,
            st->filesize,
            st->unk1);
        return value;
    }

    // if not, call the original avs function
    // this returns 1 on success with valid values in st
    // return 0 on failure, with filesize set to 0
    // maybe this could be used for future optimization
    value = avs::core::avs_fs_lstat(name, st);
    WRAP_DEBUG_FMT(
        "file={} a={}, m={}, c={}, size={}, u={}",
        name,
        st->st_atime,
        st->st_mtime,
        st->st_ctime,
        st->filesize,
        st->unk1);

    // and cache it if there is room (prevent unconstrained growth)
    if (ldj_thumb_stat_cache.size() <= 4096) {
        // c++ was a mistake
        ldj_thumb_stat_cache.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(filename),
            std::forward_as_tuple(value, *st));
    }

    // done
    return value;
}

static int avs_fs_fstat(avs::core::avs_file_t fd, struct avs::core::avs_stat *st) {
    if (is_fake_fd(fd) && ROM_FILE_CONTENTS) {
        if (st) {
            st->filesize = static_cast<uint32_t>(ROM_FILE_CONTENTS->length());
            st->padding.st_dev = 0;
        }
        return 1;
    }

    return avs::core::avs_fs_fstat(fd, st);
}

static int avs_fs_lstat(const char *name, struct avs::core::avs_stat *st) {
    if (name == nullptr) {
        return avs::core::avs_fs_lstat(name, st);
    }

    if (is_dest_file(name) || is_dest_spec_ea3_config(name)) {
        if (st) {
            st->filesize = 0;
            st->padding.st_dev = 0;
        }
        return 1;
    }

    if (!hooks::avs::config::DISABLE_CACHE && (st != nullptr) &&
        avs::game::is_model("LDJ") && is_ldj_cacheable_file(name)) {
        return cached_avs_fs_lstat(name, st);
    }

    const auto value = avs::core::avs_fs_lstat(name, st);
    if (!is_spam_file(name)) {
        WRAP_DEBUG_FMT("name: {}", name);
    }

    return value;
}

static avs::core::avs_file_t avs_fs_open(const char *name, uint16_t mode, int flags) {
    if (name == nullptr) {
        return avs::core::avs_fs_open(name, mode, flags);
    }

    if (!FAKE_FILE_OPEN && (is_dest_file(name, mode) || ROM_FILE_MAP.contains(name))) {
        FAKE_FILE_OPEN = true;

        if (ROM_FILE_MAP.contains(name)) {
            ROM_FILE_CONTENTS = &ROM_FILE_MAP.at(name);
        }

        log_info("avshook", "opening fake file '{}'", name);

        return 1337;
    }

    auto value = avs::core::avs_fs_open(name, mode, flags);

    if (!is_spam_file(name)) {
        WRAP_DEBUG_FMT("name: {} mode: {} flags: {}", name, mode, flags);
    }

    return value;
}

static void avs_fs_close(avs::core::avs_file_t fd) {
    if (is_fake_fd(fd)) {
        FAKE_FILE_OPEN = false;
        ROM_FILE_CONTENTS = nullptr;

        log_info("hooks::avs", "closing fake fd");
    } else {
        avs::core::avs_fs_close(fd);
    }
}

static int avs_fs_copy(const char *sname, const char *dname) {
    if (sname == nullptr || dname == nullptr) {
        return avs::core::avs_fs_copy(sname, dname);
    }

    auto value = avs::core::avs_fs_copy(sname, dname);
    WRAP_DEBUG_FMT("sname: {} dname {}", sname, dname);

    return value;
}

static avs::core::avs_file_t avs_fs_opendir(const char *path) {
    if (path == nullptr) {
        return avs::core::avs_fs_opendir(path);
    }

    auto value = avs::core::avs_fs_opendir(path);
    WRAP_DEBUG_FMT("path: {}", path);

    return value;
}

static int avs_fs_mount(const char *mountpoint, const char *fsroot, const char *fstype, void *data) {
    // avs redirection breaks ongaku paradise
    if (mountpoint == nullptr ||
        fsroot == nullptr ||
        fstype == nullptr ||
        avs::game::is_model("JC9")) {
        return avs::core::avs_fs_mount(mountpoint, fsroot, fstype, data);
    }

    std::optional<std::string> new_fs_root = std::nullopt;

    if (_stricmp(mountpoint, "/mnt/ea3-config.xml") == 0 && is_dest_spec_ea3_config(fsroot)) {
        new_fs_root = fmt::format("/{}", avs::ea3::CFG_PATH);
    }

    // remap drive mounts to `dev/vfs/drive_x` where x is the drive letter
    if (!config::DISABLE_VFS_DRIVE_REDIRECTION &&
        (_strnicmp(fsroot, "d:", 2) == 0 ||
        _strnicmp(fsroot, "e:", 2) == 0 ||
        _strnicmp(fsroot, "f:", 2) == 0) &&
        _stricmp(fstype, "fs") == 0)
    {
        // sub path is everything after the drive and colon characters
        const char drive_letter[2] {
            static_cast<char>(std::tolower(static_cast<unsigned char>(fsroot[0]))),
            '\0',
        };
        const auto separator = fsroot[2] == '/' ? "" : "/";
        const auto sub_path = &fsroot[2];
        const std::filesystem::path mapped_path = fmt::format(
                "dev/vfs/drive_{}{}{}",
                drive_letter,
                separator,
                sub_path);

        // create the mapped directory path
        std::error_code err;
        std::filesystem::create_directories(mapped_path, err);

        if (err) {
            log_warning("hooks::avs", "failed to create '{}': {}", mapped_path.string(), err.message());
        } else {

            // if this is the `e:\`, then create the special directories
            if (drive_letter[0] == 'e' &&
                (sub_path[0] == '/' || sub_path[0] == '\\') &&
                sub_path[1] == '\0')
            {
                fileutils::dir_create_log("hooks::avs", mapped_path / "tmp");
                fileutils::dir_create_log("hooks::avs", mapped_path / "up");
            }

            log_misc("hooks::avs", "source directory '{}' remapped to '{}'",
                    fsroot,
                    mapped_path.string());
        }

        new_fs_root = mapped_path.string();
    }

    auto fs_root_data = new_fs_root.has_value() ? new_fs_root->c_str() : fsroot;
    auto value = avs::core::avs_fs_mount(mountpoint, fs_root_data, fstype, data);

    WRAP_DEBUG_FMT("mountpoint: {}, fsroot: {}, fstype: {}", mountpoint, fs_root_data, fstype);

    return value;
}

static size_t avs_fs_read(avs::core::avs_file_t fd, uint8_t *data, uint32_t data_size) {
    if (is_fake_fd(fd) && ROM_FILE_CONTENTS) {
        const auto size = std::min(static_cast<size_t>(data_size), ROM_FILE_CONTENTS->length());

        memcpy(data, ROM_FILE_CONTENTS->c_str(), size);

        return size;
    }

    return avs::core::avs_fs_read(fd, data, data_size);
}

static int property_file_write(avs::core::property_ptr prop, const char* path) {
    if (prop == nullptr || path == nullptr) {
        return avs::core::property_file_write(prop, path);
    }

    // resort anthem dumps eacoin to /dev/nvram/eacoin AND /eacoin.xml
    // it never reads /eacoin.xml, so it's probably a development leftover
    if (avs::game::is_model("JDZ") && _stricmp(path, "/eacoin.xml") == 0) {
        return 0;
    }

    auto value = avs::core::property_file_write(prop, path);

    WRAP_DEBUG_FMT("path: {}", path);

    return value;
}

void hooks::avs::init() {
    log_info("hooks::avs", "initializing");

    AVS_HOOK(avs_fs_fstat);
    AVS_HOOK(avs_fs_lstat);
    AVS_HOOK(avs_fs_open);
    AVS_HOOK(avs_fs_close);
    AVS_HOOK(avs_fs_copy);
    AVS_HOOK(avs_fs_opendir);
    AVS_HOOK(avs_fs_mount);
    AVS_HOOK(avs_fs_read);
    AVS_HOOK(property_file_write);
}

void hooks::avs::set_rom(const char *path, const char *contents) {
    ROM_FILE_MAP.emplace(path, contents);
}
