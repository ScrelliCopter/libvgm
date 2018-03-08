#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include <string.h>
#include <ctype.h>

#define VGM_LONG_UPDATE	0

#include <common_def.h>
#include "emu/EmuStructs.h"
#include "emu/SoundEmu.h"
#include "emu/Resampler.h"
#include "emu/SoundDevs.h"
#include "emu/EmuCores.h"
#include "emu/dac_control.h"
#include "emu/cores/sn764intf.h"	// for SN76496_CFG
#include "emu/cores/segapcm.h"		// for SEGAPCM_CFG
#include "emu/cores/ayintf.h"		// for AY8910_CFG
#include "emu/cores/okim6258.h"		// for OKIM6258_CFG
#include "emu/cores/k054539.h"
#include "emu/cores/c140.h"
#include "emu/cores/es5503.h"
#include "emu/cores/es5506.h"
#include "emu/cores/c352.h"			// for C352_CFG

#include "vgm/dblk_compr.h"

const char* interErr = NULL;

typedef struct _vgm_file_header
{
	UINT32 fccVGM;
	UINT32 lngEOFOffset;
	UINT32 lngVersion;
	UINT32 lngHzPSG;
	UINT32 lngHzYM2413;
	UINT32 lngGD3Offset;
	UINT32 lngTotalSamples;
	UINT32 lngLoopOffset;
	UINT32 lngLoopSamples;
	UINT32 lngRate;
	UINT16 shtPSG_Feedback;
	UINT8 bytPSG_SRWidth;
	UINT8 bytPSG_Flags;
	UINT32 lngHzYM2612;
	UINT32 lngHzYM2151;
	UINT32 lngDataOffset;
	UINT32 lngHzSPCM;
	UINT32 lngSPCMIntf;
	UINT32 lngHzRF5C68;
	UINT32 lngHzYM2203;
	UINT32 lngHzYM2608;
	UINT32 lngHzYM2610;
	UINT32 lngHzYM3812;
	UINT32 lngHzYM3526;
	UINT32 lngHzY8950;
	UINT32 lngHzYMF262;
	UINT32 lngHzYMF278B;
	UINT32 lngHzYMF271;
	UINT32 lngHzYMZ280B;
	UINT32 lngHzRF5C164;
	UINT32 lngHzPWM;
	UINT32 lngHzAY8910;
	UINT8 bytAYType;
	UINT8 bytAYFlag;
	UINT8 bytAYFlagYM2203;
	UINT8 bytAYFlagYM2608;
	UINT8 bytVolumeModifier;
	UINT8 bytReserved2;
	INT8 bytLoopBase;
	UINT8 bytLoopModifier;
	UINT32 lngHzGBDMG;
	UINT32 lngHzNESAPU;
	UINT32 lngHzYMW258;
	UINT32 lngHzUPD7759;
	UINT32 lngHzOKIM6258;
	UINT8 bytOKI6258Flags;
	UINT8 bytK054539Flags;
	UINT8 bytC140Type;
	UINT8 bytReservedFlags;
	UINT32 lngHzOKIM6295;
	UINT32 lngHzK051649;
	UINT32 lngHzK054539;
	UINT32 lngHzHuC6280;
	UINT32 lngHzC140;
	UINT32 lngHzK053260;
	UINT32 lngHzPokey;
	UINT32 lngHzQSound;
	UINT32 lngHzSCSP;
	UINT32 lngExtraOffset;
	UINT32 lngHzWSwan;
	UINT32 lngHzVSU;
	UINT32 lngHzSAA1099;
	UINT32 lngHzES5503;
	UINT32 lngHzES5506;
	UINT8 bytES5503Chns;
	UINT8 bytES5506Chns;
	UINT8 bytC352ClkDiv;
	UINT8 bytESReserved;
	UINT32 lngHzX1_010;
	UINT32 lngHzC352;
	UINT32 lngHzGA20;
} VGM_HEADER;
typedef struct _vgm_linked_chip_device VGM_LINKCDEV;
struct _vgm_linked_chip_device
{
	DEV_INFO defInf;
	RESMPL_STATE resmpl;
	VGM_LINKCDEV* linkDev;
};
typedef struct _vgm_chip_device VGM_CHIPDEV;
struct _vgm_chip_device
{
	DEV_INFO defInf;
	RESMPL_STATE resmpl;
	VGM_LINKCDEV* linkDev;
	DEVFUNC_WRITE_A8D8 write8;		// write 8-bit data to 8-bit register/offset
	DEVFUNC_WRITE_A16D8 writeM8;	// write 8-bit data to 16-bit memory offset
	DEVFUNC_WRITE_A8D16 writeD16;	// write 16-bit data to 8-bit register/offset
	DEVFUNC_WRITE_A16D16 writeM16;	// write 16-bit data to 16-bit register/offset
	DEVFUNC_WRITE_MEMSIZE romSize;
	DEVFUNC_WRITE_BLOCK romWrite;
	DEVFUNC_WRITE_MEMSIZE romSizeB;
	DEVFUNC_WRITE_BLOCK romWriteB;
};

typedef struct _vgm_dac_stream VGM_DACSTRM;
struct _vgm_dac_stream
{
	DEV_INFO defInf;
	UINT8 bankID;
};
typedef struct _vgm_pcm_bank
{
	UINT32 dataSize;
	UINT8* data;
	
	UINT32 bankAlloc;
	UINT32 bankCount;
	UINT32* bankOfs;
	UINT32* bankSize;
} VGM_PCM_BANK;

#define CHANNEL_COUNT	2
#define CHIP_COUNT		0x29
#define DACSTRM_COUNT	0x10	// only 0x10 for now
#define PCM_BANK_COUNT	0x40

typedef struct InternalState
{
	UINT32 smplSize;
	UINT32 smplAlloc;
	WAVE_32BS* smplData;

	UINT32 VGMLen;
	UINT8* VGMData;
	UINT32 VGMPos;
	VGM_HEADER VGMHdr;
	UINT32 VGMSmplPos;
	UINT32 renderSmplPos;
	double flSpeedMul;
	double flRenderPos;
	VGM_CHIPDEV VGMChips[CHIP_COUNT][2];
	VGM_DACSTRM DACStreams[DACSTRM_COUNT];
	UINT32 sampleRate;

	VGM_PCM_BANK PCMBank[PCM_BANK_COUNT];
	PCM_COMPR_TBL PCMComprTbl;
	UINT32 ym2612_pcmBnkPos;

	UINT8 RF5C_Bank[2];

} InternalState;

InternalState* InternalOpen(gzFile hFile, UINT32 rate, WAVE_32BS* buf, UINT32 bufLen)
{
	InternalState* s = malloc(sizeof(InternalState));
	if(!s)
	{
		interErr = "Could not allocate internal state!\n";
		return NULL;
	}

	// gotta clear this somewhere
	memset(s->RF5C_Bank, 0, sizeof(s->RF5C_Bank));

	UINT32 tempData[2];

	s->VGMLen = 0;
	s->VGMData = NULL;
	gzread(hFile, tempData, 0x08);
	if (! memcmp(&tempData[0], "Vgm ", 4))
	{
		s->VGMLen = 0x04 + tempData[0];
		s->VGMData = (UINT8*)malloc(s->VGMLen);
		memcpy(&s->VGMData[0x00], tempData, 0x08);
		gzread(hFile, &s->VGMData[0x08], s->VGMLen - 0x08);
		
		memcpy(&tempData[0], &s->VGMData[0x34], 0x04);	// get data offset
		tempData[0] += (tempData[0] == 0x00) ? 0x40 : 0x34;
		if (tempData[0] >= 0xC0)
			memcpy(&tempData[1], &s->VGMData[0xBC], 0x04);	// get extra header offset
		else
			tempData[1] = 0x00;
		tempData[1] += (tempData[1] == 0x00) ? 0x00 : 0xBC;
		if (tempData[1] && tempData[0] > tempData[1])
			tempData[0] = tempData[1];	// the main header ends where the extra header begins
		if (tempData[0] > sizeof(VGM_HEADER))
			tempData[0] = sizeof(VGM_HEADER);	// don't copy too many bytes into VGM_HEADER struct
		memset(&s->VGMHdr, 0x00, sizeof(VGM_HEADER));
		memcpy(&s->VGMHdr, &s->VGMData[0x00], tempData[0]);
	}

	if (! s->VGMLen)
	{
		interErr = "Error reading file!\n";
		free(s);
		return NULL;
	}
	
	s->sampleRate = rate;
	s->smplSize = sizeof(WAVE_32BS);
	s->smplAlloc = bufLen / s->smplSize;
	s->smplData = buf;

	// groovy speed hack.
	s->flSpeedMul = 44100.0 / (double)rate;

	InitVGMChips(s);

	return s;
}

