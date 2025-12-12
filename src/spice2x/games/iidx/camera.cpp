#include "camera.h"

#if SPICE64

#include <d3d9.h>
#include "mf_wrappers.h"
#include "avs/game.h"
#include "external/rapidjson/document.h"
#include "external/rapidjson/pointer.h"
#include "external/rapidjson/prettywriter.h"
#include "games/iidx/iidx.h"
#include "games/io.h"
#include "launcher/options.h"
#include "misc/eamuse.h"
#include "util/detour.h"
#include "util/fileutils.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/sigscan.h"
#include "util/utils.h"

static std::filesystem::path dll_path;
static HMODULE iidx_module;
static uintptr_t addr_hook_a = 0;
static uintptr_t addr_textures = 0;
static uintptr_t addr_camera_manager = 0;
static uintptr_t addr_device_offset = 0;
static uintptr_t addr_afp_texture_offset = 0;

typedef void **(__fastcall *camera_hook_a_t)(PBYTE);
static camera_hook_a_t camera_hook_a_orig = nullptr;
auto static hook_a_init = std::once_flag {};

static LPDIRECT3DDEVICE9EX device;
static LPDIRECT3DTEXTURE9 *camera_texture_a = nullptr;
static LPDIRECT3DTEXTURE9 *camera_texture_b = nullptr;
static LPDIRECT3DTEXTURE9 *preview_texture_a = nullptr;
static LPDIRECT3DTEXTURE9 *preview_texture_b = nullptr;

struct PredefinedHook {
    std::string     pe_identifier;
    uintptr_t       hook_a;
    uintptr_t       hook_textures;
    uintptr_t       hook_camera_manager;
    uintptr_t       hook_device_offset;
    uintptr_t       hook_afp_texture_offset;
};

PredefinedHook g_predefinedHooks[1] = {};

const DWORD   g_predefinedHooksLength = ARRAYSIZE(g_predefinedHooks);

namespace games::iidx {

    static IIDXLocalCamera *top_camera = nullptr;       // camera id #0
    static IIDXLocalCamera *front_camera = nullptr;     // camera id #1
    std::vector<IIDXLocalCamera*> LOCAL_CAMERA_LIST = {};
    static IDirect3DDeviceManager9 *s_pD3DManager = nullptr;
    std::filesystem::path CAMERA_CONFIG_PATH;
    bool CAMERA_READY = false;

    bool parse_cmd_params() {
        const auto remove_spaces = [](const char& c) {
            return c == ' ';
        };

        log_misc("iidx:camhook", "Using user-supplied addresses (-iidxcamhookoffset)");

        auto s = TDJ_CAMERA_OVERRIDE.value();
        s.erase(std::remove_if(s.begin(), s.end(), remove_spaces), s.end());

        if (sscanf(
                s.c_str(),
                "%i,%i,%i,%i,%i",
                (int*)&addr_hook_a, (int*)&addr_textures, (int*)&addr_camera_manager, (int*)&addr_device_offset, (int*)&addr_afp_texture_offset) == 5) {

            return true;

        } else {
            log_fatal("iidx:camhook", "failed to parse -iidxcamhookoffset");
            return false;
        }
    }

