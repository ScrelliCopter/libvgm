#include <stdint.h>

#ifndef __LIBVGMPLAY_H__
#define __LIBVGMPLAY_H__

#ifdef _MSC_VER
  #ifdef LIBVGMPLAY_BUILD
    #define LIBVGMPLAY_EXPORT __declspec(dllexport)
  #else
    #define LIBVGMPLAY_EXPORT __declspec(dllimport)
  #endif
#else
  #define LIBVGMPLAY_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VgmStream VgmStream;

typedef struct VgmSample16 { int16_t l, r; } VgmSample16;
typedef struct VgmSample32 { int32_t l, r; } VgmSample32;

const char LIBVGMPLAY_EXPORT* vgmplayGetError();
VgmStream LIBVGMPLAY_EXPORT* vgmplayOpenFile(const char* filepath, uint32_t samplerate, uint32_t buffersize);
void LIBVGMPLAY_EXPORT vgmplayClose(VgmStream* vgmstream);
uint32_t LIBVGMPLAY_EXPORT vgmplayRead16(VgmStream* s, VgmSample16* outBuf, uint32_t outLen);

#ifdef __cplusplus
}
#endif

#endif//__LIBVGMPLAY_H__