void InternalFree(InternalState* s)
{
	DeinitVGMChips(s);

	free(s->VGMData);
	s->VGMData = NULL;

	free(s);
}

void InternalRead(InternalState* s, UINT32 smplCount, UINT32 smplOfs)
{
	memset(s->smplData, 0, s->smplAlloc * sizeof(WAVE_32BS));

#if VGM_LONG_UPDATE
	ProcessVGM(s, smplCount, smplOfs);
#else
	for (UINT32 curSmpl = 0; curSmpl < smplCount; curSmpl ++)
	{
		ProcessVGM(s, 1, smplOfs + curSmpl);

		/*
		if ( s->smplData[smplOfs + curSmpl].L != 0 )
		{
			printf ( "%i", s->smplData[smplOfs + curSmpl].L );
		}
		*/
	}
#endif
	
	/*
#if VGM_LONG_UPDATE
	ProcessVGM(s, smplCount, 0);
#endif
	SmplPtr = (INT32*)outData;
	for (curSmpl = 0; curSmpl < smplCount; curSmpl ++, SmplPtr += sizeof(WAVE_32BS))
	{
#if ! VGM_LONG_UPDATE
		ProcessVGM(s, 1, curSmpl);
#endif
	}
	
	return curSmpl * smplSize;
	*/
}

void ProcessVGM(InternalState* s, UINT32 smplCount, UINT32 smplOfs)
{
	UINT8 curChip;
	UINT8 chipNum;
	VGM_CHIPDEV* cDev;
	VGM_LINKCDEV* clDev;
	DEV_INFO* dacDInf;
	
	ReadVGMFile(s, smplCount);
	// I know that using a for-loop has a bad performance, but it's just for testing anyway.
	for (curChip = 0x00; curChip < CHIP_COUNT; curChip ++)
	for (chipNum = 0; chipNum < 2; chipNum ++)
	{
		cDev = &s->VGMChips[curChip][chipNum];
		if (cDev->defInf.dataPtr != NULL)
			Resmpl_Execute(&cDev->resmpl, smplCount, &s->smplData[smplOfs]);
		clDev = cDev->linkDev;
		while(clDev != NULL)
		{
			if (clDev->defInf.dataPtr != NULL)
				Resmpl_Execute(&clDev->resmpl, smplCount, &s->smplData[smplOfs]);
			clDev = clDev->linkDev;
		};
	}
	
	for (curChip = 0x00; curChip < DACSTRM_COUNT; curChip ++)
	{
		dacDInf = &s->DACStreams[curChip].defInf;
		dacDInf->devDef->Update(dacDInf->dataPtr, smplCount, NULL);
	}
	
	return;
}

