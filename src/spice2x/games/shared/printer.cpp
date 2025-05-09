#include "printer.h"

#include <vector>
#include <thread>

#include "avs/game.h"
#include "hooks/sleephook.h"
#include "hooks/libraryhook.h"
#include "launcher/launcher.h"
#include "util/detour.h"
#include "util/fileutils.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/utils.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "external/stb_image_write.h"

namespace games::shared {

    // settings
    std::vector<std::string> PRINTER_PATH;
    std::vector<std::string> PRINTER_FORMAT;
    int PRINTER_JPG_QUALITY = 85;
    bool PRINTER_CLEAR = false;
    bool PRINTER_OVERWRITE_FILE = false;
    static DWORD PRINTER_TOTAL_COUNT = 1024;
    static short PRINTER_PAPER_REMAIN = 4096;

    typedef struct tagCPDIDinfo {
        short usbNo;
        short printerID;
        CHAR serialNo[6];
        WORD mediaType;
    } CPDIDinfo, *PCPDIDinfo;

    typedef struct tagCPDPaperRemainParams {
        short restNum;
        BOOL isWarning;
    } CPDPaperRemainParams, *PCPDPaperRemainParams;

    typedef struct tagCP9StatusInfo {
        DWORD printerStatus;
        DWORD detail;
        DWORD reserved2;
        DWORD reserved3;
    } CP9StatusInfo, *PCP9StatusInfo;

    typedef struct tagCP9FWInfo {
        char version_info1[6];
        char version_info2[6];
        char version_info3[6];
    } CP9FWInfo, *PCP9FWInfo;

    typedef struct tagCP9PrinterParams2 {
        DWORD flags1;
        DWORD reserved1;
        short mechaPreFeed;
        short jobResume;
        short TransferOnPrinting;
    } CP9PrinterParams2, *PCP9PrinterParams2;

    typedef struct tagCPDMediaType {
        WORD mediaType;
        WORD mediaDetail;
        WORD reserved;
    } CPDMediaType, *PCPDMediaType;

    typedef struct tagCPAPrinterParams {
        DWORD ver;
        DWORD flags1;
        DWORD reserved1;
        POINTS printPixel;
        short sidePrint;
        WORD printCount;
        short overCoat;
        short mirror;
        short marginCut;
        short multiCut;
        short multipanel;
        short printOut;
        short reserved4;
        short inkSkip;
    } CPAPrinterParams, *PCPAPrinterParams;

    typedef struct tagCPDBandImageParams {
        PVOID baseAddr;
        long rowBytes;
        RECT bounds;
    } CPDBandImageParams, *PCPDBandImageParams;

    typedef struct tagCPDContrastTable {
        BYTE r[256];
        BYTE g[256];
        BYTE b[256];
    } CPDContrastTable, *PCPDContrastTable;

    typedef struct tagCPDGammaTable {
        WORD r[256];
        WORD g[256];
        WORD b[256];
    } CPDGammaTable, *PCPDGammaTable;

    typedef struct tagCPAImageEffectParams {
        DWORD ver;
        short ColorTabel;
        short DLLColorTabel;
        short ContrastTabel;
        const CPDContrastTable *pContTbl;
        short sharpness0;
        const BYTE *pSharpnessTbl;
        short sharpness1;
        short gamma;
        short printMode;
        short linesharpness;
        const CPDGammaTable *pGammaTbl;
        short overcoatMode;
    } CPAImageEffectParams, *PCPAImageEffectParams;

    enum {
        PStatus_Ready = 0,
        PStatus_Printing,
        PStatus_MechaInit,
        PStatus_FeedandCut
    };