    bool find_camera_hooks() {
        std::string dll_name = avs::game::DLL_NAME;
        dll_path = MODULE_PATH / dll_name; 

        iidx_module = libutils::try_module(dll_path);
        if (!iidx_module) {
            log_warning("iidx:camhook", "failed to hook game DLL");
            return FALSE;
        }

        uint32_t time_date_stamp = 0;
        uint32_t address_of_entry_point = 0;

        // there are three different ways we try to hook the camera entry points
        // 1) user-provided override via cmd line
        // 2) predefined offsets using PE header matching
        // 3) signature match (should work for most iidx27-33)

        // method 1: user provided
        if (TDJ_CAMERA_OVERRIDE.has_value() && parse_cmd_params()) {
            return TRUE;
        }

        // method 2: predefined offsets
        get_pe_identifier(dll_path, &time_date_stamp, &address_of_entry_point);
        auto pe = fmt::format("{:x}_{:x}", time_date_stamp, address_of_entry_point);
        log_info("iidx:camhook", "Locating predefined hook addresses for LDJ-{}", pe);

        for (DWORD i = 0; i < g_predefinedHooksLength; i++) {
            if (pe.compare(g_predefinedHooks[i].pe_identifier) == 0) {
                log_misc("iidx:camhook", "Found predefined addresses");
                addr_hook_a             = g_predefinedHooks[i].hook_a;
                addr_textures           = g_predefinedHooks[i].hook_textures;
                addr_camera_manager     = g_predefinedHooks[i].hook_camera_manager;
                addr_device_offset      = g_predefinedHooks[i].hook_device_offset;
                addr_afp_texture_offset = g_predefinedHooks[i].hook_afp_texture_offset;
                return TRUE;
            }
        }

        // method 3: signature match
        log_info("iidx:camhook", "Did not find predefined hook address, try signature match");

        // --- addr_hook_a ---
        uint8_t *addr_hook_a_ptr = reinterpret_cast<uint8_t *>(find_pattern(
            iidx_module,
            "E800000000488B8F0000000048894D",
            "X????XXX????XXX",
            0, 0));

        if (addr_hook_a_ptr == nullptr) {
            log_warning("iidx:camhook", "failed to find hook: addr_hook_a");
            log_warning("iidx:camhook", "hint: this feature REQUIRES a compatible DLL!");
            return FALSE;
        }

        // displace with the content of wildcard bytes
        int32_t disp_a = *((int32_t *) (addr_hook_a_ptr + 1));

        addr_hook_a = (addr_hook_a_ptr - (uint8_t*) iidx_module) + disp_a + 5;

        // --- addr_textures ---
        uint8_t* addr_textures_ptr = reinterpret_cast<uint8_t *>(find_pattern(
            iidx_module,
            "E800000000488BC8488BD34883C420",
            "X????XXXXXXXXXX",
            0, 0));
        
        if (addr_textures_ptr == nullptr) {
            log_warning("iidx:camhook", "failed to find hook: addr_textures (part 1)");
            return FALSE;
        }

        // displace with the content of wildcard bytes
        int32_t disp_textures = *((int32_t *) (addr_textures_ptr + 1));
        addr_textures_ptr += disp_textures + 5;

        uint32_t search_from = (addr_textures_ptr - (uint8_t*) iidx_module);

        addr_textures_ptr = reinterpret_cast<uint8_t *>(find_pattern_from(
            iidx_module,
            "488D0D",
            "XXX",
            0, 0, search_from));

        if (addr_textures_ptr == nullptr) {
            log_warning("iidx:camhook", "failed to find hook: addr_textures (part 2)");
            return FALSE;
        }

        // displace again with the next integer
        disp_textures = *((int32_t *) (addr_textures_ptr + 3));

        addr_textures = (addr_textures_ptr - (uint8_t*) iidx_module) + disp_textures + 7;

        // --- addr_camera_manager ---
        uint8_t *addr_camera_manager_ptr = reinterpret_cast<uint8_t *>(find_pattern(
            iidx_module,
            "E80000000033FF488B58",
            "X????XXXXX",
            0, 0));

        if (addr_camera_manager_ptr == nullptr) {
            log_warning("iidx:camhook", "failed to find hook: addr_get_camera_manager");
            return FALSE;
        }

        // displace with the content of wildcard bytes
        int32_t disp_camera_manager = *((int32_t *) (addr_camera_manager_ptr + 1));

        addr_camera_manager_ptr = addr_camera_manager_ptr + disp_camera_manager + 5;

        // once more to get the final address
        disp_camera_manager = *((int32_t *) (addr_camera_manager_ptr + 3));

        addr_camera_manager = (addr_camera_manager_ptr - (uint8_t*) iidx_module) + disp_camera_manager + 7;

        // addr_device_offset
        search_from = (addr_hook_a_ptr - (uint8_t*) iidx_module);
        uint8_t *addr_device_ptr = reinterpret_cast<uint8_t *>(find_pattern_from(
                iidx_module,
                "488B89",
                "XXX",
                3, 0, search_from));

        if (addr_device_ptr == nullptr) {
            log_warning("iidx:camhook", "failed to find hook: addr_device_ptr");
            return FALSE;
        }

        addr_device_offset = *addr_device_ptr;

        // --- addr_afp_texture_offset ---
        uint8_t *addr_afp_texture_ptr = reinterpret_cast<uint8_t *>(find_pattern(
            iidx_module,
            "488B41004885C07400488D5424",
            "XXX?XXXX?XXXX",
            0, 0));

        if (addr_afp_texture_ptr == nullptr) {
            log_warning("iidx:camhook", "failed to find hook: addr_afp_texture_offset");
            return FALSE;
        }

        addr_afp_texture_offset = *(addr_afp_texture_ptr + 3);

        return TRUE;
    }

