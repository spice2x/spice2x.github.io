#include <cstdint>

#define EXPORT __declspec(dllexport)
#define IMPORT

typedef void *CUVIDDECODECAPS;
typedef void *CUVIDGETDECODESTATUS;
typedef void *CUVIDRECONFIGUREDECODERINFO;
typedef void *CUVIDPROCPARAMS;
typedef void *CUVIDSOURCEDATAPACKET;
typedef void *CUcontext;
typedef void *CUvideodecoder;
typedef void *CUvideoparser;
typedef void *CUVIDDECODECREATEINFO;
typedef void *CUVIDPARSERPARAMS;
typedef void *CUVIDPICPARAMS;
typedef void *CUvideoctxlock;

#define CUDA_ERROR_NOT_SUPPORTED 801

// https://docs.nvidia.com/cuda/cuda-driver-api/group__CUDA__TYPES.html#group__CUDA__TYPES_1gc6c391505e117393cc2558fff6bfc2e9
typedef uint32_t CUresult;

extern "C" CUresult EXPORT __cdecl cuvidCreateDecoder(CUvideodecoder *phDecoder, CUVIDDECODECREATEINFO *pdci) {
    return CUDA_ERROR_NOT_SUPPORTED;
}

extern "C" CUresult EXPORT __cdecl cuvidCreateVideoParser(CUvideoparser *pObj, CUVIDPARSERPARAMS *pParams) {
    return CUDA_ERROR_NOT_SUPPORTED;
}

extern "C" CUresult EXPORT __cdecl cuvidCtxLockCreate(CUvideoctxlock *pLock, CUcontext ctx) {
    return CUDA_ERROR_NOT_SUPPORTED;
}

extern "C" CUresult EXPORT __cdecl cuvidCtxLockDestroy(CUvideoctxlock lck) {
    return CUDA_ERROR_NOT_SUPPORTED;
}

extern "C" CUresult EXPORT __cdecl cuvidDecodePicture(CUvideodecoder hDecoder, CUVIDPICPARAMS *pPicParams) {
    return CUDA_ERROR_NOT_SUPPORTED;
}

extern "C" CUresult EXPORT __cdecl cuvidDestroyDecoder(CUvideodecoder hDecoder) {
    return CUDA_ERROR_NOT_SUPPORTED;
}

extern "C" CUresult EXPORT __cdecl cuvidDestroyVideoParser(CUvideoparser obj) {
    return CUDA_ERROR_NOT_SUPPORTED;
}

extern "C" CUresult EXPORT __cdecl cuvidGetDecodeStatus(CUvideodecoder hDecoder, int nPicIdx, CUVIDGETDECODESTATUS* pDecodeStatus) {
    return CUDA_ERROR_NOT_SUPPORTED;
}

extern "C" CUresult EXPORT __cdecl cuvidGetDecoderCaps(CUVIDDECODECAPS *pdc) {
    return CUDA_ERROR_NOT_SUPPORTED;
}

extern "C" CUresult EXPORT __cdecl cuvidMapVideoFrame64(
    CUvideodecoder hDecoder, int nPicIdx, unsigned long long *pDevPtr, unsigned int *pPitch, CUVIDPROCPARAMS *pVPP) {
    return CUDA_ERROR_NOT_SUPPORTED;
}

extern "C" CUresult EXPORT __cdecl cuvidParseVideoData(CUvideoparser obj, CUVIDSOURCEDATAPACKET *pPacket) {
    return CUDA_ERROR_NOT_SUPPORTED;
}

extern "C" CUresult EXPORT __cdecl cuvidReconfigureDecoder(CUvideodecoder hDecoder, CUVIDRECONFIGUREDECODERINFO *pDecReconfigParams) {
    return CUDA_ERROR_NOT_SUPPORTED;
}

extern "C" CUresult EXPORT __cdecl cuvidUnmapVideoFrame64(CUvideodecoder hDecoder, unsigned long long DevPtr) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