    enum {
        Error_NoError = 0,
        Error_Something,
        Error_DeviceNotFound = 100,
        Error_Busy,
        Error_Printing_Busy,
        Error_Printing_Ready,
        Error_PortBusy,
        Error_GetCPDIDinfo,
        Error_PrinterBusy,
        Error_FuncParamError = 201,
        Error_MemAllocError,
        Error_Timeout,
        Error_MattePrinter = 301,
        Error_MatteRibbon,
        Error_InvalidParam = 1000,
        Error_SheetEnd,
        Error_PaperEnd,
        Error_PaperJam,
        Error_SheetJam,
        Error_SheetCassetNotSet,
        Error_PaperSheetIllegal,
        Error_PaperCassetNotSet,
        Error_PaperSizeIllegal,
        Error_PaperTrayNotSet,
        Error_OHPReverse,
        Error_HeatError,
        Error_DewError,
        Error_DoorOpen,
        Error_UnusableSheet,
        Error_SheetCassetIllegal,
        Error_PaperRemain,
        Error_SheetRemain,
        Error_NotSupported,
        Error_SheetMarkError = 1101,
        Error_PaperJam_D,
        Error_MechaError,
        Error_MechaError_D,
        Error_MechaInitReq,
        Error_PrintingTurnOff,
        Error_ContrastDataError,
        Error_TableError,
        Error_PrinterError,
        Error_PickPosition,
        Error_NoScrapBox,
        Error_PrintingDoorOpen,
        Error_SheetError,
        Error_SheetCountError,
        Error38_HeadVoltage = 1200,
        Error38_HeadPosition,
        Error38_FunStopped,
        Error38_CutterError,
        Error38_PinchRollerPosition,
        Error38_HeadTemp,
        Error38_MediaTemp,
        Error38_PaperWindingMorterTemp,
        Error38_RibbonTension = 1210,
        Error38_RFID_Error = 1220,
        Error38_SystemError = 1230,
        Error_JInvalidParam = 1300,
        Error_JMemoryFull,
        Error_JPaperSizeIllegal = 1310,
        Error_JLastJobError,
        Error_JTimeout = 1320,
        Error_JJobCancel,
        Error_JUSBInterrupt,
    };

    static DWORD __stdcall CPU9CheckPaperRemain(PCPDPaperRemainParams pPaperRemain, PCPDIDinfo pIDInfo) {
        pPaperRemain->restNum = PRINTER_PAPER_REMAIN;
        pPaperRemain->isWarning = FALSE;
        return Error_NoError;
    }

    static DWORD __stdcall CPU9CheckPrinter(PCP9StatusInfo pStInfo, PCPDIDinfo pIDInfo) {
        pStInfo->printerStatus = PStatus_Ready;
        pStInfo->detail = 0;
        return Error_NoError;
    }

    static DWORD __stdcall CPU9CheckPrintEnd(DWORD meminfo, PBOOL pbisEnd, PCPDIDinfo pIDInfo) {
        *pbisEnd = true;
        return Error_NoError;
    }

    static DWORD __stdcall CPU9GetFWInfo(PCP9FWInfo pFWInfo, PCPDIDinfo pIDInfo) {
        strcpy(pFWInfo->version_info1, std::string("1").c_str());
        strcpy(pFWInfo->version_info2, std::string("0").c_str());
        strcpy(pFWInfo->version_info3, std::string("1").c_str());
        return Error_NoError;
    }

    static DWORD __stdcall CPU9GetMediaType(PCPDMediaType pMType, PCPDIDinfo pIDInfo) {
        if (avs::game::is_model("KLP")) {
            pMType->mediaType = 56;
        } else {
            pMType->mediaType = 2;
        }

        pMType->mediaDetail = 18432;

        return Error_NoError;
    }

    static DWORD __stdcall CPU9GetTempInfo(void *pTempInfo, PCPDIDinfo pIDInfo) {
        return Error_NoError;
    }

    static DWORD __stdcall CPU9GetTotalPrintCount(PDWORD pdwCount, PCPDIDinfo pIDInfo) {
        *pdwCount = PRINTER_TOTAL_COUNT;
        return Error_NoError;
    }

    static DWORD __stdcall CPU9PreHeat(PCPDIDinfo pIDInfo) {
        return Error_NoError;
    }

    static DWORD __stdcall CPU9PrintJobCancel(PCPDIDinfo pIDInfo) {
        return Error_NoError;
    }

    static DWORD __stdcall CPU9PrintOut(PDWORD pmeminfo, PCPDIDinfo pIDInfo) {

        // do logic
        if (PRINTER_PAPER_REMAIN > 0) {
            PRINTER_TOTAL_COUNT++;
            PRINTER_PAPER_REMAIN--;
            return Error_NoError;
        } else {
            return Error_PaperRemain;
        }
    }

    static DWORD __stdcall CPU9SetPrintParameter2(const CP9PrinterParams2 *setP, PCPDIDinfo pIDInfo) {
        return Error_NoError;
    }

    typedef void (CALLBACK *PFNACBfunc)(DWORD dwErr, short PrintFlag, short PrintUsbNo, long nJid, int CopyNumber);