void InitVGMChips(InternalState* s)
{
	static const UINT32 chipOfs[CHIP_COUNT] = {
		0x0C, 0x10, 0x2C, 0x30, 0x38, 0x40, 0x44, 0x48, 0x4C, 0x50, 0x54, 0x58, 0x5C, 0x60, 0x64, 0x68, 0x6C, 0x70, 0x74,
		0x80, 0x84, 0x88, 0x8C, 0x90, 0x98, 0x9C, 0xA0, 0xA4, 0xA8, 0xAC, 0xB0, 0xB4, 0xB8,
		0xC0, 0xC4, 0xC8, 0xCC, 0xD0, 0xD8, 0xDC, 0xE0 };

	UINT8* vgmHdrArr;
	UINT8 curChip;
	UINT8 chipNum;
	UINT32 chpClk;
	DEV_GEN_CFG devCfg;
	VGM_CHIPDEV* cDev;
	VGM_LINKCDEV* clDev;
	DEV_INFO* devInf;
	UINT8 retVal;
	
	memset(&s->VGMChips, 0x00, sizeof(s->VGMChips));
	vgmHdrArr = (UINT8*)&s->VGMHdr;	// can't use VGMData due to the data offset
	for (curChip = 0x00; curChip < CHIP_COUNT; curChip ++)
	for (chipNum = 0; chipNum < 2; chipNum ++)
	{
		memcpy(&chpClk, &vgmHdrArr[chipOfs[curChip]], 0x04);
		if (! chpClk)
			continue;
		if (chipNum && ! (chpClk & 0x40000000))
			continue;
		cDev = &s->VGMChips[curChip][chipNum];
		
		cDev->defInf.dataPtr = NULL;
		cDev->linkDev = NULL;
		devCfg.emuCore = 0x00;
		devCfg.srMode = DEVRI_SRMODE_NATIVE;
		devCfg.flags = (chpClk & 0x80000000) >> 31;
		devCfg.clock = chpClk & ~0xC0000000;
		devCfg.smplRate = s->sampleRate;
		switch(curChip)
		{
		case DEVID_SN76496:
			{
				SN76496_CFG snCfg;
				
				if (! s->VGMHdr.bytPSG_SRWidth)
					s->VGMHdr.bytPSG_SRWidth = 0x10;
				if (! s->VGMHdr.shtPSG_Feedback)
					s->VGMHdr.shtPSG_Feedback = 0x09;
				//if (! VGMHdr.bytPSG_Flags)
				//	VGMHdr.bytPSG_Flags = 0x00;
				devCfg.emuCore = FCC_MAXM;
				devCfg.flags = 0x00;
				snCfg._genCfg = devCfg;
				snCfg.shiftRegWidth = s->VGMHdr.bytPSG_SRWidth;
				snCfg.noiseTaps = (UINT8)s->VGMHdr.shtPSG_Feedback;
				snCfg.segaPSG = (s->VGMHdr.bytPSG_Flags & 0x01) ? 0 : 1;
				snCfg.negate = (s->VGMHdr.bytPSG_Flags & 0x02) ? 1 : 0;
				snCfg.stereo = (s->VGMHdr.bytPSG_Flags & 0x04) ? 0 : 1;
				snCfg.clkDiv = (s->VGMHdr.bytPSG_Flags & 0x08) ? 1 : 8;
				if ((chipNum & 0x01) && (chpClk & 0x80000000))	// must be 2nd chip + T6W28 mode
					snCfg.t6w28_tone = s->VGMChips[curChip][chipNum ^ 0x01].defInf.dataPtr;
				else
					snCfg.t6w28_tone = NULL;
				
				retVal = SndEmu_Start(curChip, (DEV_GEN_CFG*)&snCfg, &cDev->defInf);
				if (retVal)
					break;
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
				
				if (cDev->defInf.devDef->SetPanning != NULL)
				{
					INT16 panPos[4] = {0x00, -0x80, +0x80, 0x00};
					cDev->defInf.devDef->SetPanning(cDev->defInf.dataPtr, panPos);
				}
			}
			break;
		case DEVID_SEGAPCM:
			{
				SEGAPCM_CFG spCfg;
				
				spCfg._genCfg = devCfg;
				spCfg.bnkshift = (s->VGMHdr.lngSPCMIntf >> 0) & 0xFF;
				spCfg.bnkmask = (s->VGMHdr.lngSPCMIntf >> 16) & 0xFF;
				retVal = SndEmu_Start(curChip, (DEV_GEN_CFG*)&spCfg, &cDev->defInf);
				if (retVal)
					break;
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A16D8, 0, (void**)&cDev->writeM8);
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&cDev->romSize);
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			}
			break;
		case DEVID_RF5C68:
		case 0x10:	// DEVID_RF5C164
			if (curChip == DEVID_RF5C68)
				devCfg.emuCore = FCC_MAME;
			else
				devCfg.emuCore = FCC_GENS;
			retVal = SndEmu_Start(DEVID_RF5C68, &devCfg, &cDev->defInf);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_A16D8, 0, (void**)&cDev->writeM8);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			break;
		case DEVID_YM2610:
			retVal = SndEmu_Start(curChip, &devCfg, &cDev->defInf);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 'A', (void**)&cDev->romSize);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 'A', (void**)&cDev->romWrite);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 'B', (void**)&cDev->romSizeB);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 'B', (void**)&cDev->romWriteB);
			break;
		case DEVID_YMF278B:
			retVal = SndEmu_Start(curChip, &devCfg, &cDev->defInf);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0x524F, (void**)&cDev->romSize);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0x524F, (void**)&cDev->romWrite);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0x5241, (void**)&cDev->romSizeB);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0x5241, (void**)&cDev->romWriteB);
			if (cDev->romWrite != NULL)
			{
				const char* romFile = "yrw801.rom";
				FILE* hFile;
				UINT32 yrwSize;
				UINT8* yrwData;
				
				hFile = fopen(romFile, "rb");
				if (hFile == NULL)
				{
					printf("Warning: Couldn't load %s!\n", romFile);
					break;
				}
				
				fseek(hFile, 0, SEEK_END);
				yrwSize = ftell(hFile);
				rewind(hFile);
				yrwData = (UINT8*)malloc(yrwSize);
				fread(yrwData, 1, yrwSize, hFile);
				fclose(hFile);
				
				cDev->romSize(cDev->defInf.dataPtr, yrwSize);
				cDev->romWrite(cDev->defInf.dataPtr, 0x00, yrwSize, yrwData);
				free(yrwData);
			}
			break;
		case DEVID_AY8910:
			{
				AY8910_CFG ayCfg;
				
				devCfg.emuCore = FCC_EMU_;
				ayCfg._genCfg = devCfg;
				ayCfg.chipType = s->VGMHdr.bytAYType;
				ayCfg.chipFlags = s->VGMHdr.bytAYFlag;
				
				retVal = SndEmu_Start(curChip, (DEV_GEN_CFG*)&ayCfg, &cDev->defInf);
				if (retVal)
					break;
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
				
				if (cDev->defInf.devDef->SetPanning != NULL)
				{
					INT16 panPos[3] = {-0x80, +0x80, 0x00};
					cDev->defInf.devDef->SetPanning(cDev->defInf.dataPtr, panPos);
				}
			}
			break;
		case DEVID_32X_PWM:
			retVal = SndEmu_Start(curChip, &devCfg, &cDev->defInf);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D16, 0, (void**)&cDev->writeD16);
			break;
		case DEVID_YMW258:
			retVal = SndEmu_Start(curChip, &devCfg, &cDev->defInf);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D16, 0, (void**)&cDev->writeD16);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&cDev->romSize);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			break;
		case DEVID_OKIM6258:
			{
				OKIM6258_CFG okiCfg;
				
				okiCfg._genCfg = devCfg;
				okiCfg.divider = (s->VGMHdr.bytOKI6258Flags & 0x03) >> 0;
				okiCfg.adpcmBits = (s->VGMHdr.bytOKI6258Flags & 0x04) ? OKIM6258_ADPCM_4B : OKIM6258_ADPCM_3B;
				okiCfg.outputBits = (s->VGMHdr.bytOKI6258Flags & 0x08) ? OKIM6258_OUT_12B : OKIM6258_OUT_10B;
				
				retVal = SndEmu_Start(curChip, (DEV_GEN_CFG*)&okiCfg, &cDev->defInf);
				if (retVal)
					break;
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
			}
			break;
		case DEVID_K054539:
			{
				if (devCfg.clock < 1000000)	// if < 1 MHz, then it's the sample rate, not the clock
					devCfg.clock *= 384;	// (for backwards compatibility with old VGM logs from 2012/13)
				devCfg.flags = s->VGMHdr.bytK054539Flags;
				
				retVal = SndEmu_Start(curChip, &devCfg, &cDev->defInf);
				if (retVal)
					break;
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A16D8, 0, (void**)&cDev->writeM8);
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&cDev->romSize);
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			}
			break;
		case DEVID_C140:
			{
				if (devCfg.clock < 1000000)	// if < 1 MHz, then it's the sample rate, not the clock
					devCfg.clock *= 384;	// (for backwards compatibility with old VGM logs from 2013/14)
				devCfg.flags = s->VGMHdr.bytC140Type;	// banking type
				
				retVal = SndEmu_Start(curChip, &devCfg, &cDev->defInf);
				if (retVal)
					break;
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A16D8, 0, (void**)&cDev->writeM8);
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&cDev->romSize);
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			}
			break;
		case DEVID_C352:
			{
				C352_CFG cCfg;
				
				cCfg._genCfg = devCfg;
				cCfg.clk_divider = s->VGMHdr.bytC352ClkDiv * 4;
				
				retVal = SndEmu_Start(curChip, (DEV_GEN_CFG*)&cCfg, &cDev->defInf);
				if (retVal)
					break;
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A16D16, 0, (void**)&cDev->writeM16);
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&cDev->romSize);
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			}
			break;
		case DEVID_QSOUND:
			retVal = SndEmu_Start(curChip, &devCfg, &cDev->defInf);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_QUICKWRITE, DEVRW_A8D16, 0, (void**)&cDev->writeD16);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&cDev->romSize);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			break;
		case DEVID_WSWAN:
			retVal = SndEmu_Start(curChip, &devCfg, &cDev->defInf);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_A16D8, 0, (void**)&cDev->writeM8);
			break;
		case DEVID_ES5503:
			{
				devCfg.flags = s->VGMHdr.bytES5503Chns;	// output channels
				
				retVal = SndEmu_Start(curChip, &devCfg, &cDev->defInf);
				if (retVal)
					break;
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			}
			break;
		case DEVID_ES5506:
			{
				devCfg.flags = s->VGMHdr.bytES5506Chns;	// output channels
				
				retVal = SndEmu_Start(curChip, &devCfg, &cDev->defInf);
				if (retVal)
					break;
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D16, 0, (void**)&cDev->writeD16);
				SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			}
			break;
		case DEVID_SCSP:
			if (devCfg.clock < 1000000)	// if < 1 MHz, then it's the sample rate, not the clock
				devCfg.clock *= 512;	// (for backwards compatibility with old VGM logs from 2012-14)
			retVal = SndEmu_Start(curChip, &devCfg, &cDev->defInf);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A16D8, 0, (void**)&cDev->writeM8);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A16D16, 0, (void**)&cDev->writeM16);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			break;
		default:
			if (curChip == DEVID_YM2612)
				devCfg.emuCore = FCC_GPGX;
			else if (curChip == DEVID_YM3812)
				devCfg.emuCore = FCC_ADLE;
			else if (curChip == DEVID_YMF262)
				devCfg.emuCore = FCC_NUKE;
			else if (curChip == DEVID_NES_APU)
				devCfg.emuCore = FCC_NSFP;
			else if (curChip == DEVID_C6280)
				devCfg.emuCore = FCC_OOTK;
			retVal = SndEmu_Start(curChip, &devCfg, &cDev->defInf);
			if (retVal)
				break;
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&cDev->write8);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A16D8, 0, (void**)&cDev->writeM8);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_MEMSIZE, 0, (void**)&cDev->romSize);
			SndEmu_GetDeviceFunc(cDev->defInf.devDef, RWF_MEMORY | RWF_WRITE, DEVRW_BLOCK, 0, (void**)&cDev->romWrite);
			if (curChip == DEVID_YM2612)
				cDev->defInf.devDef->SetOptionBits(cDev->defInf.dataPtr, 0x80);	// enable legacy mode
			else if (curChip == DEVID_GB_DMG)
				cDev->defInf.devDef->SetOptionBits(cDev->defInf.dataPtr, 0x80);	// enable legacy mode
			break;
		}
		if (retVal)
		{
			cDev->defInf.dataPtr = NULL;
			cDev->defInf.devDef = NULL;
			continue;
		}
		
		if (cDev->defInf.linkDevCount > 0 && cDev->defInf.devDef->LinkDevice != NULL)
		{
			UINT32 curLDev;
			DEVLINK_INFO* dLink;
			VGM_LINKCDEV* clParent;
			
			clParent = NULL;
			for (curLDev = 0; curLDev < cDev->defInf.linkDevCount; curLDev ++)
			{
				dLink = &cDev->defInf.linkDevs[curLDev];
				clDev = (VGM_LINKCDEV*)calloc(1, sizeof(VGM_LINKCDEV));
				if (clDev == NULL)
					break;
				clDev->linkDev = NULL;
				if (clParent == NULL)
					cDev->linkDev = clDev;
				else
					clParent->linkDev = clDev;
				
				// Note: Configuration changes for child-devices go here.
				if (dLink->devID == DEVID_AY8910)
					dLink->cfg->emuCore = FCC_EMU_;
				else if (dLink->devID == DEVID_YMF262)
					dLink->cfg->emuCore = FCC_ADLE;
				
				if (curChip == DEVID_YM2203)
					((AY8910_CFG*)dLink->cfg)->chipFlags = s->VGMHdr.bytAYFlagYM2203;
				else if (curChip == DEVID_YM2608)
					((AY8910_CFG*)dLink->cfg)->chipFlags = s->VGMHdr.bytAYFlagYM2608;
				
				retVal = SndEmu_Start(dLink->devID, dLink->cfg, &clDev->defInf);
				if (retVal)
				{
					free(cDev->linkDev);
					cDev->linkDev = NULL;
					break;
				}
				cDev->defInf.devDef->LinkDevice(cDev->defInf.dataPtr, dLink->linkID, &clDev->defInf);
				clParent = clDev;
			}
		}
		// already done by SndEmu_Start()
		//cDev->defInf.devDef->Reset(cDev->defInf.dataPtr);
		
		Resmpl_SetVals(&cDev->resmpl, 0xFF, 0x100, s->sampleRate);
		Resmpl_DevConnect(&cDev->resmpl, &cDev->defInf);
		Resmpl_Init(&cDev->resmpl);
		clDev = cDev->linkDev;
		while(clDev != NULL)
		{
			Resmpl_SetVals(&clDev->resmpl, 0xFF, 0x100, s->sampleRate);
			Resmpl_DevConnect(&clDev->resmpl, &clDev->defInf);
			Resmpl_Init(&clDev->resmpl);
			clDev = clDev->linkDev;
		}
	}
	
	devCfg.emuCore = 0x00;
	devCfg.srMode = DEVRI_SRMODE_NATIVE;
	devCfg.flags = 0x00;
	devCfg.clock = 0x00;
	devCfg.smplRate = s->sampleRate;
	for (curChip = 0x00; curChip < DACSTRM_COUNT; curChip ++)
	{
		devInf = &s->DACStreams[curChip].defInf;
		retVal = device_start_daccontrol(&devCfg, devInf);
		devInf->devDef->Reset(devInf->dataPtr);
		s->DACStreams[curChip].bankID = 0xFF;
	}
	memset(&s->PCMBank, 0x00, sizeof(VGM_PCM_BANK) * PCM_BANK_COUNT);
	memset(&s->PCMComprTbl, 0x00, sizeof(PCM_COMPR_TBL));
	
	s->VGMSmplPos = 0;
	s->renderSmplPos = 0;
	s->flRenderPos = 0.0;
	s->VGMPos = s->VGMHdr.lngDataOffset;
	s->VGMPos += s->VGMPos ? 0x34 : 0x40;
	
	return;
}