    static void **__fastcall camera_hook_a(PBYTE a1) {
        std::call_once(hook_a_init, [&]{           
            device = *reinterpret_cast<LPDIRECT3DDEVICE9EX*>(a1 + addr_device_offset);
            auto const preview = *reinterpret_cast<LPDIRECT3DTEXTURE9**>((uint8_t*)iidx_module + addr_textures);
            auto const manager = reinterpret_cast<Camera::CCameraManager2*>((uint8_t*)iidx_module + addr_camera_manager);

            camera_texture_a = manager->cameras->a->d3d9_texture(addr_afp_texture_offset);
            camera_texture_b = manager->cameras->b->d3d9_texture(addr_afp_texture_offset);
            preview_texture_a = preview;
            preview_texture_b = preview + 2;

            init_local_camera();
        });
        return camera_hook_a_orig(a1);
    }

    bool create_hooks() {
        auto *hook_a_ptr = reinterpret_cast<uint16_t *>(
                reinterpret_cast<intptr_t>(iidx_module) + addr_hook_a);

        if (!detour::trampoline(
                    reinterpret_cast<camera_hook_a_t>(hook_a_ptr),
                    camera_hook_a,
                    &camera_hook_a_orig)) {
            log_warning("iidx:camhook", "failed to trampoline hook_a");
            return FALSE;
        }

        return TRUE;
    }

    HRESULT create_d3d_device_manager() {
        UINT resetToken = 0;
        IDirect3DDeviceManager9 *pD3DManager = nullptr;

        HRESULT hr = DXVA2CreateDirect3DDeviceManager9(&resetToken, &pD3DManager);
        if (FAILED(hr)) { goto done; }

        hr = pD3DManager->ResetDevice(device, resetToken);
        if (FAILED(hr)) { goto done; }

        s_pD3DManager = pD3DManager;
        (s_pD3DManager)->AddRef();

    done:
        if (SUCCEEDED(hr)) {
            log_misc("iidx:camhook", "Created DeviceManager for DXVA");
        } else {
            log_warning("iidx:camhook", "Cannot create DXVA DeviceManager: {}", hr);
        }

        SafeRelease(&pD3DManager);
        return hr;
    }

