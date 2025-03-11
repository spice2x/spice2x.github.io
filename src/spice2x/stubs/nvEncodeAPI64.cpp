#include <cstdint>

#define EXPORT __declspec(dllexport)
#define IMPORT

typedef uint32_t NVENCSTATUS;
#define NV_ENC_SUCCESS 0
#define NV_ENC_ERR_INVALID_PTR 6

extern "C" NVENCSTATUS EXPORT __cdecl NvEncodeAPIGetMaxSupportedVersion(uint32_t* version) {
    return NV_ENC_ERR_INVALID_PTR;
}

extern "C" NVENCSTATUS EXPORT __cdecl NvEncodeAPICreateInstance(void *functionList) {
    return NV_ENC_ERR_INVALID_PTR;
}
