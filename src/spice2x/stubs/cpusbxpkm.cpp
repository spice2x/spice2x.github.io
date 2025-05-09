#include <cstdint>

#define EXPORT __declspec(dllexport)

enum {
    Error_NoError = 0,
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
};

extern "C" int32_t EXPORT __stdcall CPU9CheckPaperRemain(void *p1, void *p2) {
    return Error_FuncParamError;
}

extern "C" int32_t EXPORT __stdcall CPU9CheckPrinter(void *p1, void *p2) {
    return Error_FuncParamError;
}

extern "C" int32_t EXPORT __stdcall CPU9CheckPrintEnd(uint32_t p1, void *p2, void *p3) {
    return Error_FuncParamError;
}

extern "C" int32_t EXPORT __stdcall CPU9GetFWInfo(void *p1, void *p2) {
    return Error_FuncParamError;
}

extern "C" int32_t EXPORT __stdcall CPU9GetMediaType(void *p1, void *p2) {
    return Error_FuncParamError;
}

extern "C" int32_t EXPORT __stdcall CPU9GetTempInfo(void *p1, void *p2) {
    return Error_FuncParamError;
}

extern "C" int32_t EXPORT __stdcall CPU9GetTotalPrintCount(void *p1, void *p2) {
    return Error_FuncParamError;
}

extern "C" int32_t EXPORT __stdcall CPU9PreHeat(void *p1) {
    return Error_FuncParamError;
}

extern "C" int32_t EXPORT __stdcall CPU9PrintJobCancel(void *p1) {
    return Error_FuncParamError;
}

extern "C" int32_t EXPORT __stdcall CPU9PrintOut(void *p1, void *p2) {
    return Error_FuncParamError;
}

extern "C" int32_t EXPORT __stdcall CPU9SetPrintParameter2(void *p1, void *p2) {
    return Error_FuncParamError;
}

extern "C" int32_t EXPORT __stdcall CPUASendImage(void *p1, void *p2, void *p3, void *p4) {
    return Error_FuncParamError;
}

extern "C" int32_t EXPORT __stdcall CPUASendImagePrint(
    void *p1, void *p2, void *p3, bool p4, void *p5, int32_t p6, void *p7) {
    return Error_FuncParamError;
}

extern "C" int32_t EXPORT __stdcall CPUASendImagePrint2(
    void *p1, void *p2, void *p3, bool p4, bool p5, void *p6, int32_t p7, void *p8) {
    return Error_FuncParamError;
}

extern "C" int32_t EXPORT __stdcall CPUASetPrintParameter(void *p1, void *p2, bool p3, void *p4, void *p5) {
    return Error_FuncParamError;
}

extern "C" int32_t EXPORT __stdcall CPUXSearchPrinters(void *p1, void *p2, int32_t p3, void *p4) {
    return Error_FuncParamError;
}

extern "C" void EXPORT __stdcall CPUXInit() {
    return;
}