    bool init_local_camera() {
        HRESULT hr = S_OK;
        IMFAttributes *pAttributes = nullptr;
        IMFActivate **ppDevices = nullptr;
        uint32_t numDevices;

        // Initialize an attribute store to specify enumeration parameters.
        hr = WrappedMFCreateAttributes(&pAttributes, 1);

        // Ask for source type = video capture devices.
        if (SUCCEEDED(hr)) {
            hr = pAttributes->SetGUID(
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, 
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
            );
        }
        
        // Enumerate devices.
        if (SUCCEEDED(hr)) {
            hr = WrappedMFEnumDeviceSources(pAttributes, &ppDevices, &numDevices);
        }

        log_info("iidx:camhook", "MFEnumDeviceSources returned {} device(s)", numDevices);
        if (numDevices > 0) {
            hr = create_d3d_device_manager();
        }

        if (SUCCEEDED(hr)) {

            // get options
            std::string top_camera_id("");
            std::string front_camera_id("");
            auto options = games::get_options(eamuse_get_game());
            if (options->at(launcher::Options::IIDXCamHookTopId).is_active()) {
                top_camera_id = options->at(launcher::Options::IIDXCamHookTopId).value_text();
            }
            if (options->at(launcher::Options::IIDXCamHookFrontId).is_active()) {
                front_camera_id = options->at(launcher::Options::IIDXCamHookFrontId).value_text();
            }
            int preferred_top_index = -1;
            int preferred_front_index = -1;

            log_info("iidx:camhook", "-------- Listing all cameras from MFEnumDeviceSources --------");

            // print camera names first for user
            for (size_t i = 0; i < numDevices; i++) {
                // get camera activation
                auto pActivate = ppDevices[i];

                // print friendly name
                PWCHAR friendly = nullptr;
                UINT32 friendly_len = 0;
                hr = pActivate->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                    &friendly,
                    &friendly_len);
                if (SUCCEEDED(hr) && friendly != nullptr) {
                    log_info("iidx:camhook", "Camera {} Name: {}", i, ws2s(friendly));
                    CoTaskMemFree(friendly);
                    friendly = nullptr;
                }

                // get symlink name
                PWCHAR symlink = nullptr;
                UINT32 symlink_len = 0;
                hr = pActivate->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                    &symlink,
                    &symlink_len);
                if (FAILED(hr) || symlink == nullptr) {
                    log_info("iidx:camhook", "Camera {}: failed to get vidcap symlink name", i);
                    continue;
                }

                const auto symlink_s = ws2s(symlink);
                log_info("iidx:camhook", "Camera {}   ID: {}", i, symlink_s);

                if (!top_camera_id.empty() && (symlink_s.find(top_camera_id) != std::string::npos)) {
                    preferred_top_index = i;
                } else if (!front_camera_id.empty() && (symlink_s.find(front_camera_id) != std::string::npos)) {
                    preferred_front_index = i;
                }

                if (symlink != nullptr) {
                    CoTaskMemFree(symlink);
                    symlink = nullptr;
                }
            }

            log_info("iidx:camhook", "--------    End of cameras from MFEnumDeviceSources   --------");

            // pick out preferred top/front cameras

            log_misc("iidx:camhook", "Adding user-preferred cameras");
            for (size_t i = 0; i < numDevices && LOCAL_CAMERA_LIST.size() < 2; i++) {
                auto pActivate = ppDevices[i];
                if ((int)i == preferred_top_index && top_camera == nullptr) {
                    log_misc("iidx:camhook", "Adding user-preferred top camera {} / '{}'", i, top_camera_id);
                    top_camera = add_top_camera(pActivate);
                } else if ((int)i == preferred_front_index && front_camera == nullptr) {
                    log_misc("iidx:camhook", "Adding user-preferred front camera {} / '{}'", i, front_camera_id);
                    front_camera = add_front_camera(pActivate);
                }
            }

