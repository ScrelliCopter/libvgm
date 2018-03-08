#ifndef __INTERNAL_H__
#define __INTERNAL_H__

#include "emu/Resampler.h"

typedef struct gzFile_s *gzFile;
typedef struct InternalState InternalState;

extern const char* interErr;

InternalState* InternalOpen(gzFile hFile, UINT32 rate, WAVE_32BS* buf, UINT32 bufLen);
void InternalFree(InternalState* s);
void InternalRead(InternalState* s, UINT32 smplCount, UINT32 smplOfs);
void ProcessVGM(InternalState* s, UINT32 smplCount, UINT32 smplOfs);
void InitVGMChips(InternalState* s);
void DeinitVGMChips(InternalState* s);
void SendChipCommand_Data8(InternalState* s, UINT8 chipID, UINT8 chipNum, UINT8 ofs, UINT8 data);
void SendChipCommand_RegData8(InternalState* s, UINT8 chipID, UINT8 chipNum, UINT8 port, UINT8 reg, UINT8 data);
void SendChipCommand_MemData8(InternalState* s, UINT8 chipID, UINT8 chipNum, UINT16 ofs, UINT8 data);
void SendChipCommand_Data16(InternalState* s, UINT8 chipID, UINT8 chipNum, UINT8 ofs, UINT16 data);
UINT32 DoVgmCommand(InternalState* s, UINT8 cmd, const UINT8* data);
void ReadVGMFile(InternalState* s, UINT32 samples);

#endif//__INTERNAL_H__
