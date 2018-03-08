#include <libvgmplay.h>

#define AUDDRV_WAVEWRITE

#ifdef _WIN32
//#define _WIN32_WINNT	0x500	// for GetConsoleWindow()
#include <windows.h>
#ifdef _DEBUG
#include <crtdbg.h>
#endif
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <conio.h>
int __cdecl _getch(void);	// from conio.h
#else
#define _getch	getchar
#include <unistd.h>		// for usleep()
#define	Sleep(msec)	usleep(msec * 1000)
#endif

#include <common_def.h>
#include "audio/AudioStream.h"
#include "audio/AudioStream_SpcDrvFuns.h"
#include "emu/Resampler.h"

VgmStream* vgmstream;
static UINT32 smplSize;
static void* audDrv;
static void* audDrvLog;
static UINT32 bufferSamples;
static VgmSample16* smplData;
static volatile bool canRender;

static UINT32 FillBuffer(void* drvStruct, void* userParam, UINT32 bufSize, void* data);

int main(int argc, char** argv)
{
	UINT8 retVal;
	UINT32 drvCount;
	UINT32 idWavOut;
	UINT32 idWavOutDev;
	UINT32 idWavWrt;
	AUDDRV_INFO* drvInfo;
	AUDIO_OPTS* opts;

	const UINT32 rate = 48000;

	if (argc < 2)
	{
		printf("Usage: libvgmplaytest vgmfile.vgz\n");
		return 0;
	}

	vgmstream = vgmplayOpenFile(argv[1], rate, rate * 1);
	if (!vgmstream)
	{
		printf("Error opening vgm stream: %s\n", vgmplayGetError());
		return 1;
	}

	printf("Opening Audio Device ...\n");
	Audio_Init();
	drvCount = Audio_GetDriverCount();
	if (! drvCount)
		goto Exit_AudDeinit;
	
	idWavOut = 1;
	idWavOutDev = 0;
	idWavWrt = (UINT32)-1;
	//idWavWrt = 0;

	Audio_GetDriverInfo(idWavOut, &drvInfo);
	printf("Using driver %s.\n", drvInfo->drvName);
	retVal = AudioDrv_Init(idWavOut, &audDrv);
	if (retVal)
	{
		printf("WaveOut: Drv Init Error: %02X\n", retVal);
		goto Exit_AudDeinit;
	}
#ifdef AUDDRV_DSOUND
	if (drvInfo->drvSig == ADRVSIG_DSOUND)
		SetupDirectSound(audDrv);
#endif

	opts = AudioDrv_GetOptions(audDrv);
	opts->sampleRate = rate;
	opts->numChannels = 2;
	opts->numBitsPerSmpl = 16;
	smplSize = opts->numChannels * opts->numBitsPerSmpl / 8;

	canRender = false;
	AudioDrv_SetCallback(audDrv, FillBuffer, NULL);
	printf("Opening Device %u ...\n", idWavOutDev);
	retVal = AudioDrv_Start(audDrv, idWavOutDev);
	if (retVal)
	{
		printf("Dev Init Error: %02X\n", retVal);
		goto Exit_AudDrvDeinit;
	}

	bufferSamples = AudioDrv_GetBufferSize(audDrv) / smplSize;
	
	audDrvLog = NULL;
	if (idWavWrt != (UINT32)-1)
	{
		retVal = AudioDrv_Init(idWavWrt, &audDrvLog);
		if (retVal)
			audDrvLog = NULL;
	}
	if (audDrvLog != NULL)
	{
#ifdef AUDDRV_WAVEWRITE
		void* aDrv = AudioDrv_GetDrvData(audDrvLog);
		WavWrt_SetFileName(aDrv, "waveOut.wav");
#endif
		retVal = AudioDrv_Start(audDrvLog, 0);
		if (retVal)
			AudioDrv_Deinit(&audDrvLog);
	}
	if (audDrvLog != NULL)
		AudioDrv_DataForward_Add(audDrv, audDrvLog);
	
	canRender = true;
	while(1) // main loop
	{
		int inkey = getchar();
		if (toupper(inkey) == 'P')
			AudioDrv_Pause(audDrv);
		else if (toupper(inkey) == 'R')
			AudioDrv_Resume(audDrv);
		else
			break;
		while(getchar() != '\n')
			;
	}
	canRender = false;

	retVal = AudioDrv_Stop(audDrv);
	if (audDrvLog != NULL)
		retVal = AudioDrv_Stop(audDrvLog);

Exit_AudDrvDeinit:
	AudioDrv_Deinit(&audDrv);
	if (audDrvLog != NULL)
		AudioDrv_Deinit(&audDrvLog);
Exit_AudDeinit:
	Audio_Deinit();
	vgmplayClose(vgmstream);
	printf("Done.\n");

#if defined(_DEBUG) && (_MSC_VER >= 1400)
	if (_CrtDumpMemoryLeaks())
		_getch();
#endif

	return 0;
}

static UINT32 FillBuffer(void* drvStruct, void* userParam, UINT32 bufSize, void* data)
{
	UINT32 smplCount = bufSize / smplSize;
	if (! smplCount)
		return 0;
	
	if (! canRender)
	{
		memset(data, 0x00, smplCount * smplSize);
		return smplCount * smplSize;
	}
	
	if (smplCount > bufferSamples)
		smplCount = bufferSamples;
	
	return vgmplayRead16(vgmstream, (VgmSample16*)data, smplCount);
}