            // fill out the rest of cameras
            log_misc("iidx:camhook", "Adding other cameras");
            for (size_t i = 0; i < numDevices && LOCAL_CAMERA_LIST.size() < 2; i++) {
                auto pActivate = ppDevices[i];

                if ((int)i == preferred_top_index || (int)i == preferred_front_index) {
                    // we already tried these above; don't use them here
                    continue;
                }

                if (!FLIP_CAMS) {
                    // top camera first, then front
                    if (top_camera == nullptr) {
                        top_camera = add_top_camera(pActivate);
                    } else if (front_camera == nullptr) {
                        front_camera = add_front_camera(pActivate);
                    }
                } else {
                    // front first, then top
                    if (front_camera == nullptr) {
                        front_camera = add_front_camera(pActivate);
                    } else if (top_camera == nullptr) {
                        top_camera = add_top_camera(pActivate);
                    }
                }
            }
        }

        if (LOCAL_CAMERA_LIST.size() == 0) {
            goto done;
        }

        camera_config_load();
        if (top_camera) {
            top_camera->StartCapture();
        }
        if (front_camera) {
            front_camera->StartCapture();
        }

        CAMERA_READY = true;

    done:
        SafeRelease(&pAttributes);

        for (DWORD i = 0; i < numDevices; i++) {
            SafeRelease(&ppDevices[i]);
        }
        CoTaskMemFree(ppDevices);

        return SUCCEEDED(hr);
    }

    IIDXLocalCamera* add_top_camera(IMFActivate* pActivate) {
        auto top_camera = new IIDXLocalCamera("top", TDJ_CAMERA_PREFER_16_9, pActivate, s_pD3DManager, device, camera_texture_a, preview_texture_a);
        if (top_camera->m_initialized) {
            LOCAL_CAMERA_LIST.push_back(top_camera);
        } else {
            top_camera->Release();
            top_camera = nullptr;
        }
        return top_camera;
    }

    IIDXLocalCamera* add_front_camera(IMFActivate* pActivate) {
        auto front_camera = new IIDXLocalCamera("front", TDJ_CAMERA_PREFER_16_9, pActivate, s_pD3DManager, device, camera_texture_b, preview_texture_b);
        if (front_camera->m_initialized) {
            LOCAL_CAMERA_LIST.push_back(front_camera);
        } else {
            front_camera->Release();
            front_camera = nullptr;
        }
        return front_camera;
    }

    bool init_camera_hooks() {
        bool result = find_camera_hooks();
        if (result) {
            log_misc(
                "iidx:camhook",
                "found hooks:\n    hook_a             0x{:x}\n    textures           0x{:x}\n    camera_manager     0x{:x}\n    device_offset      0x{:x}\n    afp_texture_offset 0x{:x}",
                addr_hook_a, addr_textures, addr_camera_manager, addr_device_offset, addr_afp_texture_offset);
            result = create_hooks();
            if (result) {
                init_mf_library();
            }
        }
        
        return result;
    }

    void camera_release() {
        if (top_camera) {
            top_camera->Release();
            top_camera = nullptr;
        }
        if (front_camera) {
            front_camera->Release();
            front_camera = nullptr;
        }
        SafeRelease(&s_pD3DManager);
    }

    bool camera_config_load() {
        CAMERA_CONFIG_PATH =
            fileutils::get_config_file_path("iidx::camhook", "spicetools_camera_control.json");

        try {
            // read config file
            std::string config = fileutils::text_read(CAMERA_CONFIG_PATH);
            if (!config.empty()) {
                // parse document
                rapidjson::Document doc;
                doc.Parse(config.c_str());

                // check parse error
                auto error = doc.GetParseError();
                if (error) {
                    log_warning("iidx:camhook", "config parse error: {}", error);
                    return false;
                }

                // verify root is a dict
                if (!doc.IsObject()) {
                    log_warning("iidx:camhook", "config not found");
                    return false;
                }

                const std::string root("/sp2x_cameras");
                auto numCameras = LOCAL_CAMERA_LIST.size();
                for (size_t i = 0; i < numCameras; i++) {
                    auto *camera = LOCAL_CAMERA_LIST.at(i);
                    auto symLink = camera->GetSymLink();

                    const auto cameraNode = rapidjson::Pointer(root + "/" + symLink).Get(doc);
                    if (cameraNode && cameraNode->IsObject()) {
                        log_info("iidx:camhook", "Parsing config for: {}", symLink);

                        // Media type
                        auto mediaTypePointer = rapidjson::Pointer(root + "/" + symLink + "/MediaType").Get(doc);
                        if (mediaTypePointer && mediaTypePointer->IsString()) {
                            std::string mediaType = mediaTypePointer->GetString();
                            if (mediaType.length() > 0) {
                                camera->m_selectedMediaTypeDescription = mediaType;
                                camera->m_useAutoMediaType = false;
                            } else {
                                camera->m_useAutoMediaType = true;
                            }
                        }

                        // Draw mode
                        auto drawModePointer = rapidjson::Pointer(root + "/" + symLink + "/DrawMode").Get(doc);
                        if (drawModePointer && drawModePointer->IsString()) {
                            std::string drawModeString = drawModePointer->GetString();
                            for (int j = 0; j < DRAW_MODE_SIZE; j++) {
                                if (DRAW_MODE_LABELS[j].compare(drawModeString) == 0) {
                                    camera->m_drawMode = (LocalCameraDrawMode) j;
                                    break;
                                }
                            }
                        }

                        // Flip
                        auto flipHorizontalPointer = rapidjson::Pointer(root + "/" + symLink + "/FlipHorizontal").Get(doc);
                        if (flipHorizontalPointer && flipHorizontalPointer->IsBool()) {
                            camera->m_flipHorizontal = flipHorizontalPointer->GetBool();
                        }

                        auto flipVerticalPointer = rapidjson::Pointer(root + "/" + symLink + "/FlipVertical").Get(doc);
                        if (flipVerticalPointer && flipVerticalPointer->IsBool()) {
                            camera->m_flipVertical = flipVerticalPointer->GetBool();
                        }

                        // Allow manual control
                        auto manualControlPointer = rapidjson::Pointer(root + "/" + symLink + "/AllowManualControl").Get(doc);
                        if (manualControlPointer && manualControlPointer->IsBool() && manualControlPointer->GetBool()) {
                            camera->m_allowManualControl = true;
                        }

                        // Camera control
                        // if m_allowManualControl is false, SetCameraControlProp will fail, but run through this code
                        // anyway to exercise parsing code
                        for (int propIndex = 0; propIndex < CAMERA_CONTROL_PROP_SIZE; propIndex++) {
                            std::string label = CAMERA_CONTROL_LABELS[propIndex];
                            CameraControlProp prop = {};
                            camera->GetCameraControlProp(propIndex, &prop);
                            auto valuePointer = rapidjson::Pointer(root + "/" + symLink + "/" + label + "/value").Get(doc);
                            auto flagsPointer = rapidjson::Pointer(root + "/" + symLink + "/" + label + "/flags").Get(doc);
                            if (valuePointer && valuePointer->IsInt() && flagsPointer && flagsPointer->IsInt()) {
                                prop.value = (long)valuePointer->GetInt();
                                prop.valueFlags = (long)flagsPointer->GetInt();
                                camera->SetCameraControlProp(propIndex, prop.value, prop.valueFlags);
                            }

                        }
                        log_misc("iidx:camhook", "  >> done");
                    } else {
                        log_misc("iidx:camhook", "No previous config for: {}", symLink);
                    }
                }
            }
        } catch (const std::exception& e) {
            log_warning("iidx:camhook", "exception occurred while config: {}", e.what());
            return false;
        }

        return true;
    }

    bool camera_config_save() {
        log_info("iidx:camhook", "saving config");

        // create document
        rapidjson::Document doc;
        std::string config = fileutils::text_read(CAMERA_CONFIG_PATH);
        if (!config.empty()) {
            doc.Parse(config.c_str());
            log_misc("iidx:camhook", "existing config file found");
        }
        if (!doc.IsObject()) {
            log_misc("iidx:camhook", "clearing out config file");
            doc.SetObject();
        }

        auto numCameras = LOCAL_CAMERA_LIST.size();
        for (size_t i = 0; i < numCameras; i++) {
            auto *camera = LOCAL_CAMERA_LIST.at(i);
            auto symLink = camera->GetSymLink();
            std::string root("/sp2x_cameras/" + symLink + "/");

            // Media type
            if (camera->m_useAutoMediaType) {
                rapidjson::Pointer(root + "MediaType").Set(doc, "");
            } else {
                rapidjson::Pointer(root + "MediaType").Set(doc, camera->m_selectedMediaTypeDescription);
            }

            // Draw Mode
            rapidjson::Pointer(root + "DrawMode").Set(doc, DRAW_MODE_LABELS[camera->m_drawMode]);

            // Flip
            rapidjson::Pointer(root + "FlipHorizontal").Set(doc, camera->m_flipHorizontal);
            rapidjson::Pointer(root + "FlipVertical").Set(doc, camera->m_flipVertical);

            // Manual control
            rapidjson::Pointer(root + "AllowManualControl").Set(doc, camera->m_allowManualControl);

            // Camera control
            for (int propIndex = 0; propIndex < CAMERA_CONTROL_PROP_SIZE; propIndex++) {
                std::string label = CAMERA_CONTROL_LABELS[propIndex];
                CameraControlProp prop = {};
                camera->GetCameraControlProp(propIndex, &prop);
                rapidjson::Pointer(root + label + "/value").Set(doc, (int)prop.value);
                rapidjson::Pointer(root + label + "/flags").Set(doc, (int)prop.valueFlags);
            }
        }

        // build JSON
        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);

        // save to file
        if (fileutils::write_config_file("iidx::camhook", CAMERA_CONFIG_PATH, buffer.GetString())) {
        } else {
            log_warning("iidx:camhook", "unable to save config file");
        }

        return true;
    }

    bool camera_config_reset() {
        return false;
    }
}

#endif