void DeinitVGMChips(InternalState* s)
{
	UINT8 curChip;
	UINT8 chipNum;
	VGM_CHIPDEV* cDev;
	VGM_LINKCDEV* clDev;
	VGM_LINKCDEV* clDevOld;
	DEV_INFO* devInf;
	VGM_PCM_BANK* pcmBnk;
	
	for (curChip = 0x00; curChip < DACSTRM_COUNT; curChip ++)
	{
		devInf = &s->DACStreams[curChip].defInf;
		devInf->devDef->Stop(devInf->dataPtr);
		devInf->dataPtr = NULL;
	}
	for (curChip = 0x00; curChip < PCM_BANK_COUNT; curChip ++)
	{
		pcmBnk = &s->PCMBank[curChip];
		free(pcmBnk->bankOfs);	pcmBnk->bankOfs = NULL;
		free(pcmBnk->bankSize);	pcmBnk->bankSize = NULL;
		free(pcmBnk->data);		pcmBnk->data = NULL;
	}
	free(s->PCMComprTbl.values.d8);	s->PCMComprTbl.values.d8 = NULL;
	
	for (curChip = 0x00; curChip < CHIP_COUNT; curChip ++)
	for (chipNum = 0; chipNum < 2; chipNum ++)
	{
		cDev = &s->VGMChips[curChip][chipNum];
		if (cDev->defInf.dataPtr == NULL)
			continue;
		
		Resmpl_Deinit(&cDev->resmpl);
		SndEmu_Stop(&cDev->defInf);
		SndEmu_FreeDevLinkData(&cDev->defInf);
		if (cDev->linkDev != NULL)
		{
			// free linked devices
			clDev = cDev->linkDev;
			cDev->linkDev = NULL;
			while(clDev != NULL)
			{
				if (clDev->defInf.dataPtr != NULL)
				{
					Resmpl_Deinit(&clDev->resmpl);
					SndEmu_Stop(&clDev->defInf);
				}
				SndEmu_FreeDevLinkData(&clDev->defInf);
				clDevOld = clDev;
				clDev = clDev->linkDev;
				free(clDevOld);
			}
		}
	}
	
	return;
}

void SendChipCommand_Data8(InternalState* s, UINT8 chipID, UINT8 chipNum, UINT8 ofs, UINT8 data)
{
	VGM_CHIPDEV* cDev;
	
	cDev = &s->VGMChips[chipID][chipNum];
	if (cDev->write8 == NULL)
		return;
	
	cDev->write8(cDev->defInf.dataPtr, ofs, data);
	return;
}

void SendChipCommand_RegData8(InternalState* s, UINT8 chipID, UINT8 chipNum, UINT8 port, UINT8 reg, UINT8 data)
{
	VGM_CHIPDEV* cDev;
	
	cDev = &s->VGMChips[chipID][chipNum];
	if (cDev->write8 == NULL)
		return;
	
	cDev->write8(cDev->defInf.dataPtr, (port << 1) | 0, reg);
	cDev->write8(cDev->defInf.dataPtr, (port << 1) | 1, data);
	return;
}

void SendChipCommand_MemData8(InternalState* s, UINT8 chipID, UINT8 chipNum, UINT16 ofs, UINT8 data)
{
	VGM_CHIPDEV* cDev;
	
	cDev = &s->VGMChips[chipID][chipNum];
	if (cDev->writeM8 == NULL)
		return;
	
	cDev->writeM8(cDev->defInf.dataPtr, ofs, data);
	return;
}

void SendChipCommand_Data16(InternalState* s, UINT8 chipID, UINT8 chipNum, UINT8 ofs, UINT16 data)
{
	VGM_CHIPDEV* cDev;
	
	cDev = &s->VGMChips[chipID][chipNum];
	if (cDev->writeD16 == NULL)
		return;
	
	cDev->writeD16(cDev->defInf.dataPtr, ofs, data);
	return;
}

void SendChipCommand_MemData16(InternalState* s, UINT8 chipID, UINT8 chipNum, UINT16 ofs, UINT16 data)
{
	VGM_CHIPDEV* cDev;
	
	cDev = &s->VGMChips[chipID][chipNum];
	if (cDev->writeM16 == NULL)
		return;
	
	cDev->writeM16(cDev->defInf.dataPtr, ofs, data);
	return;
}

