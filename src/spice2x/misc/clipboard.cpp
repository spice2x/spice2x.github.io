#include "clipboard.h"

// GDI+ Headers
// WARNING: Must stay in this order to compile
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#ifdef __GNUC__
#include <gdiplus/gdiplusflat.h>
#else
#include <gdiplusflat.h>
#endif

#include <thread>

#include "util/libutils.h"
#include "util/logging.h"
#include "util/utils.h"

namespace clipboard {
    namespace imports {
        static bool ATTEMPTED_LOAD_LIBRARY = false;

        static decltype(Gdiplus::GdiplusShutdown) *GdiplusShutdown = nullptr;
        static decltype(Gdiplus::GdiplusStartup) *GdiplusStartup = nullptr;
        static decltype(Gdiplus::DllExports::GdipCreateBitmapFromFile) *GdipCreateBitmapFromFile = nullptr;
        static decltype(Gdiplus::DllExports::GdipCreateHBITMAPFromBitmap) *GdipCreateHBITMAPFromBitmap = nullptr;
        static decltype(Gdiplus::DllExports::GdipDisposeImage) *GdipDisposeImage = nullptr;
    }

    void copy_image_handler(const std::filesystem::path &path) {
        if (!imports::ATTEMPTED_LOAD_LIBRARY) {
            imports::ATTEMPTED_LOAD_LIBRARY = true;

            auto gdiplus = libutils::try_library("gdiplus.dll");

            if (gdiplus) {
                imports::GdiplusShutdown = (decltype(imports::GdiplusShutdown)) libutils::try_proc(
                        gdiplus, "GdiplusShutdown");
                imports::GdiplusStartup = (decltype(imports::GdiplusStartup)) libutils::try_proc(
                        gdiplus, "GdiplusStartup");
                imports::GdipCreateBitmapFromFile = (decltype(imports::GdipCreateBitmapFromFile)) libutils::try_proc(
                        gdiplus, "GdipCreateBitmapFromFile");
                imports::GdipCreateHBITMAPFromBitmap = (decltype(imports::GdipCreateHBITMAPFromBitmap)) libutils::try_proc(
                        gdiplus, "GdipCreateHBITMAPFromBitmap");
                imports::GdipDisposeImage = (decltype(imports::GdipDisposeImage)) libutils::try_proc(
                        gdiplus, "GdipDisposeImage");
            } else {
                log_warning("clipboard", "GDI+ library not found, disabling clipboard functionality");
            }
        }
        if (!imports::GdiplusShutdown ||
            !imports::GdiplusStartup ||
            !imports::GdipCreateBitmapFromFile ||
            !imports::GdipCreateHBITMAPFromBitmap ||
            !imports::GdipDisposeImage)
        {
            return;
        }

        // If the screenshot button is print screen, the OpenClipboard call seems to fail often if we only
        // call it once, probably due to a race condition. So, we can try calling a lot until we can open it.
        bool clipboard_open = false;
        for (int i = 0; i < 1000000; i++) {
            if (OpenClipboard(nullptr)) {
                clipboard_open = true;
                break;
            }
        }
        if (!clipboard_open) {
            log_warning("clipboard", "Failed to open clipboard");
            return;
        }

        // Start gdiplus
        Gdiplus::GdiplusStartupInput input {};
        ULONG_PTR token;
        imports::GdiplusStartup(&token, &input, nullptr);

        // Convert the file path to a wstring and open the screenshot file
        Gdiplus::GpBitmap *bitmap = nullptr;
        auto status = imports::GdipCreateBitmapFromFile(path.c_str(), &bitmap);
        if (status != Gdiplus::Ok) {
            log_warning("clipboard", "failed to create GDI+ bitmap: {}", static_cast<uint32_t>(status));

            imports::GdiplusShutdown(token);
            CloseClipboard();

            return;
        }

        // Retrieve the HBITMAP from the Bitmap object
        HBITMAP hbitmap {};
        status = imports::GdipCreateHBITMAPFromBitmap(bitmap, &hbitmap, 0);
        if (status == Gdiplus::Ok) {

            // Convert the HBITMAP to a DIB to copy to the clipboard
            BITMAP bm;
            GetObject(hbitmap, sizeof(bm), &bm);

            BITMAPINFOHEADER info;
            info.biSize = sizeof(info);
            info.biWidth = bm.bmWidth;
            info.biHeight = bm.bmHeight;
            info.biPlanes = 1;
            info.biBitCount = bm.bmBitsPixel;
            info.biCompression = BI_RGB;

            std::vector<BYTE> dimensions(bm.bmWidthBytes * bm.bmHeight);
            auto hdc = GetDC(nullptr);
            GetDIBits(hdc, hbitmap, 0, info.biHeight, dimensions.data(), reinterpret_cast<BITMAPINFO *>(&info), 0);
            ReleaseDC(nullptr, hdc);

            auto hmem = GlobalAlloc(GMEM_MOVEABLE, sizeof(info) + dimensions.size());
            auto buffer = reinterpret_cast<BYTE *>(GlobalLock(hmem));
            memcpy(buffer, &info, sizeof(info));
            memcpy(&buffer[sizeof(info)], dimensions.data(), dimensions.size());
            GlobalUnlock(hmem);

            if (SetClipboardData(CF_DIB, hmem)) {
                log_info("clipboard", "saved image to clipboard");
            } else {
                log_warning("clipboard", "failed to save image to clipboard");
            }
        } else {
            log_warning("clipboard", "failed to retrieve HBITMAP from image bitmap: {}", static_cast<uint32_t>(status));
        }

        // Clean up after ourselves. hmem can't be deleted because it is owned by the clipboard now.
        imports::GdipDisposeImage(bitmap);

        imports::GdiplusShutdown(token);

        CloseClipboard();
    }

    void copy_image(const std::filesystem::path path) {

        // Create a new thread since we loop to open the clipboard
        std::thread handle(copy_image_handler, std::move(path));

        handle.detach();
    }

    void copy_text(const std::string& str) {
        if (!OpenClipboard(nullptr)) {
            log_warning("clipboard", "Failed to open clipboard");
            return;
        }

        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, str.length() + 1);
        memcpy(GlobalLock(mem), str.c_str(), str.length() + 1);
        GlobalUnlock(mem);

        EmptyClipboard();
        SetClipboardData(CF_TEXT, mem);
        CloseClipboard();
    }

    const std::string paste_text() {
        HGLOBAL hglb; 
        LPSTR str;
        std::string text;

        // check if clipboard content is text
        if (!IsClipboardFormatAvailable(CF_TEXT)) {
            return text;
        }
        if (!OpenClipboard(nullptr)) {
            log_warning("clipboard", "Failed to open clipboard");
            return text;
        }

        hglb = GetClipboardData(CF_TEXT); 
        if (hglb != NULL) { 
            str = reinterpret_cast<LPSTR>(GlobalLock(hglb));
            if (str != NULL)  { 
                text = str;
                GlobalUnlock(hglb); 
            } 
        } 
        CloseClipboard();  
        return text;
    }
}