    static inline std::string get_image_out_path(bool clear, std::string path, const std::string &format) {

        // check path
        if (path.empty()) {
            log_warning("printer", "Printer Emulation output directory can't be empty. Resetting to \".\"");
            path = ".";
        }

        // check trailing slash
        if (string_ends_with(path.c_str(), "\\"))
            path = std::string(path.c_str(), path.length() - 1);

        // find non-existing filename
        static std::string prefix = "printer_";
        for (int n = 0; n < 4096; n++) {
            std::ostringstream filename_s;
            filename_s << path << "\\" << prefix << n << "." << format;
            std::string filename = filename_s.str();
            if (clear && fileutils::file_exists(filename.c_str())) {
                log_info("printer", "deleting {}...", filename);
                DeleteFile(filename.c_str());
                if (PRINTER_OVERWRITE_FILE) {
                    return filename;
                }
            }
            else if (!fileutils::file_exists(filename.c_str()))
                return filename;
        }

        // error
        if (!clear) {
            log_fatal("sdvx", "could not find path");
        }

        return "DUMMY";
    }

    static inline bool process_image_print(const CPDBandImageParams *pBandImage) {

        // log
        log_info("printer", "processing incoming print job");

        // get image bounds
        int image_width = pBandImage->bounds.right - pBandImage->bounds.left;
        int image_height = pBandImage->bounds.bottom - pBandImage->bounds.top;

        // check bounds
        if (image_width <= 0 || image_height <= 0) {
            log_warning("printer", "invalid image size: {}x{}", image_width, image_height);
            return false;
        }

        // check rowBytes
        if (pBandImage->rowBytes < 0 || pBandImage->rowBytes != image_width * 3) {
            log_warning("printer", "unsupported image data layout: {}", pBandImage->rowBytes);
            return false;
        }

        // make a copy of the image data
        auto image_data = new uint8_t[image_width * image_height * 3];
        memcpy(image_data, pBandImage->baseAddr, (size_t) (image_width * image_height * 3));

        // convert BGR to RGB
        log_misc("printer", "converting BGR to RGB...");
        for (int pixel = 0; pixel < image_width * image_height; pixel++) {
            int index = pixel * 3;
            uint8_t tmp = image_data[index];
            image_data[index] = image_data[index + 2];
            image_data[index + 2] = tmp;
        }

        if (avs::game::is_model({"KLP", "KFC"})) {
            // rotate image clockwise
            log_misc("printer", "rotate clockwise...");
            auto rotated = new uint8_t[image_width * image_height * 3];
            for (int x = 0; x < image_width; x++) {
                for (int y = 0; y < image_height; y++) {
                    int index1 = (y * image_width + x) * 3;
                    int index2 = (x * image_height + y) * 3;
                    rotated[index2 + 0] = image_data[index1 + 0];
                    rotated[index2 + 1] = image_data[index1 + 1];
                    rotated[index2 + 2] = image_data[index1 + 2];
                }
            }
            delete[] image_data;
            image_data = rotated;
            std::swap(image_width, image_height);

        } else {
            // flip horizontally
            log_misc("printer", "flip horizontally...");
            for (int x = 0; x < image_width / 2; x++) {
                for (int y = 0; y < image_height; y++) {
                    int index1 = (y * image_width + x) * 3;
                    int index2 = (y * image_width + image_width - x - 1) * 3;
                    uint8_t r = image_data[index1 + 0];
                    uint8_t g = image_data[index1 + 1];
                    uint8_t b = image_data[index1 + 2];
                    image_data[index1 + 0] = image_data[index2 + 0];
                    image_data[index1 + 1] = image_data[index2 + 1];
                    image_data[index1 + 2] = image_data[index2 + 2];
                    image_data[index2 + 0] = r;
                    image_data[index2 + 1] = g;
                    image_data[index2 + 2] = b;
                }
            }
        }

        // iterate folders
        log_misc("printer", "writing files...");
        for (const auto &path : PRINTER_PATH) {
            for (const auto &format : PRINTER_FORMAT) {

                // get image path
                std::string image_path = get_image_out_path(PRINTER_OVERWRITE_FILE, path, format);
                bool success = false;

                // call write function depending on format
                if (format == "png" && stbi_write_png(
                        image_path.c_str(), image_width, image_height, 3, image_data, image_width * 3))
                    success = true;
                if (format == "bmp" && stbi_write_bmp(
                        image_path.c_str(), image_width, image_height, 3, image_data))
                    success = true;
                if (format == "tga" && stbi_write_tga(
                        image_path.c_str(), image_width, image_height, 3, image_data))
                    success = true;
                if (format == "jpg" && stbi_write_jpg(
                        image_path.c_str(), image_width, image_height, 3, image_data, PRINTER_JPG_QUALITY))
                    success = true;

                // logging
                if (success) {
                    log_info("printer", "printer emulation has written an image to {}", image_path);
                } else {
                    log_warning("printer", "printer emulation failed to write image to {}", image_path);
                }
            }
        }

        // clean up
        delete[] image_data;
        return true;
    }