void WriteChipROM(InternalState* s, UINT8 chipID, UINT8 chipNum, UINT8 memID,
						 UINT32 memSize, UINT32 dataOfs, UINT32 dataSize, const UINT8* data)
{
	VGM_CHIPDEV* cDev;
	
	cDev = &s->VGMChips[chipID][chipNum];
	if (memID == 0)
	{
		if (cDev->romSize != NULL)
			cDev->romSize(cDev->defInf.dataPtr, memSize);
		if (cDev->romWrite != NULL && dataSize)
			cDev->romWrite(cDev->defInf.dataPtr, dataOfs, dataSize, data);
	}
	else
	{
		if (cDev->romSizeB != NULL)
			cDev->romSizeB(cDev->defInf.dataPtr, memSize);
		if (cDev->romWriteB != NULL && dataSize)
			cDev->romWriteB(cDev->defInf.dataPtr, dataOfs, dataSize, data);
	}
	return;
}

void WriteChipRAM(InternalState* s, UINT8 chipID, UINT8 chipNum,
						 UINT32 dataOfs, UINT32 dataSize, const UINT8* data)
{
	VGM_CHIPDEV* cDev;
	
	cDev = &s->VGMChips[chipID][chipNum];
	if (cDev->romWrite != NULL)
		cDev->romWrite(cDev->defInf.dataPtr, dataOfs, dataSize, data);
	return;
}

static const UINT8 VGM_CMDLEN[0x10] =
{	0x01, 0x01, 0x01, 0x02, 0x03, 0x03, 0x00, 0x01,
	0x01, 0x00, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05};
typedef struct
{
	UINT8 chipID;
	UINT8 memIdx;
} VGM_ROMDUMP_IDS;
static const VGM_ROMDUMP_IDS VGMROM_CHIPS[0x14] =
{	{0x04, 0x00},	// SegaPCM
	{0x07, 0x00},	// YM2608 DeltaT
	{0x08, 0x00},	// YM2610 ADPCM
	{0x08, 0x01},	// YM2610 DeltaT
	{0x0D, 0x00},	// YMF278B ROM
	{0x0E, 0x00},	// YMF271
	{0x0F, 0x00},	// YMZ280B
	{0x0D, 0x01},	// YMF278B RAM
	{0x0B, 0x00},	// Y8950 DeltaT
	{0x15, 0x00},	// YMW258/MultiPCM
	{0x16, 0x00},	// uPD7759
	{0x18, 0x00},	// OKIM6295
	{0x1A, 0x00},	// K054539
	{0x1C, 0x00},	// C140
	{0x1D, 0x00},	// K053260
	{0x1F, 0x00},	// QSound
	{0x25, 0x00},	// ES5506
	{0x26, 0x00},	// X1-010
	{0x27, 0x00},	// C352
	{0x28, 0x00},	// GA20
};
static const VGM_ROMDUMP_IDS VGMRAM_CHIPS1[0x03] =
{	{0x05, 0x00},	// RF5C68
	{0x10, 0x00},	// RF5C164
	{0x14, 0x00},	// NES APU
};
static const VGM_ROMDUMP_IDS VGMRAM_CHIPS2[0x02] =
{	{0x20, 0x00},	// SCSP
	{0x24, 0x00},	// ES5503
};

typedef struct
{
	UINT8 chipID;
	UINT8 cmdType;
} VGM_CMDTYPES;
#define CMDTYPE_DUMMY		0x00
#define CMDTYPE_R_D8		0x01	// Register + Data (8-bit)
#define CMDTYPE_CP_R_D8		0x02	// Port (in command byte) + Register + Data (8-bit)
#define CMDTYPE_P_R_D8		0x03	// Port + Register + Data (8-bit)
#define CMDTYPE_O8_D8		0x04	// Offset (8-bit) + Data (8-bit)
#define CMDTYPE_O16_D8		0x05	// Offset (16-bit) + Data (8-bit)
#define CMDTYPE_O8_D16		0x06	// Offset (8-bit) + Data (16-bit)
#define CMDTYPE_O16_D16		0x07	// Offset (16-bit) + Data (16-bit)
#define CMDTYPE_P_O8_D8		0x08	// Port + Offset (8-bit) + Data (8-bit)
#define CMDTYPE_R2_D8		0x09	// Register [with dual-chip bit] + Data (8-bit)
#define CMDTYPE_SPCM_MEM	0x80	// SegaPCM Memory Write
#define CMDTYPE_RF5C_MEM	0x81	// RF5Cxx Memory Write
#define CMDTYPE_PWM_REG		0x82	// PWM register write (4-bit offset, 12-bit data)
#define CMDTYPE_QSOUND		0x83	// QSound register write (16-bit data, 8-bit offset)
#define CMDTYPE_WSWAN_REG	0x84	// WonderSwan register write (O8_D8 with remapping)
#define CMDTYPE_NES_REG		0x85	// NES APU register write (O8_D8 with remapping)
#define CMDTYPE_YMW_BANK	0x86	// YMW258 bank write (CMDTYPE_O8_D16 with remapping to register)
#define CMDTYPE_SAA_REG		0x87	// SAA1099 register write (CMDTYPE_R_D8 with remapping)
#define CMDTYPE_RF5C_REG	0x88	// RF5C68/164 register write (CMDTYPE_O8_D8 with bank cache)
static const VGM_CMDTYPES VGM_CMDS_50[0x10] =
{
	{0x00,	CMDTYPE_DUMMY},		// 50 SN76496 (handled separately)
	{0x01,	CMDTYPE_R_D8},		// 51 YM2413
	{0x02,	CMDTYPE_CP_R_D8},	// 52 YM2612
	{0x02,	CMDTYPE_CP_R_D8},	// 53 YM2612
	{0x03,	CMDTYPE_R_D8},		// 54 YM2151
	{0x06,	CMDTYPE_R_D8},		// 55 YM2203
	{0x07,	CMDTYPE_CP_R_D8},	// 56 YM2608
	{0x07,	CMDTYPE_CP_R_D8},	// 57 YM2608
	{0x08,	CMDTYPE_CP_R_D8},	// 58 YM2610
	{0x08,	CMDTYPE_CP_R_D8},	// 59 YM2610
	{0x09,	CMDTYPE_R_D8},		// 5A YM3812
	{0x0A,	CMDTYPE_R_D8},		// 5B YM3526
	{0x0B,	CMDTYPE_R_D8},		// 5C Y8950
	{0x0F,	CMDTYPE_R_D8},		// 5D YMZ280B
	{0x0C,	CMDTYPE_CP_R_D8},	// 5E YMF262
	{0x0C,	CMDTYPE_CP_R_D8},	// 5F YMF262
};
static const VGM_CMDTYPES VGM_CMDS_B0[0x10] =
{
	{0x05,	CMDTYPE_RF5C_REG},	// B0 RF5C68
	{0x10,	CMDTYPE_RF5C_REG},	// B1 RF5C164
	{0x11,	CMDTYPE_PWM_REG},	// B2 PWM
	{0x13,	CMDTYPE_O8_D8},		// B3 GameBoy DMG
	{0x14,	CMDTYPE_NES_REG},	// B4 NES APU
	{0x15,	CMDTYPE_O8_D8},		// B5 YMW258/MultiPCM
	{0x16,	CMDTYPE_O8_D8},		// B6 uPD7759
	{0x17,	CMDTYPE_O8_D8},		// B7 OKIM6258
	{0x18,	CMDTYPE_O8_D8},		// B8 OKIM6295
	{0x1B,	CMDTYPE_O8_D8},		// B9 HuC6280
	{0x1D,	CMDTYPE_O8_D8},		// BA K053260
	{0x1E,	CMDTYPE_O8_D8},		// BB Pokey
	{0x21,	CMDTYPE_WSWAN_REG},	// BC WonderSwan (register write)
	{0x23,	CMDTYPE_SAA_REG},	// BD SAA1099
	{0x25,	CMDTYPE_O8_D8},		// BE ES5506
	{0x28,	CMDTYPE_O8_D8},		// BF GA20
};
static const VGM_CMDTYPES VGM_CMDS_C0[0x10] =
{
	{0x04,	CMDTYPE_SPCM_MEM},	// C0 Sega PCM
	{0x05,	CMDTYPE_RF5C_MEM},	// C1 RF5C68
	{0x10,	CMDTYPE_RF5C_MEM},	// C2 RF5C164
	{0x15,	CMDTYPE_YMW_BANK},	// C3 YMW258/MultiPCM bank offset
	{0x1F,	CMDTYPE_QSOUND},	// C4 QSound
	{0x20,	CMDTYPE_O16_D8},	// C5 SCSP
	{0x21,	CMDTYPE_O16_D8},	// C6 WonderSwan (memory write)
	{0x22,	CMDTYPE_O16_D8},	// C7 VSU-VUE
	{0x26,	CMDTYPE_O16_D8},	// C8 X1-010
	{0xFF,	CMDTYPE_DUMMY},		// C9 [unused]
	{0xFF,	CMDTYPE_DUMMY},		// CA [unused]
	{0xFF,	CMDTYPE_DUMMY},		// CB [unused]
	{0xFF,	CMDTYPE_DUMMY},		// CC [unused]
	{0xFF,	CMDTYPE_DUMMY},		// CD [unused]
	{0xFF,	CMDTYPE_DUMMY},		// CE [unused]
	{0xFF,	CMDTYPE_DUMMY},		// CF [unused]
};
static const VGM_CMDTYPES VGM_CMDS_D0[0x10] =
{
	{0x0D,	CMDTYPE_P_R_D8},	// D0 YMF278B
	{0x0E,	CMDTYPE_P_R_D8},	// D1 YMF271
	{0x19,	CMDTYPE_P_R_D8},	// D2 K051649/SCC1
	{0x1A,	CMDTYPE_O16_D8},	// D3 K054539
	{0x1C,	CMDTYPE_O16_D8},	// D4 C140
	{0x24,	CMDTYPE_P_O8_D8},	// D5 ES5503
	{0x25,	CMDTYPE_O16_D8},	// D6 ES5506
	{0xFF,	CMDTYPE_DUMMY},		// D7 [unused]
	{0xFF,	CMDTYPE_DUMMY},		// D8 [unused]
	{0xFF,	CMDTYPE_DUMMY},		// D9 [unused]
	{0xFF,	CMDTYPE_DUMMY},		// DA [unused]
	{0xFF,	CMDTYPE_DUMMY},		// DB [unused]
	{0xFF,	CMDTYPE_DUMMY},		// DC [unused]
	{0xFF,	CMDTYPE_DUMMY},		// DD [unused]
	{0xFF,	CMDTYPE_DUMMY},		// DE [unused]
	{0xFF,	CMDTYPE_DUMMY},		// DF [unused]
};
static const VGM_CMDTYPES VGM_CMDS_E0[0x10] =
{
	{0xFF,	CMDTYPE_DUMMY},		// E0 [unused]
	{0x27,	CMDTYPE_O16_D16},	// E1 C352
	{0xFF,	CMDTYPE_DUMMY},		// E2 [unused]
	{0xFF,	CMDTYPE_DUMMY},		// E3 [unused]
	{0xFF,	CMDTYPE_DUMMY},		// E4 [unused]
	{0xFF,	CMDTYPE_DUMMY},		// E5 [unused]
	{0xFF,	CMDTYPE_DUMMY},		// E6 [unused]
	{0xFF,	CMDTYPE_DUMMY},		// E7 [unused]
	{0xFF,	CMDTYPE_DUMMY},		// E8 [unused]
	{0xFF,	CMDTYPE_DUMMY},		// E9 [unused]
	{0xFF,	CMDTYPE_DUMMY},		// EA [unused]
	{0xFF,	CMDTYPE_DUMMY},		// EB [unused]
	{0xFF,	CMDTYPE_DUMMY},		// EC [unused]
	{0xFF,	CMDTYPE_DUMMY},		// ED [unused]
	{0xFF,	CMDTYPE_DUMMY},		// EE [unused]
	{0xFF,	CMDTYPE_DUMMY},		// EF [unused]
};

