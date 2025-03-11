#include <cstdint>

#define EXPORT __declspec(dllexport)
#define IMPORT

// https://docs.nvidia.com/cuda/cuda-driver-api/index.html

#define CUDA_ERROR_NOT_SUPPORTED 801

typedef void *CUcontext;
typedef void *CUdevice;
typedef void *CUgraphicsResource;
typedef void *CUstream;
typedef void *CUdeviceptr;
typedef void *CUarray;
typedef void *CUDA_MEMCPY2D;
typedef void *IDirect3DResource9;
typedef uint32_t CUresult;

extern "C" CUresult EXPORT __cdecl cuCtxPushCurrent_v2(CUcontext ctx) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuDeviceGet(char *pciBusId, int len, CUdevice dev) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuInit(unsigned int Flags) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuGraphicsMapResources(
    unsigned int count, CUgraphicsResource *resources, CUstream hStream) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuMemAlloc_v2(CUdeviceptr *dptr, size_t bytesize) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuCtxPopCurrent_v2(CUcontext *pctx) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuGetErrorName(CUresult error, const char **pStr) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuCtxCreate_v2(CUcontext *pctx, unsigned int flags, CUdevice dev) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuCtxDestroy_v2(CUcontext ctx) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuGraphicsResourceSetMapFlags_v2(
    CUgraphicsResource resource, unsigned int flags) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuGraphicsUnregisterResource(CUgraphicsResource resource) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuMemAllocPitch_v2(
    CUdeviceptr *dptr, size_t *pPitch, size_t WidthInBytes, size_t Height, unsigned int ElementSizeBytes) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuGraphicsUnmapResources(
    unsigned int count, CUgraphicsResource *resources, CUstream hStream) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuStreamSynchronize(CUstream hStream) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuMemcpy2DAsync_v2(const CUDA_MEMCPY2D *pCopy, CUstream hStream) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuGraphicsSubResourceGetMappedArray(
    CUarray *pArray, CUgraphicsResource resource, unsigned int arrayIndex, unsigned int mipLevel) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuMemFree_v2(CUdeviceptr dptr) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuMemcpy2D_v2(const CUDA_MEMCPY2D *pCopy) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuGraphicsD3D9RegisterResource(
    CUgraphicsResource *pCudaResource, IDirect3DResource9 *pD3DResource, unsigned int Flags) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuStreamCreate(CUstream *phStream, unsigned int Flags) {
    return CUDA_ERROR_NOT_SUPPORTED;
}
extern "C" CUresult EXPORT __cdecl cuStreamDestroy_v2(CUstream hStream){
    return CUDA_ERROR_NOT_SUPPORTED;
}