    static DWORD __stdcall CPUASendImage(
        const CPDBandImageParams *pBandImage,
        const CPAPrinterParams *setP,
        const CPAImageEffectParams *piep,
        PCPDIDinfo pIDInfo
    ) {
        // process image
        if (!process_image_print(pBandImage)) {
            return Error_InvalidParam;
        }

        return Error_NoError;
    }

    /*
     * This is the function which seems to get called from the game.
     * Default card layout parameters:
     *     rowBytes = 3216
     *     bounds.left = 0
     *     bounds.top = 0
     *     bounds.right = 1072
     *     bounds.bottom = 712
     */
    static DWORD __stdcall CPUASendImagePrint(const CPAPrinterParams *setP, const CPDBandImageParams *pBandImage,
                                              const CPAImageEffectParams *piep, BOOL memClear, PFNACBfunc pfncb,
                                              long nJid, PCPDIDinfo pIDInfo) {
        /*
         * From documentation: rowBytes
         * Number of bits a line of image data. Usually, in case of a bitmap file of 24bit, it is
         * (bmInfoHeader.biWidth * 3 + 3) / 4 * 4
         * It scans up from the lower left in case of a positive numeric (bitmap file standard).
         * In this case, the address at the lower left of image data is usually set in baseAddr.
         * It scans below from the upper left in the case of a negative numeric. In this case,
         * The address in the upper left of image data is usually set in baseAddr.
         */

        // process image
        if (!process_image_print(pBandImage)) {
            return Error_InvalidParam;
        }

        // fire up printer thread
        // the game fires up a listener around 4 seconds after the call
        WORD printCount = setP->printCount;
        short usbNo = pIDInfo->usbNo;
        std::thread t([printCount, pfncb, usbNo, nJid]() {
            for (int print_no = 1; print_no <= printCount; print_no++) {

                // wait for game listener
                Sleep(4000);

                // do logic
                if (PRINTER_PAPER_REMAIN > 0) {
                    PRINTER_TOTAL_COUNT++;
                    PRINTER_PAPER_REMAIN--;
                    pfncb(Error_NoError, 0, usbNo, nJid, print_no);
                } else {
                    pfncb(Error_PaperRemain, 0, usbNo, nJid, print_no);
                }
            }
        });

        // detach thread so it will keep running
        t.detach();

        return Error_NoError;
    }

    /*
     * Probably irrelevant, doesn't seem to get called by the game.
     */
    static DWORD __stdcall CPUASendImagePrint2(
        const CPAPrinterParams *setP,
        const CPDBandImageParams *pBandImage,
        const CPAImageEffectParams *piep,
        BOOL memClear,
        BOOL sendOnPrn,
        PFNACBfunc pfncb,
        long nJid,
        PCPDIDinfo pIDInfo
    ) {
        // forward to other function
        return CPUASendImagePrint(setP, pBandImage, piep, memClear, pfncb, nJid, pIDInfo);
    }

    static DWORD __stdcall CPUASetPrintParameter(
        const CPAPrinterParams *setP,
        CPAImageEffectParams *piep,
        BOOL memClear,
        PDWORD pmeminfo,
        PCPDIDinfo pIDInfo
    ) {
        return Error_NoError;
    }

    static DWORD __stdcall CPUXSearchPrinters(
        PCPDIDinfo pIDInfo,
        DWORD infoSize,
        LPDWORD pSizeNeeded,
        LPDWORD pInfoNum
    ) {
        // set information number
        // (LovePlus needs this to determine how many info structures to allocate)
        *pInfoNum = 1;

        // check info size
        if (infoSize != 12) {
            *pSizeNeeded = 12;

            return Error_GetCPDIDinfo;
        }

        // set printer information
        pIDInfo->usbNo = 1;
        pIDInfo->printerID = 1;
        memset(pIDInfo->serialNo, 'F', 5);
        memset(pIDInfo->serialNo + 5, 0, 1);
        pIDInfo->mediaType = 2;

        // LovePlus
        if (avs::game::is_model("KLP")) {

            // LovePlus uses a different media type
            pIDInfo->mediaType = 56;
        }

        // Otoca D'or
        if (avs::game::is_model("NCG")) {

            // requires a specific printer ID
            pIDInfo->printerID = 2160;
        }

        return Error_NoError;
    }