void DoRAMOfsPatches(InternalState* s, UINT8 chipID, UINT8 chipNum, UINT32* dataOfs, UINT32* dataSize)
{
	switch(chipID)
	{
	case 0x05:
		// RF5C68 bank patch
		(*dataOfs) |= (s->RF5C_Bank[0] << 12);
		break;
	case 0x10:
		(*dataOfs) |= (s->RF5C_Bank[1] << 12);
		break;
	}
	return;
}

UINT32 DoVgmCommand(InternalState* s,UINT8 cmd, const UINT8* data)
{
	UINT8 chipID;
	
	if (cmd >= 0x70 && cmd <= 0x7F)
	{
		s->VGMSmplPos += 1 + (cmd & 0x0F);
		return 0x01;
	}
	else if (cmd >= 0x80 && cmd <= 0x8F)
	{
		UINT8 data = s->PCMBank[0x00].data[s->ym2612_pcmBnkPos];
		SendChipCommand_RegData8(s, DEVID_YM2612, 0x00, 0x00, 0x2A, data);
		s->ym2612_pcmBnkPos ++;
		s->VGMSmplPos += (cmd & 0x0F);
		return 0x01;
	}
	
	chipID = 0;
	if (cmd == 0x30)
	{
		chipID = 1;
		cmd += 0x20;
	}
	else if (cmd == 0x3F)
	{
		chipID = 1;
		cmd += 0x10;
	}
	else if (cmd >= 0xA1 && cmd <= 0xAF)
	{
		chipID = 1;
		cmd -= 0x50;
	}
	switch(cmd)
	{
	case 0x61:
		s->VGMSmplPos += (data[0x01] << 0) | (data[0x02] << 8);
		return 0x03;
	case 0x62:
		s->VGMSmplPos += 735;
		return 0x01;
	case 0x63:
		s->VGMSmplPos += 882;
		return 0x01;
	case 0x66:
		printf("VGM end.\n");
		return 0x00;	// terminate
	case 0x67:
		{
			UINT32 dblkLen;
			UINT8 dblkType;
			UINT32 memSize;
			UINT32 dataOfs;
			UINT32 dataSize;
			const VGM_ROMDUMP_IDS* romChip;
			
			dblkType = data[0x02];
			memcpy(&dblkLen, &data[0x03], 0x04);
			chipID = (dblkLen & 0x80000000) >> 31;
			dblkLen &= 0x7FFFFFFF;
			
			switch(dblkType & 0xC0)
			{
			case 0x00:	// uncompressed data block
			case 0x40:	// compressed data block
				if (dblkType == 0x7F)
				{
					ReadPCMComprTable(dblkLen, &data[0x07], &s->PCMComprTbl);
				}
				else
				{
					VGM_PCM_BANK* pcmBnk = &s->PCMBank[dblkType & 0x3F];
					UINT32 dataLen = dblkLen;
					const UINT8* dataPtr = &data[0x07];
					PCM_CDB_INF dbCI;
					
					if (dblkType & 0x40)
					{
						ReadComprDataBlkHdr(dblkLen, dataPtr, &dbCI);
						dbCI.cmprInfo.comprTbl = &s->PCMComprTbl;
						dataLen = dbCI.decmpLen;
					}
					
					if (pcmBnk->bankCount >= pcmBnk->bankAlloc)
					{
						pcmBnk->bankAlloc += 0x10;
						pcmBnk->bankOfs = (UINT32*)realloc(pcmBnk->bankOfs, pcmBnk->bankAlloc * sizeof(UINT32));
						pcmBnk->bankSize = (UINT32*)realloc(pcmBnk->bankSize, pcmBnk->bankAlloc * sizeof(UINT32));
					}
					pcmBnk->bankOfs[pcmBnk->bankCount] = pcmBnk->dataSize;
					pcmBnk->bankSize[pcmBnk->bankCount] = dataLen;
					pcmBnk->bankCount ++;
					
					pcmBnk->data = (UINT8*)realloc(pcmBnk->data, pcmBnk->dataSize + dataLen);
					if (dblkType & 0x40)
					{
						DecompressDataBlk(dataLen, &pcmBnk->data[pcmBnk->dataSize],
											dblkLen - dbCI.hdrSize, &dataPtr[dbCI.hdrSize], &dbCI.cmprInfo);
					}
					else
					{
						memcpy(&pcmBnk->data[pcmBnk->dataSize], dataPtr, dataLen);
					}
					pcmBnk->dataSize += dataLen;
					
					// TODO: refresh DAC Stream pointers
				}
				break;
			case 0x80:	// ROM/RAM write
				dblkType &= 0x3F;
				if (dblkType >= 0x14)
					break;
				romChip = &VGMROM_CHIPS[dblkType];
				memcpy(&memSize, &data[0x07], 0x04);
				memcpy(&dataOfs, &data[0x0B], 0x04);
				dataSize = dblkLen - 0x08;
				WriteChipROM(s, romChip->chipID, chipID, romChip->memIdx,
							memSize, dataOfs, dataSize, &data[0x0F]);
				break;
			case 0xC0:	// RAM Write
				if (dblkType == 0xFF)
					break;	// reserved
				dblkType &= 0x3F;
				if (! (dblkType & 0x20))
				{
					romChip = &VGMRAM_CHIPS1[dblkType & 0x1F];
					dataOfs = 0x00;
					memcpy(&dataOfs, &data[0x07], 0x02);
					dataSize = dblkLen - 0x02;
					DoRAMOfsPatches(s, romChip->chipID, chipID, &dataOfs, &dataSize);
					WriteChipRAM(s, romChip->chipID, chipID, dataOfs, dataSize, &data[0x09]);
				}
				else
				{
					romChip = &VGMRAM_CHIPS2[dblkType & 0x1F];
					memcpy(&dataOfs, &data[0x07], 0x04);
					dataSize = dblkLen - 0x04;
					DoRAMOfsPatches(s, romChip->chipID, chipID, &dataOfs, &dataSize);
					WriteChipRAM(s, romChip->chipID, chipID, dataOfs, dataSize, &data[0x0B]);
				}
				break;
			}
			return 0x07 + dblkLen;
		}
	case 0x68:
		return 0x0C;
	case 0xE0:	// Seek to PCM Data Bank Pos
		s->ym2612_pcmBnkPos = (data[0x01] << 0) | (data[0x02] << 8) | (data[0x03] << 16) | (data[0x04] << 24);
		return 0x05;
	case 0x31:	// Set AY8910 stereo mask
		chipID = (data[0x01] & 0x80) >> 7;
		// TODO: assign a register or do a special function call
		//if (data[0x01] & 0x40)
		//	SendChipCommand_Data8(0x06, chipID, 0xFF, data[0x01] & 0x3F);	// YM2203 SSG
		//else
		//	SendChipCommand_Data8(0x12, chipID, 0xFF, data[0x01] & 0x3F);	// AY8910 SSG
		return 0x02;
	case 0x4F:	// SN76489 GG Stereo
		SendChipCommand_Data8(s, 0x00, chipID, 0x01, data[0x01]);
		return 0x02;
	case 0x50:	// SN76489
		SendChipCommand_Data8(s, 0x00, chipID, 0x00, data[0x01]);
		return 0x02;
	case 0x90:	// DAC Ctrl: Setup Chip
		if (data[0x01] < DACSTRM_COUNT)
		{
			VGM_DACSTRM* dacStrm = &s->DACStreams[data[0x01]];
			UINT8 chipType = data[0x02] & 0x7F;
			UINT8 chipID = (data[0x02] & 0x80) >> 7;
			VGM_CHIPDEV* destChip = &s->VGMChips[chipType][chipID];
			UINT16 chipCmd = (data[0x03] << 8) | (data[0x04] << 0);
			daccontrol_setup_chip(dacStrm->defInf.dataPtr, &destChip->defInf, chipType, chipCmd);
		}
		return 0x05;
	case 0x91:	// DAC Ctrl: Set Data
		if (data[0x01] < DACSTRM_COUNT)
		{
			VGM_DACSTRM* dacStrm = &s->DACStreams[data[0x01]];
			VGM_PCM_BANK* pcmBnk;
			dacStrm->bankID = data[0x02];
			pcmBnk = &s->PCMBank[dacStrm->bankID];
			daccontrol_set_data(dacStrm->defInf.dataPtr, pcmBnk->data, pcmBnk->dataSize, data[0x03], data[0x04]);
		}
		return 0x05;
	case 0x92:	// DAC Ctrl: Set Freq
		if (data[0x01] < DACSTRM_COUNT)
		{
			VGM_DACSTRM* dacStrm = &s->DACStreams[data[0x01]];
			UINT32 freq = (data[0x02] << 0) | (data[0x03] << 8) | (data[0x04] << 16) | (data[0x05] << 24);
			daccontrol_set_frequency(dacStrm->defInf.dataPtr, freq);
		}
		return 0x06;
	case 0x93:	// DAC Ctrl: Play Data (verbose)
		if (data[0x01] < DACSTRM_COUNT)
		{
			VGM_DACSTRM* dacStrm = &s->DACStreams[data[0x01]];
			UINT32 startOfs = (data[0x02] << 0) | (data[0x03] << 8) | (data[0x04] << 16) | (data[0x05] << 24);
			UINT32 soundLen = (data[0x07] << 0) | (data[0x08] << 8) | (data[0x09] << 16) | (data[0x0A] << 24);
			daccontrol_start(dacStrm->defInf.dataPtr, startOfs, data[0x06], soundLen);
		}
		return 0x0B;
	case 0x94:	// DAC Ctrl: Stop immediately
		if (data[0x01] < DACSTRM_COUNT)
		{
			VGM_DACSTRM* dacStrm = &s->DACStreams[data[0x01]];
			daccontrol_stop(dacStrm->defInf.dataPtr);
		}
		else if (data[0x01] == 0xFF)
		{
			for (chipID = 0x00; chipID < DACSTRM_COUNT; chipID ++)
				daccontrol_stop(s->DACStreams[chipID].defInf.dataPtr);
		}
		return 0x02;
	case 0x95:	// DAC Ctrl: Play Data (using sound ID)
		if (data[0x01] < DACSTRM_COUNT)
		{
			VGM_DACSTRM* dacStrm = &s->DACStreams[data[0x01]];
			VGM_PCM_BANK* pcmBnk = &s->PCMBank[dacStrm->bankID];
			UINT16 sndID = (data[0x02] << 0) | (data[0x03] << 8);
			UINT32 startOfs = pcmBnk->bankOfs[sndID];
			UINT32 soundLen = pcmBnk->bankSize[sndID];
			UINT8 flags = DCTRL_LMODE_BYTES |
						((data[0x04] & 0x10) << 0) |	// Reverse Mode
						((data[0x04] & 0x01) << 7);		// Looping
			daccontrol_start(dacStrm->defInf.dataPtr, startOfs, flags, soundLen);
		}
		return 0x05;
	}
	{
		VGM_CMDTYPES cmdType = {0xFF, CMDTYPE_DUMMY};
		
		switch(cmd & 0xF0)
		{
		case 0x50:
			cmdType = VGM_CMDS_50[cmd & 0x0F];
			break;
		case 0xA0:
			if (cmd == 0xA0)	// AY8910
			{
				cmdType.chipID = 0x12;
				cmdType.cmdType = CMDTYPE_R2_D8;
				break;
			}
			break;
		case 0xB0:
			cmdType = VGM_CMDS_B0[cmd & 0x0F];
			break;
		case 0xC0:
			cmdType = VGM_CMDS_C0[cmd & 0x0F];
			break;
		case 0xD0:
			cmdType = VGM_CMDS_D0[cmd & 0x0F];
			break;
		case 0xE0:
			cmdType = VGM_CMDS_E0[cmd & 0x0F];
			break;
		default:
			printf("Stopping at unknown command %02X.\n", cmd);
			break;
		}
		switch(cmdType.cmdType)
		{
		case CMDTYPE_R_D8:	// Register + Data (8-bit)
			SendChipCommand_RegData8(s, cmdType.chipID, chipID, 0, data[0x01], data[0x02]);
			break;
		case CMDTYPE_CP_R_D8:	// Port (in command byte) + Register + Data (8-bit)
			SendChipCommand_RegData8(s, cmdType.chipID, chipID, cmd & 0x01, data[0x01], data[0x02]);
			break;
		case CMDTYPE_R2_D8:	// Register + Data (8-bit)
			chipID = (data[0x01] & 0x80) >> 7;
			SendChipCommand_RegData8(s, cmdType.chipID, chipID, 0, data[0x01] & 0x7F, data[0x02]);
			break;
		case CMDTYPE_P_R_D8:	// Port + Register + Data (8-bit)
			chipID = (data[0x01] & 0x80) >> 7;
			SendChipCommand_RegData8(s, cmdType.chipID, chipID, data[0x01] & 0x7F, data[0x02], data[0x03]);
			break;
		case CMDTYPE_O8_D8:		// Offset (8-bit) + Data (8-bit)
			chipID = (data[0x01] & 0x80) >> 7;
			SendChipCommand_Data8(s, cmdType.chipID, chipID, data[0x01] & 0x7F, data[0x02]);
			break;
		case CMDTYPE_O16_D8:	// Offset (16-bit) + Data (8-bit)
			{
				UINT16 ofs;
				
				chipID = (data[0x01] & 0x80) >> 7;
				ofs = ((data[0x01] & 0x7F) << 8) | ((data[0x02]) << 0);
				SendChipCommand_MemData8(s, cmdType.chipID, chipID, ofs, data[0x03]);
			}
			break;
		case CMDTYPE_O8_D16:
			{
				UINT16 value;
				
				chipID = (data[0x01] & 0x80) >> 7;
				value = (data[0x02] << 0) | (data[0x03] << 8);
				SendChipCommand_Data16(s, cmdType.chipID, chipID, data[0x01] & 0x7F, value);
			}
			break;
		case CMDTYPE_O16_D16:
			{
				UINT16 ofs;
				UINT16 value;
				
				chipID = (data[0x01] & 0x80) >> 7;
				ofs = ((data[0x01] & 0x7F) << 8) | ((data[0x02]) << 0);
				value = (data[0x03] << 8) | (data[0x04] << 0);
				SendChipCommand_MemData16(s, cmdType.chipID, chipID, ofs, value);
			}
			break;
		case CMDTYPE_P_O8_D8:
			chipID = (data[0x01] & 0x80) >> 7;
			SendChipCommand_Data8(s, cmdType.chipID, chipID, data[0x02], data[0x03]);
			break;
		case CMDTYPE_SPCM_MEM:	// SegaPCM Memory Write
		case CMDTYPE_RF5C_MEM:	// RF5Cxx Memory Write
			{
				UINT16 memOfs;
				memOfs = (data[0x01] << 0) | (data[0x02] << 8);
				if (cmdType.cmdType == CMDTYPE_SPCM_MEM)
				{
					chipID = (memOfs & 0x8000) >> 15;
					memOfs &= 0x7FFF;
				}
				SendChipCommand_MemData8(s, cmdType.chipID, chipID, memOfs, data[0x03]);
			}
			break;
		case CMDTYPE_PWM_REG:	// PWM register write (4-bit offset, 12-bit data)
			{
				UINT8 ofs;
				UINT16 value;
				
				ofs = (data[0x01] & 0xF0) >> 4;
				value = ((data[0x01] & 0x0F) << 8) | ((data[0x02]) << 0);
				SendChipCommand_Data16(s, cmdType.chipID, chipID, ofs, value);
			}
			break;
		case CMDTYPE_QSOUND:
			if (s->VGMChips[cmdType.chipID][chipID].writeD16 != NULL)
			{
				UINT16 value;
				
				value = (data[0x01] << 8) | (data[0x02] << 0);
				SendChipCommand_Data16(s, cmdType.chipID, chipID, data[0x03], value);
			}
			else
			{
				SendChipCommand_Data8(s, cmdType.chipID, chipID, 0x00, data[0x01]);	// Data MSB
				SendChipCommand_Data8(s, cmdType.chipID, chipID, 0x01, data[0x02]);	// Data LSB
				SendChipCommand_Data8(s, cmdType.chipID, chipID, 0x02, data[0x03]);	// Register
			}
			break;
		case CMDTYPE_WSWAN_REG:	// Offset (8-bit) + Data (8-bit)
			chipID = (data[0x01] & 0x80) >> 7;
			SendChipCommand_Data8(s, cmdType.chipID, chipID, 0x80 + (data[0x01] & 0x7F), data[0x02]);
			break;
		case CMDTYPE_NES_REG:	// Offset (8-bit) + Data (8-bit)
			{
				UINT8 ofs;
				
				chipID = (data[0x01] & 0x80) >> 7;
				ofs = data[0x01] & 0x7F;
				// remap FDS registers
				if (ofs == 0x3F)
					ofs = 0x23;	// FDS I/O enable
				else if ((ofs & 0xE0) == 0x20)
					ofs = 0x80 | (ofs & 0x1F);	// FDS register
				SendChipCommand_Data8(s, cmdType.chipID, chipID, ofs, data[0x02]);
			}
			break;
		case CMDTYPE_YMW_BANK:
			{
				UINT8 bankmask;
				
				chipID = (data[0x01] & 0x80) >> 7;
				bankmask = data[0x01] & 0x03;
				// data[0x03] is ignored as we don't support YMW258 ROMs > 16 MB
				if (bankmask == 0x03 && ! (data[0x02] & 0x08))
				{
					// 1 MB banking (reg 0x10)
					SendChipCommand_Data8(s, cmdType.chipID, chipID, 0x10, data[0x02] / 0x10);
				}
				else
				{
					// 512 KB banking (regs 0x11/0x12)
					if (bankmask & 0x02)	// low bank
						SendChipCommand_Data8(s, cmdType.chipID, chipID, 0x11, data[0x02] / 0x08);
					if (bankmask & 0x01)	// high bank
						SendChipCommand_Data8(s, cmdType.chipID, chipID, 0x12, data[0x02] / 0x08);
				}
			}
		case CMDTYPE_SAA_REG:	// Offset (8-bit) + Data (8-bit)
			chipID = (data[0x01] & 0x80) >> 7;
			SendChipCommand_Data8(s, cmdType.chipID, chipID, 0x01, data[0x01] & 0x7F);
			SendChipCommand_Data8(s, cmdType.chipID, chipID, 0x00, data[0x02]);
			break;
		case CMDTYPE_RF5C_REG:	// Offset (8-bit) + Data (8-bit)
			{
				UINT8 ofs;
				
				chipID = (data[0x01] & 0x80) >> 7;
				ofs = data[0x01] & 0x7F;
				SendChipCommand_Data8(s, cmdType.chipID, chipID, ofs, data[0x02]);
				
				// RF5C68 bank patch
				if (ofs == 0x07 && ! (data[0x02] & 0x40))
				{
					UINT8 cID = (s, cmdType.chipID == 0x10) ? 1 : 0;
					s->RF5C_Bank[cID] = data[0x02] & 0x0F;
				}
			}
			break;
		}
	}
	return VGM_CMDLEN[cmd >> 4];
}

void ReadVGMFile(InternalState* s, UINT32 samples)
{
	UINT32 cmdLen;
	
	if (! s->VGMPos)
		return;
	
	s->flRenderPos += (double)samples * s->flSpeedMul;
	//s->renderSmplPos += samples;
	s->renderSmplPos = (UINT32)s->flRenderPos;
	while(s->VGMSmplPos <= s->renderSmplPos)
	{
		cmdLen = DoVgmCommand(s, s->VGMData[s->VGMPos], &s->VGMData[s->VGMPos]);
		if (! cmdLen)
		{
			s->VGMPos = 0x00;
			break;
		}
		
		s->VGMPos += cmdLen;
	}
	
	return;
}
