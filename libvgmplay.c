#include <libvgmplay.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <zlib.h>
#include <common_def.h>

#include "internal.h"

struct VgmStream
{
	InternalState* iState;
	WAVE_32BS* iBuf;
	uint32_t iSampCount;
};

static const char* err = NULL;

const char* vgmplayGetError()
{
	return err;
}

VgmStream* vgmplayOpenFile(const char* filepath, uint32_t samplerate, uint32_t buffersize)
{
	// Blah blah obligatory sanity checks.
	if (filepath == NULL || strlen(filepath) < 1)
	{
		err = "File path cannot be null or empty.";
		return NULL;
	}
	if (samplerate == 0)
	{
		err = "Invalid samplerate.";
		return NULL;
	}

	// Create stream object.
	VgmStream* s = (VgmStream*)malloc(sizeof(VgmStream));
	if (!s)
	{
		err = "Failed to allocate stream object.";
		return NULL;
	}

	// Open the file.
	gzFile hFile = gzopen(filepath, "rb");
	if (hFile == NULL)
	{
		err = "Error opening file.";
		free(s);
		return NULL;
	}
	
	// Do the internal stuffs.
	s->iSampCount = buffersize / sizeof(WAVE_32BS);
	s->iBuf = (WAVE_32BS*)malloc(s->iSampCount * sizeof(WAVE_32BS));
	s->iState = InternalOpen(hFile, samplerate, s->iBuf, s->iSampCount * sizeof(WAVE_32BS));
	gzclose(hFile);
	if (!s->iState)
	{
		err = interErr;
		free(s);
		return NULL;
	}

	return s;
}

void vgmplayClose(VgmStream* vgmstream)
{
	if (vgmstream)
	{
		if (vgmstream->iState)
		{
			InternalFree(vgmstream->iState);
		}

		free(vgmstream);
	}
}

uint32_t vgmplayRead32(VgmStream* s, VgmSample32* outBuf, uint32_t outLen)
{
	UINT32 smplCount = outLen / sizeof(VgmSample32);
	if (! smplCount)
		return 0;
	
	if (smplCount > s->iSampCount)
		smplCount = s->iSampCount;

	InternalRead(s->iState, smplCount, 0);
	
	return smplCount * sizeof(VgmSample32);
}

uint32_t vgmplayRead16(VgmStream* s, VgmSample16* outBuf, uint32_t outLen)
{
	UINT32 smplCount;
	INT16* SmplPtr16;
	UINT32 curSmpl;
	WAVE_32BS fnlSmpl;
	
	smplCount = outLen;
	if (! smplCount)
		return 0;
	
	if (smplCount > s->iSampCount)
		smplCount = s->iSampCount;

	InternalRead(s->iState, smplCount, 0);

	SmplPtr16 = (INT16*)outBuf;
	for (curSmpl = 0; curSmpl < smplCount; curSmpl ++, SmplPtr16 += 2)
	{
		fnlSmpl.L = s->iBuf[curSmpl].L >> 8;
		fnlSmpl.R = s->iBuf[curSmpl].R >> 8;
		if (fnlSmpl.L < -0x8000)
			fnlSmpl.L = -0x8000;
		else if (fnlSmpl.L > +0x7FFF)
			fnlSmpl.L = +0x7FFF;
		if (fnlSmpl.R < -0x8000)
			fnlSmpl.R = -0x8000;
		else if (fnlSmpl.R > +0x7FFF)
			fnlSmpl.R = +0x7FFF;
		SmplPtr16[0] = (INT16)fnlSmpl.L;
		SmplPtr16[1] = (INT16)fnlSmpl.R;
	}
	
	return curSmpl * sizeof(VgmSample16);
}