    static void __stdcall CPUXInit() {
        log_info("printer", "CPUXInit called");
    }

    void printer_attach() {
        log_info("printer", "SpiceTools Printer");

        // default parameters
        if (PRINTER_PATH.empty()) {
            PRINTER_PATH.emplace_back(".");
        }
        if (PRINTER_FORMAT.empty()) {
            PRINTER_FORMAT.emplace_back("png");
        }

        // validate file formats
        for (const auto &format : PRINTER_FORMAT) {
            if (format != "png" &&
                format != "bmp" &&
                format != "tga" &&
                format != "jpg")
            {
                log_fatal("printer", "unknown file format: {}", format);
            }
        }

        // validate JPEG quality
        if (PRINTER_JPG_QUALITY <= 0 || PRINTER_JPG_QUALITY > 100) {
            log_fatal("printer", "invalid JPEG quality setting (1-100): {}",
                    PRINTER_JPG_QUALITY);
        }

        // clear
        if (PRINTER_CLEAR) {
            PRINTER_CLEAR = false;
            for (const auto &path : PRINTER_PATH) {
                for (const auto &format : PRINTER_FORMAT) {
                    get_image_out_path(true, path, format);
                }
            }
        }

        // IAT hooks
        detour::iat_try("CPU9CheckPaperRemain", CPU9CheckPaperRemain);
        detour::iat_try("CPU9CheckPrinter", CPU9CheckPrinter);
        detour::iat_try("CPU9CheckPrintEnd", CPU9CheckPrintEnd);
        detour::iat_try("CPU9GetFWInfo", CPU9GetFWInfo);
        detour::iat_try("CPU9GetMediaType", CPU9GetMediaType);
        detour::iat_try("CPU9GetTempInfo", CPU9GetTempInfo);
        detour::iat_try("CPU9GetTotalPrintCount", CPU9GetTotalPrintCount);
        detour::iat_try("CPU9PreHeat", CPU9PreHeat);
        detour::iat_try("CPU9PrintJobCancel", CPU9PrintJobCancel);
        detour::iat_try("CPU9PrintOut", CPU9PrintOut);
        detour::iat_try("CPU9SetPrintParameter2", CPU9SetPrintParameter2);
        detour::iat_try("CPUASendImage", CPUASendImage);
        detour::iat_try("CPUASendImagePrint", CPUASendImagePrint);
        detour::iat_try("CPUASendImagePrint2", CPUASendImagePrint2);
        detour::iat_try("CPUASetPrintParameter", CPUASetPrintParameter);
        detour::iat_try("CPUXInit", CPUXInit);
        detour::iat_try("CPUXSearchPrinters", CPUXSearchPrinters);

        // library hook
        libraryhook_hook_library("CPUSBXPKM.DLL", GetModuleHandle(nullptr));
        libraryhook_hook_proc("CPU9CheckPaperRemain", CPU9CheckPaperRemain);
        libraryhook_hook_proc("CPU9CheckPrinter", CPU9CheckPrinter);
        libraryhook_hook_proc("CPU9CheckPrintEnd", CPU9CheckPrintEnd);
        libraryhook_hook_proc("CPU9GetFWInfo", CPU9GetFWInfo);
        libraryhook_hook_proc("CPU9GetMediaType", CPU9GetMediaType);
        libraryhook_hook_proc("CPU9GetTempInfo", CPU9GetTempInfo);
        libraryhook_hook_proc("CPU9GetTotalPrintCount", CPU9GetTotalPrintCount);
        libraryhook_hook_proc("CPU9PreHeat", CPU9PreHeat);
        libraryhook_hook_proc("CPU9PrintJobCancel", CPU9PrintJobCancel);
        libraryhook_hook_proc("CPU9PrintOut", CPU9PrintOut);
        libraryhook_hook_proc("CPU9SetPrintParameter2", CPU9SetPrintParameter2);
        libraryhook_hook_proc("CPUASendImage", CPUASendImage);
        libraryhook_hook_proc("CPUASendImagePrint", CPUASendImagePrint);
        libraryhook_hook_proc("CPUASendImagePrint2", CPUASendImagePrint2);
        libraryhook_hook_proc("CPUASetPrintParameter", CPUASetPrintParameter);
        libraryhook_hook_proc("CPUXInit", CPUXInit);
        libraryhook_hook_proc("CPUXSearchPrinters", CPUXSearchPrinters);
        libraryhook_enable(avs::game::DLL_INSTANCE);
    }
}
