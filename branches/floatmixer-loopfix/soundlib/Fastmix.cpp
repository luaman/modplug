/*
 * Fastmix.cpp
 * -----------
 * Purpose: Mixer core for rendering samples, mixing plugins, etc...
 * Notes  : If this is Fastmix.cpp, where is Slowmix.cpp? :)
 *          This code is ugly like hell and you are probably not going to understand it
 *          unless you have worked with OpenMPT for a long time - at least I didn't. :)
 *          I guess that a lot of this could be refactored using inline functions.
 * Authors: Olivier Lapicque
 *          OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"
#include "Sndfile.h"

#include "Resampler.h"
#include "WindowedFIR.h"

#ifdef MPT_INTMIXER
#include "IntMixer.h"
#else
#include "FloatMixer.h"
#endif // MPT_INTMIXER


template<typename T>
static void InterleaveStereo(const T *inputL, const T *inputR, T *output, size_t numSamples)
//------------------------------------------------------------------------------------------
{
	while(numSamples--)
	{
		*(output++) = *(inputL++);
		*(output++) = *(inputR++);
	}
}


template<typename T>
static void DeinterleaveStereo(const T *input, T *outputL, T *outputR, size_t numSamples)
//---------------------------------------------------------------------------------------
{
	while(numSamples--)
	{
		*(outputL++) = *(input++);
		*(outputR++) = *(input++);
	}
}


/////////////////////////////////////////////////////
// Mixing Macros

#ifdef MPT_INTMIXER

#define SNDMIX_BEGINSAMPLELOOP8\
	register ModChannel * const pChn = pChannel;\
	nPos = pChn->nPosLo;\
	const signed char *p = (signed char *)(pChn->pCurrentSample)+pChn->nPos;\
	if (pChn->dwFlags[CHN_STEREO]) p += pChn->nPos;\
	int *pvol = pbuffer;\
	do {

#define SNDMIX_BEGINSAMPLELOOP16\
	register ModChannel * const pChn = pChannel;\
	nPos = pChn->nPosLo;\
	const signed short *p = (signed short *)(pChn->pCurrentSample)+pChn->nPos;\
	if (pChn->dwFlags[CHN_STEREO]) p += pChn->nPos;\
	int *pvol = pbuffer;\
	do {

#define SNDMIX_ENDSAMPLELOOP\
		nPos += pChn->nInc;\
	} while (pvol < pbufmax);\
	pChn->nPos += nPos >> 16;\
	pChn->nPosLo = nPos & 0xFFFF;

#define SNDMIX_ENDSAMPLELOOP8	SNDMIX_ENDSAMPLELOOP
#define SNDMIX_ENDSAMPLELOOP16	SNDMIX_ENDSAMPLELOOP

//////////////////////////////////////////////////////////////////////////////
// Mono

// No interpolation
#define SNDMIX_GETMONOVOL8NOIDO\
	int vol = p[nPos >> 16] << 8;

#define SNDMIX_GETMONOVOL16NOIDO\
	int vol = p[nPos >> 16];

// Linear Interpolation
#define SNDMIX_GETMONOVOL8LINEAR\
	int poshi = nPos >> 16;\
	int poslo = (nPos >> 8) & 0xFF;\
	int srcvol = p[poshi];\
	int destvol = p[poshi+1];\
	int vol = (srcvol<<8) + ((int)(poslo * (destvol - srcvol)));

#define SNDMIX_GETMONOVOL16LINEAR\
	int poshi = nPos >> 16;\
	int poslo = (nPos >> 8) & 0xFF;\
	int srcvol = p[poshi];\
	int destvol = p[poshi+1];\
	int vol = srcvol + ((int)(poslo * (destvol - srcvol)) >> 8);

// Cubic Spline
#define SNDMIX_GETMONOVOL8HQSRC\
	int poshi = nPos >> 16;\
	int poslo = (nPos >> 6) & 0x3FC;\
	int vol = (CResampler::FastSincTable[poslo]*p[poshi-1] + CResampler::FastSincTable[poslo+1]*p[poshi]\
		 + CResampler::FastSincTable[poslo+2]*p[poshi+1] + CResampler::FastSincTable[poslo+3]*p[poshi+2]) >> 6;\

#define SNDMIX_GETMONOVOL16HQSRC\
	int poshi = nPos >> 16;\
	int poslo = (nPos >> 6) & 0x3FC;\
	int vol = (CResampler::FastSincTable[poslo]*p[poshi-1] + CResampler::FastSincTable[poslo+1]*p[poshi]\
		 + CResampler::FastSincTable[poslo+2]*p[poshi+1] + CResampler::FastSincTable[poslo+3]*p[poshi+2]) >> 14;\

// 8-taps polyphase
#define SNDMIX_GETMONOVOL8KAISER\
	int poshi = nPos >> 16;\
	const short int *poslo = (const short int *)(sinc+(nPos&0xfff0));\
	int vol = (poslo[0]*p[poshi-3] + poslo[1]*p[poshi-2]\
		 + poslo[2]*p[poshi-1] + poslo[3]*p[poshi]\
		 + poslo[4]*p[poshi+1] + poslo[5]*p[poshi+2]\
		 + poslo[6]*p[poshi+3] + poslo[7]*p[poshi+4]) >> 6;\

#define SNDMIX_GETMONOVOL16KAISER\
	int poshi = nPos >> 16;\
	const short int *poslo = (const short int *)(sinc+(nPos&0xfff0));\
	int vol = (poslo[0]*p[poshi-3] + poslo[1]*p[poshi-2]\
		 + poslo[2]*p[poshi-1] + poslo[3]*p[poshi]\
		 + poslo[4]*p[poshi+1] + poslo[5]*p[poshi+2]\
		 + poslo[6]*p[poshi+3] + poslo[7]*p[poshi+4]) >> 14;\
// rewbs.resamplerConf
#define SNDMIX_GETMONOVOL8FIRFILTER \
	int poshi  = nPos >> 16;\
	int poslo  = (nPos & 0xFFFF);\
	int firidx = ((poslo+WFIR_FRACHALVE)>>WFIR_FRACSHIFT) & WFIR_FRACMASK; \
	int vol    = (WFIR_lut[firidx+0]*(int)p[poshi+1-4]);	\
        vol   += (WFIR_lut[firidx+1]*(int)p[poshi+2-4]);	\
        vol   += (WFIR_lut[firidx+2]*(int)p[poshi+3-4]);	\
        vol   += (WFIR_lut[firidx+3]*(int)p[poshi+4-4]);	\
        vol   += (WFIR_lut[firidx+4]*(int)p[poshi+5-4]);	\
        vol   += (WFIR_lut[firidx+5]*(int)p[poshi+6-4]);	\
        vol   += (WFIR_lut[firidx+6]*(int)p[poshi+7-4]);	\
        vol   += (WFIR_lut[firidx+7]*(int)p[poshi+8-4]);	\
        vol  >>= WFIR_8SHIFT;

#define SNDMIX_GETMONOVOL16FIRFILTER \
    int poshi  = nPos >> 16;\
    int poslo  = (nPos & 0xFFFF);\
    int firidx = ((poslo+WFIR_FRACHALVE)>>WFIR_FRACSHIFT) & WFIR_FRACMASK; \
    int vol1   = (WFIR_lut[firidx+0]*(int)p[poshi+1-4]);	\
        vol1  += (WFIR_lut[firidx+1]*(int)p[poshi+2-4]);	\
        vol1  += (WFIR_lut[firidx+2]*(int)p[poshi+3-4]);	\
        vol1  += (WFIR_lut[firidx+3]*(int)p[poshi+4-4]);	\
    int vol2   = (WFIR_lut[firidx+4]*(int)p[poshi+5-4]);	\
		vol2  += (WFIR_lut[firidx+5]*(int)p[poshi+6-4]);	\
		vol2  += (WFIR_lut[firidx+6]*(int)p[poshi+7-4]);	\
		vol2  += (WFIR_lut[firidx+7]*(int)p[poshi+8-4]);	\
    int vol    = ((vol1>>1)+(vol2>>1)) >> (WFIR_16BITSHIFT-1);


// end rewbs.resamplerConf
#define SNDMIX_INITSINCTABLE\
	const char * const sinc = (const char *)(((pChannel->nInc > 0x13000) || (pChannel->nInc < -0x13000)) ?\
		(((pChannel->nInc > 0x18000) || (pChannel->nInc < -0x18000)) ? pResampler->gDownsample2x : pResampler->gDownsample13x) : pResampler->gKaiserSinc);

#define SNDMIX_INITFIRTABLE\
	const signed short * const WFIR_lut = pResampler->m_WindowedFIR.lut;


/////////////////////////////////////////////////////////////////////////////
// Stereo

// No interpolation
#define SNDMIX_GETSTEREOVOL8NOIDO\
	int vol_l = p[(nPos>>16)*2] << 8;\
	int vol_r = p[(nPos>>16)*2+1] << 8;

#define SNDMIX_GETSTEREOVOL16NOIDO\
	int vol_l = p[(nPos>>16)*2];\
	int vol_r = p[(nPos>>16)*2+1];

// Linear Interpolation
#define SNDMIX_GETSTEREOVOL8LINEAR\
	int poshi = nPos >> 16;\
	int poslo = (nPos >> 8) & 0xFF;\
	int srcvol_l = p[poshi*2];\
	int vol_l = (srcvol_l<<8) + ((int)(poslo * (p[poshi*2+2] - srcvol_l)));\
	int srcvol_r = p[poshi*2+1];\
	int vol_r = (srcvol_r<<8) + ((int)(poslo * (p[poshi*2+3] - srcvol_r)));

#define SNDMIX_GETSTEREOVOL16LINEAR\
	int poshi = nPos >> 16;\
	int poslo = (nPos >> 8) & 0xFF;\
	int srcvol_l = p[poshi*2];\
	int vol_l = srcvol_l + ((int)(poslo * (p[poshi*2+2] - srcvol_l)) >> 8);\
	int srcvol_r = p[poshi*2+1];\
	int vol_r = srcvol_r + ((int)(poslo * (p[poshi*2+3] - srcvol_r)) >> 8);\

// Cubic Spline
#define SNDMIX_GETSTEREOVOL8HQSRC\
	int poshi = nPos >> 16;\
	int poslo = (nPos >> 6) & 0x3FC;\
	int vol_l = (CResampler::FastSincTable[poslo]*p[poshi*2-2] + CResampler::FastSincTable[poslo+1]*p[poshi*2]\
		 + CResampler::FastSincTable[poslo+2]*p[poshi*2+2] + CResampler::FastSincTable[poslo+3]*p[poshi*2+4]) >> 6;\
	int vol_r = (CResampler::FastSincTable[poslo]*p[poshi*2-1] + CResampler::FastSincTable[poslo+1]*p[poshi*2+1]\
		 + CResampler::FastSincTable[poslo+2]*p[poshi*2+3] + CResampler::FastSincTable[poslo+3]*p[poshi*2+5]) >> 6;\

#define SNDMIX_GETSTEREOVOL16HQSRC\
	int poshi = nPos >> 16;\
	int poslo = (nPos >> 6) & 0x3FC;\
	int vol_l = (CResampler::FastSincTable[poslo]*p[poshi*2-2] + CResampler::FastSincTable[poslo+1]*p[poshi*2]\
		 + CResampler::FastSincTable[poslo+2]*p[poshi*2+2] + CResampler::FastSincTable[poslo+3]*p[poshi*2+4]) >> 14;\
	int vol_r = (CResampler::FastSincTable[poslo]*p[poshi*2-1] + CResampler::FastSincTable[poslo+1]*p[poshi*2+1]\
		 + CResampler::FastSincTable[poslo+2]*p[poshi*2+3] + CResampler::FastSincTable[poslo+3]*p[poshi*2+5]) >> 14;\

// -> CODE#0025
// -> DESC="enable polyphase resampling on stereo samples"
// 8-taps polyphase
#define SNDMIX_GETSTEREOVOL8KAISER\
	int poshi = nPos >> 16;\
	const short int *poslo = (const short int *)(sinc+(nPos&0xfff0));\
	int vol_l = (poslo[0]*p[poshi*2-6] + poslo[1]*p[poshi*2-4]\
		 + poslo[2]*p[poshi*2-2] + poslo[3]*p[poshi*2]\
		 + poslo[4]*p[poshi*2+2] + poslo[5]*p[poshi*2+4]\
		 + poslo[6]*p[poshi*2+6] + poslo[7]*p[poshi*2+8]) >> 6;\
	int vol_r = (poslo[0]*p[poshi*2-5] + poslo[1]*p[poshi*2-3]\
		 + poslo[2]*p[poshi*2-1] + poslo[3]*p[poshi*2+1]\
		 + poslo[4]*p[poshi*2+3] + poslo[5]*p[poshi*2+5]\
		 + poslo[6]*p[poshi*2+7] + poslo[7]*p[poshi*2+9]) >> 6;\

#define SNDMIX_GETSTEREOVOL16KAISER\
	int poshi = nPos >> 16;\
	const short int *poslo = (const short int *)(sinc+(nPos&0xfff0));\
	int vol_l = (poslo[0]*p[poshi*2-6] + poslo[1]*p[poshi*2-4]\
		 + poslo[2]*p[poshi*2-2] + poslo[3]*p[poshi*2]\
		 + poslo[4]*p[poshi*2+2] + poslo[5]*p[poshi*2+4]\
		 + poslo[6]*p[poshi*2+6] + poslo[7]*p[poshi*2+8]) >> 14;\
	int vol_r = (poslo[0]*p[poshi*2-5] + poslo[1]*p[poshi*2-3]\
		 + poslo[2]*p[poshi*2-1] + poslo[3]*p[poshi*2+1]\
		 + poslo[4]*p[poshi*2+3] + poslo[5]*p[poshi*2+5]\
		 + poslo[6]*p[poshi*2+7] + poslo[7]*p[poshi*2+9]) >> 14;\
// rewbs.resamplerConf
// fir interpolation
#define SNDMIX_GETSTEREOVOL8FIRFILTER \
    int poshi   = nPos >> 16;\
    int poslo   = (nPos & 0xFFFF);\
    int firidx  = ((poslo+WFIR_FRACHALVE)>>WFIR_FRACSHIFT) & WFIR_FRACMASK; \
    int vol_l   = (WFIR_lut[firidx+0]*(int)p[(poshi+1-4)*2  ]);   \
		vol_l  += (WFIR_lut[firidx+1]*(int)p[(poshi+2-4)*2  ]);   \
		vol_l  += (WFIR_lut[firidx+2]*(int)p[(poshi+3-4)*2  ]);   \
        vol_l  += (WFIR_lut[firidx+3]*(int)p[(poshi+4-4)*2  ]);   \
        vol_l  += (WFIR_lut[firidx+4]*(int)p[(poshi+5-4)*2  ]);   \
		vol_l  += (WFIR_lut[firidx+5]*(int)p[(poshi+6-4)*2  ]);   \
		vol_l  += (WFIR_lut[firidx+6]*(int)p[(poshi+7-4)*2  ]);   \
        vol_l  += (WFIR_lut[firidx+7]*(int)p[(poshi+8-4)*2  ]);   \
		vol_l >>= WFIR_8SHIFT; \
    int vol_r   = (WFIR_lut[firidx+0]*(int)p[(poshi+1-4)*2+1]);   \
		vol_r  += (WFIR_lut[firidx+1]*(int)p[(poshi+2-4)*2+1]);   \
		vol_r  += (WFIR_lut[firidx+2]*(int)p[(poshi+3-4)*2+1]);   \
		vol_r  += (WFIR_lut[firidx+3]*(int)p[(poshi+4-4)*2+1]);   \
		vol_r  += (WFIR_lut[firidx+4]*(int)p[(poshi+5-4)*2+1]);   \
        vol_r  += (WFIR_lut[firidx+5]*(int)p[(poshi+6-4)*2+1]);   \
        vol_r  += (WFIR_lut[firidx+6]*(int)p[(poshi+7-4)*2+1]);   \
        vol_r  += (WFIR_lut[firidx+7]*(int)p[(poshi+8-4)*2+1]);   \
        vol_r >>= WFIR_8SHIFT;

#define SNDMIX_GETSTEREOVOL16FIRFILTER \
    int poshi   = nPos >> 16;\
    int poslo   = (nPos & 0xFFFF);\
    int firidx  = ((poslo+WFIR_FRACHALVE)>>WFIR_FRACSHIFT) & WFIR_FRACMASK; \
    int vol1_l  = (WFIR_lut[firidx+0]*(int)p[(poshi+1-4)*2  ]);   \
		vol1_l += (WFIR_lut[firidx+1]*(int)p[(poshi+2-4)*2  ]);   \
        vol1_l += (WFIR_lut[firidx+2]*(int)p[(poshi+3-4)*2  ]);   \
		vol1_l += (WFIR_lut[firidx+3]*(int)p[(poshi+4-4)*2  ]);   \
	int vol2_l  = (WFIR_lut[firidx+4]*(int)p[(poshi+5-4)*2  ]);   \
		vol2_l += (WFIR_lut[firidx+5]*(int)p[(poshi+6-4)*2  ]);   \
		vol2_l += (WFIR_lut[firidx+6]*(int)p[(poshi+7-4)*2  ]);   \
		vol2_l += (WFIR_lut[firidx+7]*(int)p[(poshi+8-4)*2  ]);   \
	int vol_l   = ((vol1_l>>1)+(vol2_l>>1)) >> (WFIR_16BITSHIFT-1); \
	int vol1_r  = (WFIR_lut[firidx+0]*(int)p[(poshi+1-4)*2+1]);   \
		vol1_r += (WFIR_lut[firidx+1]*(int)p[(poshi+2-4)*2+1]);   \
		vol1_r += (WFIR_lut[firidx+2]*(int)p[(poshi+3-4)*2+1]);   \
		vol1_r += (WFIR_lut[firidx+3]*(int)p[(poshi+4-4)*2+1]);   \
	int vol2_r  = (WFIR_lut[firidx+4]*(int)p[(poshi+5-4)*2+1]);   \
		vol2_r += (WFIR_lut[firidx+5]*(int)p[(poshi+6-4)*2+1]);   \
		vol2_r += (WFIR_lut[firidx+6]*(int)p[(poshi+7-4)*2+1]);   \
		vol2_r += (WFIR_lut[firidx+7]*(int)p[(poshi+8-4)*2+1]);   \
	int vol_r   = ((vol1_r>>1)+(vol2_r>>1)) >> (WFIR_16BITSHIFT-1);
//end rewbs.resamplerConf
// -! BEHAVIOUR_CHANGE#0025


/////////////////////////////////////////////////////////////////////////////

#define SNDMIX_STOREMONOVOL\
	pvol[0] += vol * pChn->leftVol;\
	pvol[1] += vol * pChn->rightVol;\
	pvol += 2;

#define SNDMIX_STORESTEREOVOL\
	pvol[0] += vol_l * pChn->leftVol;\
	pvol[1] += vol_r * pChn->rightVol;\
	pvol += 2;

#define SNDMIX_STOREFASTMONOVOL\
	int v = vol * pChn->leftVol;\
	pvol[0] += v;\
	pvol[1] += v;\
	pvol += 2;

#define SNDMIX_RAMPMONOVOL\
	rampLeftVol += pChn->leftRamp;\
	rampRightVol += pChn->rightRamp;\
	pvol[0] += vol * (rampLeftVol >> VOLUMERAMPPRECISION);\
	pvol[1] += vol * (rampRightVol >> VOLUMERAMPPRECISION);\
	pvol += 2;

#define SNDMIX_RAMPFASTMONOVOL\
	rampLeftVol += pChn->leftRamp;\
	int fastvol = vol * (rampLeftVol >> VOLUMERAMPPRECISION);\
	pvol[0] += fastvol;\
	pvol[1] += fastvol;\
	pvol += 2;

#define SNDMIX_RAMPSTEREOVOL\
	rampLeftVol += pChn->leftRamp;\
	rampRightVol += pChn->rightRamp;\
	pvol[0] += vol_l * (rampLeftVol >> VOLUMERAMPPRECISION);\
	pvol[1] += vol_r * (rampRightVol >> VOLUMERAMPPRECISION);\
	pvol += 2;


///////////////////////////////////////////////////
// Resonant Filters

// Filter values are clipped to double the input range (assuming input is 16-Bit, which it currently is)
#define ClipFilter(x) Clamp(x, 2.0f * (float)int16_min, 2.0f * (float)int16_max)

// Resonant filter for Mono samples
static forceinline void ProcessMonoFilter(int &vol, ModChannel *pChn)
//-------------------------------------------------------------------
{
	float fy1 = pChn->nFilter_Y[0][0];
	float fy2 = pChn->nFilter_Y[0][1];

	float fy = ((float)vol * pChn->nFilter_A0 + ClipFilter(fy1) * pChn->nFilter_B0 + ClipFilter(fy2) * pChn->nFilter_B1);
	fy2 = fy1;
	fy1 = fy - (float)(vol & pChn->nFilter_HP);
	vol = (int)fy;

	pChn->nFilter_Y[0][0] = fy1;
	pChn->nFilter_Y[0][1] = fy2;
}


// Resonant filter for Stereo samples
static forceinline void ProcessStereoFilter(int &vol_l, int &vol_r, ModChannel *pChn)
//-----------------------------------------------------------------------------------
{
	// Left channel
	float fy1 = pChn->nFilter_Y[0][0];
	float fy2 = pChn->nFilter_Y[0][1];

	float fy = ((float)vol_l * pChn->nFilter_A0 + ClipFilter(fy1) * pChn->nFilter_B0 + ClipFilter(fy2) * pChn->nFilter_B1);
	fy2 = fy1;
	fy1 = fy - (float)(vol_l & pChn->nFilter_HP);
	vol_l = (int)fy;

	pChn->nFilter_Y[0][0] = fy1;
	pChn->nFilter_Y[0][1] = fy2;

	// Right channel
	fy1 = pChn->nFilter_Y[1][0];
	fy2 = pChn->nFilter_Y[1][1];

	fy = ((float)vol_r * pChn->nFilter_A0 + ClipFilter(fy1) * pChn->nFilter_B0 + ClipFilter(fy2) * pChn->nFilter_B1);
	fy2 = fy1;
	fy1 = fy - (float)(vol_r & pChn->nFilter_HP);
	vol_r = (int)fy;

	pChn->nFilter_Y[1][0] = fy1;
	pChn->nFilter_Y[1][1] = fy2;
}


// Mono
#define SNDMIX_PROCESSFILTER \
	ProcessMonoFilter(vol, pChn);


// Stereo
#define SNDMIX_PROCESSSTEREOFILTER \
	ProcessStereoFilter(vol_l, vol_r, pChn);



//////////////////////////////////////////////////////////
// Interfaces

typedef VOID (* LPMIXINTERFACE)(ModChannel *, const CResampler *, int *, int *);

#define BEGIN_MIX_INTERFACE(func)\
	VOID func(ModChannel *pChannel, const CResampler *pResampler, int *pbuffer, int *pbufmax)\
	{\
		UNREFERENCED_PARAMETER(pResampler);\
		LONG nPos;

#define END_MIX_INTERFACE()\
		SNDMIX_ENDSAMPLELOOP\
	}

// Volume Ramps
#define BEGIN_RAMPMIX_INTERFACE(func)\
	BEGIN_MIX_INTERFACE(func)\
		LONG rampLeftVol = pChannel->rampLeftVol;\
		LONG rampRightVol = pChannel->rampRightVol;

#define END_RAMPMIX_INTERFACE()\
		SNDMIX_ENDSAMPLELOOP\
		pChannel->rampLeftVol = rampLeftVol;\
		pChannel->leftVol = rampLeftVol >> VOLUMERAMPPRECISION;\
		pChannel->rampRightVol = rampRightVol;\
		pChannel->rightVol = rampRightVol >> VOLUMERAMPPRECISION;\
	}

#define BEGIN_FASTRAMPMIX_INTERFACE(func)\
	BEGIN_MIX_INTERFACE(func)\
		LONG rampLeftVol = pChannel->rampLeftVol;

#define END_FASTRAMPMIX_INTERFACE()\
		SNDMIX_ENDSAMPLELOOP\
		pChannel->rampLeftVol = rampLeftVol;\
		pChannel->rampRightVol = rampLeftVol;\
		pChannel->leftVol = rampLeftVol >> VOLUMERAMPPRECISION;\
		pChannel->rightVol = pChannel->leftVol;\
	}


// Mono Resonant Filters
#define BEGIN_MIX_FLT_INTERFACE(func)\
	BEGIN_MIX_INTERFACE(func)


#define END_MIX_FLT_INTERFACE()\
	SNDMIX_ENDSAMPLELOOP\
	}

#define BEGIN_RAMPMIX_FLT_INTERFACE(func)\
	BEGIN_MIX_INTERFACE(func)\
		LONG rampLeftVol = pChannel->rampLeftVol;\
		LONG rampRightVol = pChannel->rampRightVol;

#define END_RAMPMIX_FLT_INTERFACE()\
		SNDMIX_ENDSAMPLELOOP\
		pChannel->rampLeftVol = rampLeftVol;\
		pChannel->leftVol = rampLeftVol >> VOLUMERAMPPRECISION;\
		pChannel->rampRightVol = rampRightVol;\
		pChannel->rightVol = rampRightVol >> VOLUMERAMPPRECISION;\
	}

// Stereo Resonant Filters
#define BEGIN_MIX_STFLT_INTERFACE(func)\
	BEGIN_MIX_INTERFACE(func)


#define END_MIX_STFLT_INTERFACE()\
	SNDMIX_ENDSAMPLELOOP\
	}

#define BEGIN_RAMPMIX_STFLT_INTERFACE(func)\
	BEGIN_MIX_INTERFACE(func)\
		LONG rampLeftVol = pChannel->rampLeftVol;\
		LONG rampRightVol = pChannel->rampRightVol;

#define END_RAMPMIX_STFLT_INTERFACE()\
		SNDMIX_ENDSAMPLELOOP\
		pChannel->rampLeftVol = rampLeftVol;\
		pChannel->leftVol = rampLeftVol >> VOLUMERAMPPRECISION;\
		pChannel->rampRightVol = rampRightVol;\
		pChannel->rightVol = rampRightVol >> VOLUMERAMPPRECISION;\
	}



/////////////////////////////////////////////////////
// Mono samples functions

BEGIN_MIX_INTERFACE(Mono8BitMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8NOIDO
	SNDMIX_STOREMONOVOL
END_MIX_INTERFACE()

BEGIN_MIX_INTERFACE(Mono16BitMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16NOIDO
	SNDMIX_STOREMONOVOL
END_MIX_INTERFACE()

BEGIN_MIX_INTERFACE(Mono8BitLinearMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8LINEAR
	SNDMIX_STOREMONOVOL
END_MIX_INTERFACE()

BEGIN_MIX_INTERFACE(Mono16BitLinearMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16LINEAR
	SNDMIX_STOREMONOVOL
END_MIX_INTERFACE()

BEGIN_MIX_INTERFACE(Mono8BitHQMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8HQSRC
	SNDMIX_STOREMONOVOL
END_MIX_INTERFACE()

BEGIN_MIX_INTERFACE(Mono16BitHQMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16HQSRC
	SNDMIX_STOREMONOVOL
END_MIX_INTERFACE()

// Volume Ramps
BEGIN_RAMPMIX_INTERFACE(Mono8BitRampMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8NOIDO
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_INTERFACE()

BEGIN_RAMPMIX_INTERFACE(Mono16BitRampMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16NOIDO
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_INTERFACE()

BEGIN_RAMPMIX_INTERFACE(Mono8BitLinearRampMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8LINEAR
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_INTERFACE()

BEGIN_RAMPMIX_INTERFACE(Mono16BitLinearRampMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16LINEAR
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_INTERFACE()

BEGIN_RAMPMIX_INTERFACE(Mono8BitHQRampMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8HQSRC
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_INTERFACE()

BEGIN_RAMPMIX_INTERFACE(Mono16BitHQRampMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16HQSRC
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_INTERFACE()

//////////////////////////////////////////////////////
// 8-taps polyphase resampling filter

// Normal
BEGIN_MIX_INTERFACE(Mono8BitKaiserMix)
	SNDMIX_INITSINCTABLE
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8KAISER
	SNDMIX_STOREMONOVOL
END_MIX_INTERFACE()

BEGIN_MIX_INTERFACE(Mono16BitKaiserMix)
	SNDMIX_INITSINCTABLE
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16KAISER
	SNDMIX_STOREMONOVOL
END_MIX_INTERFACE()

// Ramp
BEGIN_RAMPMIX_INTERFACE(Mono8BitKaiserRampMix)
	SNDMIX_INITSINCTABLE
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8KAISER
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_INTERFACE()

BEGIN_RAMPMIX_INTERFACE(Mono16BitKaiserRampMix)
	SNDMIX_INITSINCTABLE
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16KAISER
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_INTERFACE()

// -> BEHAVIOUR_CHANGE#0025
// -> DESC="enable polyphase resampling on stereo samples"
// rewbs.resamplerConf
//////////////////////////////////////////////////////
// FIR filter

// Normal
BEGIN_MIX_INTERFACE(Mono8BitFIRFilterMix)
	SNDMIX_INITFIRTABLE
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8FIRFILTER
	SNDMIX_STOREMONOVOL
END_MIX_INTERFACE()

BEGIN_MIX_INTERFACE(Mono16BitFIRFilterMix)
	SNDMIX_INITFIRTABLE
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16FIRFILTER
	SNDMIX_STOREMONOVOL
END_MIX_INTERFACE()

// Ramp
BEGIN_RAMPMIX_INTERFACE(Mono8BitFIRFilterRampMix)
	SNDMIX_INITFIRTABLE
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8FIRFILTER
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_INTERFACE()

BEGIN_RAMPMIX_INTERFACE(Mono16BitFIRFilterRampMix)
	SNDMIX_INITFIRTABLE
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16FIRFILTER
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_INTERFACE()

//end rewbs.resamplerConf
// -! BEHAVIOUR_CHANGE#0025

//////////////////////////////////////////////////////
// Fast mono mix for leftvol=rightvol (1 less imul)

BEGIN_MIX_INTERFACE(FastMono8BitMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8NOIDO
	SNDMIX_STOREFASTMONOVOL
END_MIX_INTERFACE()

BEGIN_MIX_INTERFACE(FastMono16BitMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16NOIDO
	SNDMIX_STOREFASTMONOVOL
END_MIX_INTERFACE()

BEGIN_MIX_INTERFACE(FastMono8BitLinearMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8LINEAR
	SNDMIX_STOREFASTMONOVOL
END_MIX_INTERFACE()

BEGIN_MIX_INTERFACE(FastMono16BitLinearMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16LINEAR
	SNDMIX_STOREFASTMONOVOL
END_MIX_INTERFACE()

// Fast Ramps
BEGIN_FASTRAMPMIX_INTERFACE(FastMono8BitRampMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8NOIDO
	SNDMIX_RAMPFASTMONOVOL
END_FASTRAMPMIX_INTERFACE()

BEGIN_FASTRAMPMIX_INTERFACE(FastMono16BitRampMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16NOIDO
	SNDMIX_RAMPFASTMONOVOL
END_FASTRAMPMIX_INTERFACE()

BEGIN_FASTRAMPMIX_INTERFACE(FastMono8BitLinearRampMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8LINEAR
	SNDMIX_RAMPFASTMONOVOL
END_FASTRAMPMIX_INTERFACE()

BEGIN_FASTRAMPMIX_INTERFACE(FastMono16BitLinearRampMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16LINEAR
	SNDMIX_RAMPFASTMONOVOL
END_FASTRAMPMIX_INTERFACE()


//////////////////////////////////////////////////////
// Stereo samples

BEGIN_MIX_INTERFACE(Stereo8BitMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8NOIDO
	SNDMIX_STORESTEREOVOL
END_MIX_INTERFACE()

BEGIN_MIX_INTERFACE(Stereo16BitMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16NOIDO
	SNDMIX_STORESTEREOVOL
END_MIX_INTERFACE()

BEGIN_MIX_INTERFACE(Stereo8BitLinearMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8LINEAR
	SNDMIX_STORESTEREOVOL
END_MIX_INTERFACE()

BEGIN_MIX_INTERFACE(Stereo16BitLinearMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16LINEAR
	SNDMIX_STORESTEREOVOL
END_MIX_INTERFACE()

BEGIN_MIX_INTERFACE(Stereo8BitHQMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8HQSRC
	SNDMIX_STORESTEREOVOL
END_MIX_INTERFACE()

BEGIN_MIX_INTERFACE(Stereo16BitHQMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16HQSRC
	SNDMIX_STORESTEREOVOL
END_MIX_INTERFACE()

// Volume Ramps
BEGIN_RAMPMIX_INTERFACE(Stereo8BitRampMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8NOIDO
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_INTERFACE()

BEGIN_RAMPMIX_INTERFACE(Stereo16BitRampMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16NOIDO
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_INTERFACE()

BEGIN_RAMPMIX_INTERFACE(Stereo8BitLinearRampMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8LINEAR
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_INTERFACE()

BEGIN_RAMPMIX_INTERFACE(Stereo16BitLinearRampMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16LINEAR
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_INTERFACE()

BEGIN_RAMPMIX_INTERFACE(Stereo8BitHQRampMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8HQSRC
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_INTERFACE()

BEGIN_RAMPMIX_INTERFACE(Stereo16BitHQRampMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16HQSRC
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_INTERFACE()

// -> CODE#0025
// -> DESC="enable polyphase resampling on stereo samples"

//////////////////////////////////////////////////////
// Stereo 8-taps polyphase resampling filter

BEGIN_MIX_INTERFACE(Stereo8BitKaiserMix)
	SNDMIX_INITSINCTABLE
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8KAISER
	SNDMIX_STORESTEREOVOL
END_MIX_INTERFACE()

BEGIN_MIX_INTERFACE(Stereo16BitKaiserMix)
	SNDMIX_INITSINCTABLE
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16KAISER
	SNDMIX_STORESTEREOVOL
END_MIX_INTERFACE()

// Ramp
BEGIN_RAMPMIX_INTERFACE(Stereo8BitKaiserRampMix)
	SNDMIX_INITSINCTABLE
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8KAISER
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_INTERFACE()

BEGIN_RAMPMIX_INTERFACE(Stereo16BitKaiserRampMix)
	SNDMIX_INITSINCTABLE
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16KAISER
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_INTERFACE()

// rewbs.resamplerConf
//////////////////////////////////////////////////////
// Stereo FIR Filter

BEGIN_MIX_INTERFACE(Stereo8BitFIRFilterMix)
	SNDMIX_INITFIRTABLE
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8FIRFILTER
	SNDMIX_STORESTEREOVOL
END_MIX_INTERFACE()

BEGIN_MIX_INTERFACE(Stereo16BitFIRFilterMix)
	SNDMIX_INITFIRTABLE
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16FIRFILTER
	SNDMIX_STORESTEREOVOL
END_MIX_INTERFACE()

// Ramp
BEGIN_RAMPMIX_INTERFACE(Stereo8BitFIRFilterRampMix)
	SNDMIX_INITFIRTABLE
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8FIRFILTER
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_INTERFACE()

BEGIN_RAMPMIX_INTERFACE(Stereo16BitFIRFilterRampMix)
	SNDMIX_INITFIRTABLE
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16FIRFILTER
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_INTERFACE()

// end rewbs.resamplerConf
// -! BEHAVIOUR_CHANGE#0025



//////////////////////////////////////////////////////
// Resonant Filter Mix

#ifndef NO_FILTER

// Mono Filter Mix
BEGIN_MIX_FLT_INTERFACE(FilterMono8BitMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8NOIDO
	SNDMIX_PROCESSFILTER
	SNDMIX_STOREMONOVOL
END_MIX_FLT_INTERFACE()

BEGIN_MIX_FLT_INTERFACE(FilterMono16BitMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16NOIDO
	SNDMIX_PROCESSFILTER
	SNDMIX_STOREMONOVOL
END_MIX_FLT_INTERFACE()

BEGIN_MIX_FLT_INTERFACE(FilterMono8BitLinearMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8LINEAR
	SNDMIX_PROCESSFILTER
	SNDMIX_STOREMONOVOL
END_MIX_FLT_INTERFACE()

BEGIN_MIX_FLT_INTERFACE(FilterMono16BitLinearMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16LINEAR
	SNDMIX_PROCESSFILTER
	SNDMIX_STOREMONOVOL
END_MIX_FLT_INTERFACE()

// -> CODE#0025
// -> DESC="enable polyphase resampling on stereo samples"
// rewbs.resamplerConf
//Cubic + reso filter:
BEGIN_MIX_FLT_INTERFACE(FilterMono8BitHQMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8HQSRC
	SNDMIX_PROCESSFILTER
	SNDMIX_STOREMONOVOL
END_MIX_FLT_INTERFACE()

BEGIN_MIX_FLT_INTERFACE(FilterMono16BitHQMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16HQSRC
	SNDMIX_PROCESSFILTER
	SNDMIX_STOREMONOVOL
END_MIX_FLT_INTERFACE()

//Polyphase + reso filter:
BEGIN_MIX_FLT_INTERFACE(FilterMono8BitKaiserMix)
	SNDMIX_INITSINCTABLE
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8KAISER
	SNDMIX_PROCESSFILTER
	SNDMIX_STOREMONOVOL
END_MIX_FLT_INTERFACE()

BEGIN_MIX_FLT_INTERFACE(FilterMono16BitKaiserMix)
	SNDMIX_INITSINCTABLE
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16KAISER
	SNDMIX_PROCESSFILTER
	SNDMIX_STOREMONOVOL
END_MIX_FLT_INTERFACE()

// Enable FIR Filter with resonant filters

BEGIN_MIX_FLT_INTERFACE(FilterMono8BitFIRFilterMix)
	SNDMIX_INITFIRTABLE
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8FIRFILTER
	SNDMIX_PROCESSFILTER
	SNDMIX_STOREMONOVOL
END_MIX_FLT_INTERFACE()

BEGIN_MIX_FLT_INTERFACE(FilterMono16BitFIRFilterMix)
	SNDMIX_INITFIRTABLE
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16FIRFILTER
	SNDMIX_PROCESSFILTER
	SNDMIX_STOREMONOVOL
END_MIX_FLT_INTERFACE()
// end rewbs.resamplerConf
// -! BEHAVIOUR_CHANGE#0025

// Filter + Ramp
BEGIN_RAMPMIX_FLT_INTERFACE(FilterMono8BitRampMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8NOIDO
	SNDMIX_PROCESSFILTER
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_FLT_INTERFACE()

BEGIN_RAMPMIX_FLT_INTERFACE(FilterMono16BitRampMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16NOIDO
	SNDMIX_PROCESSFILTER
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_FLT_INTERFACE()

BEGIN_RAMPMIX_FLT_INTERFACE(FilterMono8BitLinearRampMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8LINEAR
	SNDMIX_PROCESSFILTER
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_FLT_INTERFACE()

BEGIN_RAMPMIX_FLT_INTERFACE(FilterMono16BitLinearRampMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16LINEAR
	SNDMIX_PROCESSFILTER
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_FLT_INTERFACE()

// -> CODE#0025
// -> DESC="enable polyphase resampling on stereo samples"
// rewbs.resamplerConf
//Cubic + reso filter + ramp:
BEGIN_RAMPMIX_FLT_INTERFACE(FilterMono8BitHQRampMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8HQSRC
	SNDMIX_PROCESSFILTER
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_FLT_INTERFACE()

BEGIN_RAMPMIX_FLT_INTERFACE(FilterMono16BitHQRampMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16HQSRC
	SNDMIX_PROCESSFILTER
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_FLT_INTERFACE()

//Polyphase + reso filter + ramp:
BEGIN_RAMPMIX_FLT_INTERFACE(FilterMono8BitKaiserRampMix)
	SNDMIX_INITSINCTABLE
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8KAISER
	SNDMIX_PROCESSFILTER
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_FLT_INTERFACE()

BEGIN_RAMPMIX_FLT_INTERFACE(FilterMono16BitKaiserRampMix)
	SNDMIX_INITSINCTABLE
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16KAISER
	SNDMIX_PROCESSFILTER
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_FLT_INTERFACE()

//FIR Filter + reso filter + ramp
BEGIN_RAMPMIX_FLT_INTERFACE(FilterMono8BitFIRFilterRampMix)
	SNDMIX_INITFIRTABLE
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETMONOVOL8FIRFILTER
	SNDMIX_PROCESSFILTER
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_FLT_INTERFACE()

BEGIN_RAMPMIX_FLT_INTERFACE(FilterMono16BitFIRFilterRampMix)
	SNDMIX_INITFIRTABLE
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETMONOVOL16FIRFILTER
	SNDMIX_PROCESSFILTER
	SNDMIX_RAMPMONOVOL
END_RAMPMIX_FLT_INTERFACE()

// end rewbs.resamplerConf
// -! BEHAVIOUR_CHANGE#0025

// Stereo Filter Mix
BEGIN_MIX_STFLT_INTERFACE(FilterStereo8BitMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8NOIDO
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_STORESTEREOVOL
END_MIX_STFLT_INTERFACE()

BEGIN_MIX_STFLT_INTERFACE(FilterStereo16BitMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16NOIDO
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_STORESTEREOVOL
END_MIX_STFLT_INTERFACE()

BEGIN_MIX_STFLT_INTERFACE(FilterStereo8BitLinearMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8LINEAR
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_STORESTEREOVOL
END_MIX_STFLT_INTERFACE()

BEGIN_MIX_STFLT_INTERFACE(FilterStereo16BitLinearMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16LINEAR
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_STORESTEREOVOL
END_MIX_STFLT_INTERFACE()

// -> CODE#0025
// -> DESC="enable polyphase resampling on stereo samples"
// rewbs.resamplerConf
//Cubic stereo + reso filter
BEGIN_MIX_STFLT_INTERFACE(FilterStereo8BitHQMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8HQSRC
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_STORESTEREOVOL
END_MIX_STFLT_INTERFACE()

BEGIN_MIX_STFLT_INTERFACE(FilterStereo16BitHQMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16HQSRC
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_STORESTEREOVOL
END_MIX_STFLT_INTERFACE()

//Polyphase stereo + reso filter
BEGIN_MIX_STFLT_INTERFACE(FilterStereo8BitKaiserMix)
	SNDMIX_INITSINCTABLE
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8KAISER
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_STORESTEREOVOL
END_MIX_STFLT_INTERFACE()

BEGIN_MIX_STFLT_INTERFACE(FilterStereo16BitKaiserMix)
	SNDMIX_INITSINCTABLE
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16KAISER
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_STORESTEREOVOL
END_MIX_STFLT_INTERFACE()

//FIR filter stereo + reso filter
BEGIN_MIX_STFLT_INTERFACE(FilterStereo8BitFIRFilterMix)
	SNDMIX_INITFIRTABLE
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8FIRFILTER
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_STORESTEREOVOL
END_MIX_STFLT_INTERFACE()

BEGIN_MIX_STFLT_INTERFACE(FilterStereo16BitFIRFilterMix)
	SNDMIX_INITFIRTABLE
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16FIRFILTER
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_STORESTEREOVOL
END_MIX_STFLT_INTERFACE()


//end rewbs.resamplerConf
// -! BEHAVIOUR_CHANGE#0025

// Stereo Filter + Ramp
BEGIN_RAMPMIX_STFLT_INTERFACE(FilterStereo8BitRampMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8NOIDO
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_STFLT_INTERFACE()

BEGIN_RAMPMIX_STFLT_INTERFACE(FilterStereo16BitRampMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16NOIDO
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_STFLT_INTERFACE()

BEGIN_RAMPMIX_STFLT_INTERFACE(FilterStereo8BitLinearRampMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8LINEAR
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_STFLT_INTERFACE()

BEGIN_RAMPMIX_STFLT_INTERFACE(FilterStereo16BitLinearRampMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16LINEAR
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_STFLT_INTERFACE()

// -> CODE#0025
// -> DESC="enable polyphase resampling on stereo samples"
// rewbs.resamplerConf
//Cubic stereo + ramp + reso filter
BEGIN_RAMPMIX_STFLT_INTERFACE(FilterStereo8BitHQRampMix)
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8HQSRC
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_STFLT_INTERFACE()

BEGIN_RAMPMIX_STFLT_INTERFACE(FilterStereo16BitHQRampMix)
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16HQSRC
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_STFLT_INTERFACE()

//Polyphase stereo + ramp + reso filter
BEGIN_RAMPMIX_STFLT_INTERFACE(FilterStereo8BitKaiserRampMix)
	SNDMIX_INITSINCTABLE
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8KAISER
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_STFLT_INTERFACE()

BEGIN_RAMPMIX_STFLT_INTERFACE(FilterStereo16BitKaiserRampMix)
	SNDMIX_INITSINCTABLE
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16KAISER
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_STFLT_INTERFACE()

//FIR filter stereo + ramp + reso filter
BEGIN_RAMPMIX_STFLT_INTERFACE(FilterStereo8BitFIRFilterRampMix)
	SNDMIX_INITFIRTABLE
	SNDMIX_BEGINSAMPLELOOP8
	SNDMIX_GETSTEREOVOL8FIRFILTER
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_STFLT_INTERFACE()

BEGIN_RAMPMIX_STFLT_INTERFACE(FilterStereo16BitFIRFilterRampMix)
	SNDMIX_INITFIRTABLE
	SNDMIX_BEGINSAMPLELOOP16
	SNDMIX_GETSTEREOVOL16FIRFILTER
	SNDMIX_PROCESSSTEREOFILTER
	SNDMIX_RAMPSTEREOVOL
END_RAMPMIX_STFLT_INTERFACE()

// end rewbs.resamplerConf
// -! BEHAVIOUR_CHANGE#0025

#else
// Mono
#define FilterMono8BitMix				Mono8BitMix
#define FilterMono16BitMix				Mono16BitMix
#define FilterMono8BitLinearMix			Mono8BitLinearMix
#define FilterMono16BitLinearMix		Mono16BitLinearMix
#define FilterMono8BitRampMix			Mono8BitRampMix
#define FilterMono16BitRampMix			Mono16BitRampMix
#define FilterMono8BitLinearRampMix		Mono8BitLinearRampMix
#define FilterMono16BitLinearRampMix	Mono16BitLinearRampMix
// -> CODE#0025
// -> DESC="enable polyphase resampling on stereo samples"
#define FilterMono8BitHQMix				Mono8BitHQMix
#define FilterMono16BitHQMix			Mono16BitHQMix
#define FilterMono8BitKaiserMix			Mono8BitKaiserMix
#define FilterMono16BitKaiserMix		Mono16BitKaiserMix
#define FilterMono8BitHQRampMix			Mono8BitHQRampMix
#define FilterMono16BitHQRampMix		Mono16BitHQRampMix
#define FilterMono8BitKaiserRampMix		Mono8BitKaiserRampMix
#define FilterMono16BitKaiserRampMix	Mono16BitKaiserRampMix
#define FilterMono8BitFIRFilterRampMix	Mono8BitFIRFilterRampMix
#define FilterMono16BitFIRFilterRampMix	Mono16BitFIRFilterRampMix
// -! BEHAVIOUR_CHANGE#0025

// Stereo
#define FilterStereo8BitMix				Stereo8BitMix
#define FilterStereo16BitMix			Stereo16BitMix
#define FilterStereo8BitLinearMix		Stereo8BitLinearMix
#define FilterStereo16BitLinearMix		Stereo16BitLinearMix
#define FilterStereo8BitRampMix			Stereo8BitRampMix
#define FilterStereo16BitRampMix		Stereo16BitRampMix
#define FilterStereo8BitLinearRampMix	Stereo8BitLinearRampMix
#define FilterStereo16BitLinearRampMix	Stereo16BitLinearRampMix
// -> CODE#0025
// -> DESC="enable polyphase resampling on stereo samples"
#define FilterStereo8BitHQMix			Stereo8BitHQMix
#define FilterStereo16BitHQMix			Stereo16BitHQMix
#define FilterStereo8BitKaiserMix		Stereo8BitKaiserMix
#define FilterStereo16BitKaiserMix		Stereo16BitKaiserMix
#define FilterStereo8BitHQRampMix		Stereo8BitHQRampMix
#define FilterStereo16BitHQRampMix		Stereo16BitHQRampMix
#define FilterStereo8BitKaiserRampMix	Stereo8BitKaiserRampMix
#define FilterStereo16BitKaiserRampMix	Stereo16BitKaiserRampMix
#define FilterStereo8BitFIRFilterRampMix	Stereo8BitFIRFilterRampMix
#define FilterStereo16BitFIRFilterRampMix	Stereo16BitFIRFilterRampMix
// -! BEHAVIOUR_CHANGE#0025
#endif

#endif // MPT_INTMIXER

#ifdef MPT_INTMIXER
#define MIXNDX_16BIT	0x01
#define MIXNDX_STEREO	0x02
#define MIXNDX_RAMP		0x04
#define MIXNDX_FILTER	0x08

#define MIXNDX_LINEARSRC	0x10
#define MIXNDX_HQSRC		0x20
#define MIXNDX_KAISERSRC	0x30
#define MIXNDX_FIRFILTERSRC	0x40 // rewbs.resamplerConf

static UINT ResamplingModeToMixFlags(uint8 resamplingMode)
//--------------------------------------------------------
{
	switch(resamplingMode)
	{
	case SRCMODE_NEAREST:   return 0;
	case SRCMODE_LINEAR:    return MIXNDX_LINEARSRC;
	case SRCMODE_SPLINE:    return MIXNDX_HQSRC;
	case SRCMODE_POLYPHASE: return MIXNDX_KAISERSRC;
	case SRCMODE_FIRFILTER: return MIXNDX_FIRFILTERSRC;
	}
	return 0;
}


//const LPMIXINTERFACE gpMixFunctionTable[4*16] =
const LPMIXINTERFACE gpMixFunctionTable[5*16] =    //rewbs.resamplerConf: increased to 5 to cope with FIR
{
	// No SRC
	Mono8BitMix,				Mono16BitMix,				Stereo8BitMix,			Stereo16BitMix,
	Mono8BitRampMix,			Mono16BitRampMix,			Stereo8BitRampMix,		Stereo16BitRampMix,
	// No SRC, Filter
	FilterMono8BitMix,			FilterMono16BitMix,			FilterStereo8BitMix,	FilterStereo16BitMix,
	FilterMono8BitRampMix,		FilterMono16BitRampMix,		FilterStereo8BitRampMix,FilterStereo16BitRampMix,
	// Linear SRC
	Mono8BitLinearMix,			Mono16BitLinearMix,			Stereo8BitLinearMix,	Stereo16BitLinearMix,
	Mono8BitLinearRampMix,		Mono16BitLinearRampMix,		Stereo8BitLinearRampMix,Stereo16BitLinearRampMix,
	// Linear SRC, Filter
	FilterMono8BitLinearMix,	FilterMono16BitLinearMix,	FilterStereo8BitLinearMix,	FilterStereo16BitLinearMix,
	FilterMono8BitLinearRampMix,FilterMono16BitLinearRampMix,FilterStereo8BitLinearRampMix,FilterStereo16BitLinearRampMix,
	// HQ SRC
	Mono8BitHQMix,				Mono16BitHQMix,				Stereo8BitHQMix,		Stereo16BitHQMix,
	Mono8BitHQRampMix,			Mono16BitHQRampMix,			Stereo8BitHQRampMix,	Stereo16BitHQRampMix,
	// HQ SRC, Filter

// -> CODE#0025
// -> DESC="enable polyphase resampling on stereo samples"
//	FilterMono8BitLinearMix,	FilterMono16BitLinearMix,	FilterStereo8BitLinearMix,	FilterStereo16BitLinearMix,
//	FilterMono8BitLinearRampMix,FilterMono16BitLinearRampMix,FilterStereo8BitLinearRampMix,FilterStereo16BitLinearRampMix,
	FilterMono8BitHQMix,		FilterMono16BitHQMix,		FilterStereo8BitHQMix,		FilterStereo16BitHQMix,
	FilterMono8BitHQRampMix,	FilterMono16BitHQRampMix,	FilterStereo8BitHQRampMix,	FilterStereo16BitHQRampMix,
// -! BEHAVIOUR_CHANGE#0025

	// Kaiser SRC
// -> CODE#0025
// -> DESC="enable polyphase resampling on stereo samples"
//	Mono8BitKaiserMix,			Mono16BitKaiserMix,			Stereo8BitHQMix,		Stereo16BitHQMix,
//	Mono8BitKaiserRampMix,		Mono16BitKaiserRampMix,		Stereo8BitHQRampMix,	Stereo16BitHQRampMix,
	Mono8BitKaiserMix,			Mono16BitKaiserMix,			Stereo8BitKaiserMix,	Stereo16BitKaiserMix,
	Mono8BitKaiserRampMix,		Mono16BitKaiserRampMix,		Stereo8BitKaiserRampMix,Stereo16BitKaiserRampMix,
// -! BEHAVIOUR_CHANGE#0025

	// Kaiser SRC, Filter
//	FilterMono8BitLinearMix,	FilterMono16BitLinearMix,	FilterStereo8BitLinearMix,	FilterStereo16BitLinearMix,
//	FilterMono8BitLinearRampMix,FilterMono16BitLinearRampMix,FilterStereo8BitLinearRampMix,FilterStereo16BitLinearRampMix,
	FilterMono8BitKaiserMix,	FilterMono16BitKaiserMix,	FilterStereo8BitKaiserMix,	FilterStereo16BitKaiserMix,
	FilterMono8BitKaiserRampMix,FilterMono16BitKaiserRampMix,FilterStereo8BitKaiserRampMix,FilterStereo16BitKaiserRampMix,

	// FIR Filter SRC
	Mono8BitFIRFilterMix,			Mono16BitFIRFilterMix,			Stereo8BitFIRFilterMix,	Stereo16BitFIRFilterMix,
	Mono8BitFIRFilterRampMix,		Mono16BitFIRFilterRampMix,		Stereo8BitFIRFilterRampMix,Stereo16BitFIRFilterRampMix,

	// FIR Filter SRC, Filter
	FilterMono8BitFIRFilterMix,	FilterMono16BitFIRFilterMix,	FilterStereo8BitFIRFilterMix,	FilterStereo16BitFIRFilterMix,
	FilterMono8BitFIRFilterRampMix,FilterMono16BitFIRFilterRampMix,FilterStereo8BitFIRFilterRampMix,FilterStereo16BitFIRFilterRampMix,

// -! BEHAVIOUR_CHANGE#0025


};

const LPMIXINTERFACE gpFastMixFunctionTable[2*16] =
{
	// No SRC
	FastMono8BitMix,			FastMono16BitMix,			Stereo8BitMix,			Stereo16BitMix,
	FastMono8BitRampMix,		FastMono16BitRampMix,		Stereo8BitRampMix,		Stereo16BitRampMix,
	// No SRC, Filter
	FilterMono8BitMix,			FilterMono16BitMix,			FilterStereo8BitMix,	FilterStereo16BitMix,
	FilterMono8BitRampMix,		FilterMono16BitRampMix,		FilterStereo8BitRampMix,FilterStereo16BitRampMix,
	// Linear SRC
	FastMono8BitLinearMix,		FastMono16BitLinearMix,		Stereo8BitLinearMix,	Stereo16BitLinearMix,
	FastMono8BitLinearRampMix,	FastMono16BitLinearRampMix,	Stereo8BitLinearRampMix,Stereo16BitLinearRampMix,
	// Linear SRC, Filter
	FilterMono8BitLinearMix,	FilterMono16BitLinearMix,	FilterStereo8BitLinearMix,	FilterStereo16BitLinearMix,
	FilterMono8BitLinearRampMix,FilterMono16BitLinearRampMix,FilterStereo8BitLinearRampMix,FilterStereo16BitLinearRampMix,
};

#else

namespace MixFuncTable
{
#ifdef MPT_INTMIXER
	typedef Int8MToIntS I8M;
	typedef Int16MToIntS I16M;
	typedef Int8SToIntS I8S;
	typedef Int16SToIntS I16S;
#else
	typedef Int8MToFloatS I8M;
	typedef Int16MToFloatS I16M;
	typedef Int8SToFloatS I8S;
	typedef Int16SToFloatS I16S;
#endif // MPT_INTMIXER

// Table index:
//	[b1-b0]	format (8-bit-mono, 16-bit-mono, 8-bit-stereo, 16-bit-stereo)
//	[b2]	ramp
//	[b3]	filter
//	[b6-b4]	src type

// Sample type / processing type index
enum FunctionIndex
{
	ndx16Bit		= 0x01,
	ndxStereo		= 0x02,
	ndxRamp			= 0x04,
	ndxFilter		= 0x08,
};

// SRC index
enum ResamplingIndex
{
	ndxNoInterpolation	= 0x00,
	ndxLinear			= 0x10,
	ndxFastSinc			= 0x20,
	ndxKaiser			= 0x30,
	ndxFIRFilter		= 0x40,
};

// Build mix function table for given resampling, filter and ramping settings: One function each for 8-Bit / 16-Bit Mono / Stereo
#define BuildMixFuncTableRamp(resampling, filter, ramp) \
	SampleLoop<I8M, resampling<I8M>, filter<I8M>, MixMono ## ramp<I8M> >, \
	SampleLoop<I16M, resampling<I16M>, filter<I16M>, MixMono ## ramp<I16M> >, \
	SampleLoop<I8S, resampling<I8S>, filter<I8S>, MixStereo ## ramp<I8S> >, \
	SampleLoop<I16S, resampling<I16S>, filter<I16S>, MixStereo ## ramp<I16S> >

// Build mix function table for given resampling, filter settings: With and without ramping
#define BuildMixFuncTableFilter(resampling, filter) \
	BuildMixFuncTableRamp(resampling, filter, NoRamp), \
	BuildMixFuncTableRamp(resampling, filter, Ramp)

// Build mix function table for given resampling settings: With and without filter
#define BuildMixFuncTable(resampling) \
	BuildMixFuncTableFilter(resampling, NoFilter), \
	BuildMixFuncTableFilter(resampling, ResonantFilter)

typedef void (*MixFuncInterface)(ModChannel &, const CResampler &, mixsample_t *, int);
const MixFuncInterface Functions[5 * 16] =
{
	BuildMixFuncTable(NoInterpolation),			// No SRC
	BuildMixFuncTable(LinearInterpolation),		// Linear SRC
	BuildMixFuncTable(FastSincInterpolation),	// HQ SRC
	BuildMixFuncTable(PolyphaseInterpolation),	// Kaiser SRC
	BuildMixFuncTable(FIRFilterInterpolation),	// FIR SRC
};

#undef BuildMixFuncTableRamp
#undef BuildMixFuncTableFilter
#undef BuildMixFuncTable
#endif

static forceinline ResamplingIndex ResamplingModeToMixFlags(uint8 resamplingMode)
//-------------------------------------------------------------------------------
{
	switch(resamplingMode)
	{
	case SRCMODE_NEAREST:   return ndxNoInterpolation;
	case SRCMODE_LINEAR:    return ndxLinear;
	case SRCMODE_SPLINE:    return ndxFastSinc;
	case SRCMODE_POLYPHASE: return ndxKaiser;
	case SRCMODE_FIRFILTER: return ndxFIRFilter;
	}
	return ndxNoInterpolation;
}

} // namespace MixFuncTable


/////////////////////////////////////////////////////
//

void InitMixBuffer(int *pBuffer, UINT nSamples);
void EndChannelOfs(ModChannel *pChannel, int *pBuffer, UINT nSamples);
void StereoFill(int *pBuffer, UINT nSamples, LPLONG lpROfs, LPLONG lpLOfs);


/////////////////////////////////////////////////////////////////////////

// Returns the number of samples (in 16.16 format) that are going to be read from a sample, given a mix buffer length and the channel's playback speed.
// Result is negative in case of backwards-playing sample.
static forceinline int32 BufferLengthToSamples(int32 mixBufferCount, const ModChannel &chn)
//-----------------------------------------------------------------------------------------
{
	return (mixBufferCount * chn.nInc + static_cast<int32>(chn.nPosLo));
}


// Returns the buffer length required to render a certain amount of samples, based on the channel's playback speed.
static forceinline int32 SamplesToBufferLength(int32 numSamples, const ModChannel &chn)
//-------------------------------------------------------------------------------------
{
	return std::max(1, ((numSamples << 16)/* + static_cast<int32>(chn.nPosLo) + 0xFFFF*/) / abs(chn.nInc));
}


// Check how many samples can be rendered without encountering loop or sample end, and also update loop position / direction
static forceinline int32 GetSampleCount(ModChannel &chn, int32 nSamples, bool ITBidiMode)
//---------------------------------------------------------------------------------------
{
	int32 nLoopStart = chn.dwFlags[CHN_LOOP] ? chn.nLoopStart : 0;
	int32 nInc = chn.nInc;

	if ((nSamples <= 0) || (!nInc) || (!chn.nLength)) return 0;
	// Under zero ?
	if ((int32)chn.nPos < nLoopStart)
	{
		if (nInc < 0)
		{
			// Invert loop for bidi loops
			int32 nDelta = ((nLoopStart - chn.nPos) << 16) - (chn.nPosLo & 0xffff);
			chn.nPos = nLoopStart | (nDelta >> 16);
			chn.nPosLo = nDelta & 0xffff;
			if (((int32)chn.nPos < nLoopStart) || (chn.nPos >= (nLoopStart+chn.nLength)/2))
			{
				chn.nPos = nLoopStart; chn.nPosLo = 0;
			}
			nInc = -nInc;
			chn.nInc = nInc;
			chn.dwFlags.reset(CHN_PINGPONGFLAG); // go forward
			if(!chn.dwFlags[CHN_LOOP] || chn.nPos >= chn.nLength)
			{
				chn.nPos = chn.nLength;
				chn.nPosLo = 0;
				return 0;
			}
		} else
		{
			// We probably didn't hit the loop end yet (first loop), so we do nothing
			if ((int32)chn.nPos < 0) chn.nPos = 0;
		}
	} else
	// Past the end
	if (chn.nPos >= chn.nLength)
	{
		if(!chn.dwFlags[CHN_LOOP]) return 0; // not looping -> stop this channel
		if(chn.dwFlags[CHN_PINGPONGLOOP])
		{
			// Invert loop
			if (nInc > 0)
			{
				nInc = -nInc;
				chn.nInc = nInc;
			}
			chn.dwFlags.set(CHN_PINGPONGFLAG);
			// adjust loop position
			int32 nDeltaHi = (chn.nPos - chn.nLength);
			int32 nDeltaLo = 0x10000 - (chn.nPosLo & 0xffff);
			chn.nPos = chn.nLength - nDeltaHi - (nDeltaLo>>16);
			chn.nPosLo = nDeltaLo & 0xffff;
			// Impulse Tracker's software mixer would put a -2 (instead of -1) in the following line (doesn't happen on a GUS)
			if ((chn.nPos <= chn.nLoopStart) || (chn.nPos >= chn.nLength)) chn.nPos = chn.nLength - (ITBidiMode ? 2 : 1);
		} else
		{
			if (nInc < 0) // This is a bug
			{
				nInc = -nInc;
				chn.nInc = nInc;
			}
			// Restart at loop start
			chn.nPos += nLoopStart - chn.nLength;
			if ((int32)chn.nPos < nLoopStart) chn.nPos = chn.nLoopStart;
		}
	}
	int32 nPos = chn.nPos;
	// too big increment, and/or too small loop length
	if (nPos < nLoopStart)
	{
		if ((nPos < 0) || (nInc < 0)) return 0;
	}
	if ((nPos < 0) || (nPos >= (int32)chn.nLength)) return 0;
	int32 nPosLo = (uint16)chn.nPosLo, nSmpCount = nSamples;
	if (nInc < 0)
	{
		int32 nInv = -nInc;
		int32 maxsamples = 16384 / ((nInv>>16)+1);
		if (maxsamples < 2) maxsamples = 2;
		if (nSamples > maxsamples) nSamples = maxsamples;
		int32 nDeltaHi = (nInv>>16) * (nSamples - 1);
		int32 nDeltaLo = (nInv&0xffff) * (nSamples - 1);
		int32 nPosDest = nPos - nDeltaHi + ((nPosLo - nDeltaLo) >> 16);
		if (nPosDest < nLoopStart)
		{
			nSmpCount = (uint32)(((((int64)nPos - nLoopStart) << 16) + nPosLo - 1) / nInv) + 1;
		}
	} else
	{
		int32 maxsamples = 16384 / ((nInc>>16)+1);
		if (maxsamples < 2) maxsamples = 2;
		if (nSamples > maxsamples) nSamples = maxsamples;
		int32 nDeltaHi = (nInc>>16) * (nSamples - 1);
		int32 nDeltaLo = (nInc&0xffff) * (nSamples - 1);
		int32 nPosDest = nPos + nDeltaHi + ((nPosLo + nDeltaLo)>>16);
		if (nPosDest >= (int32)chn.nLength)
		{
			nSmpCount = (uint32)(((((int64)chn.nLength - nPos) << 16) - nPosLo - 1) / nInc) + 1;
		}
	}
#ifdef _DEBUG
	{
		int32 nDeltaHi = (nInc>>16) * (nSmpCount - 1);
		int32 nDeltaLo = (nInc&0xffff) * (nSmpCount - 1);
		int32 nPosDest = nPos + nDeltaHi + ((nPosLo + nDeltaLo)>>16);
		if ((nPosDest < 0) || (nPosDest > (int32)chn.nLength))
		{
			Log("Incorrect delta:\n");
			Log("nSmpCount=%d: nPos=%5d.x%04X Len=%5d Inc=%2d.x%04X\n",
				nSmpCount, nPos, nPosLo, chn.nLength, chn.nInc>>16, chn.nInc&0xffff);
			return 0;
		}
	}
#endif
	if (nSmpCount <= 1) return 1;
	if (nSmpCount > nSamples) return nSamples;
	return nSmpCount;
}

// Render count * number of channels samples
void CSoundFile::CreateStereoMix(int count)
//-----------------------------------------
{
	LPLONG pOfsL, pOfsR;

	if (!count) return;

	// Resetting sound buffer
	StereoFill(MixSoundBuffer, count, &gnDryROfsVol, &gnDryLOfsVol);
	if(m_MixerSettings.gnChannels > 2) InitMixBuffer(MixRearBuffer, count*2);

	uint32 nchused = 0, nchmixed = 0;

	const bool ITPingPongMode = IsITPingPongMode();
	const bool realtimeMix = !IsRenderingToDisc();

	for(uint32 nChn = 0; nChn < m_nMixChannels; nChn++)
	{
		ModChannel &chn = Chn[ChnMix[nChn]];
		uint32 functionNdx = 0;
		int *pbuffer;

		if(!chn.pCurrentSample) continue;
		pOfsR = &gnDryROfsVol;
		pOfsL = &gnDryLOfsVol;
		if(chn.dwFlags[CHN_16BIT]) functionNdx |= MixFuncTable::ndx16Bit;
		if(chn.dwFlags[CHN_STEREO]) functionNdx |= MixFuncTable::ndxStereo;
	#ifndef NO_FILTER
		if(chn.dwFlags[CHN_FILTER]) functionNdx |= MixFuncTable::ndxFilter;
	#endif

		const MixFuncTable::ResamplingIndex resamplingMode = MixFuncTable::ResamplingModeToMixFlags(chn.resamplingMode);
		functionNdx |= resamplingMode;

#ifdef MPT_INTMIXER
		const LPMIXINTERFACE *pMixFuncTable;
		if ((functionNdx < 0x20) && (chn.rightVol == chn.leftVol)
		 && ((!chn.nRampLength) || (chn.rightRamp == chn.leftRamp)))
		{
			pMixFuncTable = gpFastMixFunctionTable;
		} else
		{
			pMixFuncTable = gpMixFunctionTable;
		}
#endif
		pbuffer = MixSoundBuffer;
#ifndef NO_REVERB
#ifdef ENABLE_MMX
		if(GetProcSupport() & PROCSUPPORT_MMX)
		{
			if(((m_MixerSettings.DSPMask & SNDDSP_REVERB) && !chn.dwFlags[CHN_NOREVERB]) || chn.dwFlags[CHN_REVERB])
			{
				pbuffer = m_Reverb.GetReverbSendBuffer(count);
				pOfsR = &m_Reverb.gnRvbROfsVol;
				pOfsL = &m_Reverb.gnRvbLOfsVol;
			}
		}
#endif
#endif
		if(chn.dwFlags[CHN_SURROUND] && m_MixerSettings.gnChannels > 2)
			pbuffer = MixRearBuffer;

		//Look for plugins associated with this implicit tracker channel.
		PLUGINDEX nMixPlugin = GetBestPlugin(ChnMix[nChn], PrioritiseInstrument, RespectMutes);

		if ((nMixPlugin > 0) && (nMixPlugin <= MAX_MIXPLUGINS))
		{
			SNDMIXPLUGINSTATE *pPlugin = m_MixPlugins[nMixPlugin - 1].pMixState;
			if ((pPlugin) && (pPlugin->pMixBuffer))
			{
				pbuffer = pPlugin->pMixBuffer;
				pOfsR = &pPlugin->nVolDecayR;
				pOfsL = &pPlugin->nVolDecayL;
				if (!(pPlugin->dwFlags & SNDMIXPLUGINSTATE::psfMixReady))
				{
					StereoFill(pbuffer, count, pOfsR, pOfsL);
					pPlugin->dwFlags |= SNDMIXPLUGINSTATE::psfMixReady;
				}
			}
		}
		nchused++;

		// Calculate offset of loop wrap-around buffer for this sample.
		const int8 * const samplePointer = static_cast<const int8 *>(chn.pCurrentSample);
		const int8 * lookaheadPointer = nullptr;
		const SmpLength lookaheadStart = chn.nLoopEnd - InterpolationMaxLookahead;
		// We only need to apply the loop wrap-around logic if the sample is actually looping and if interpolation is applied.
		// If there is no interpolation happening, there is no lookahead happening the sample read-out is exact.
		if(chn.dwFlags[CHN_LOOP] && resamplingMode != MixFuncTable::ndxNoInterpolation)
		{
			const bool loopEndsAtSampleEnd = chn.pModSample->uFlags[CHN_LOOP] && chn.pModSample->nLoopEnd == chn.pModSample->nLength;

			SmpLength lookaheadOffset = (loopEndsAtSampleEnd ? 0 : (3 * InterpolationMaxLookahead)) + chn.pModSample->nLength - chn.nLoopEnd;
			if(chn.InSustainLoop())
			{
				lookaheadOffset += 4 * InterpolationMaxLookahead;
			}
			lookaheadPointer = samplePointer + lookaheadOffset * chn.pModSample->GetBytesPerSample();
		}

		////////////////////////////////////////////////////
		uint32 naddmix = 0;
		int nsamples = count;
		// Keep mixing this sample until the buffer is filled.
		do
		{
			uint32 nrampsamples = nsamples;
			int32 nSmpCount;
			if(chn.nRampLength > 0)
			{
				if ((int32)nrampsamples > chn.nRampLength) nrampsamples = chn.nRampLength;
			}
			if((nSmpCount = GetSampleCount(chn, nrampsamples, ITPingPongMode)) <= 0)
			{
				// Stopping the channel
				chn.pCurrentSample = nullptr;
				chn.nLength = 0;
				chn.nPos = 0;
				chn.nPosLo = 0;
				chn.nRampLength = 0;
				EndChannelOfs(&chn, pbuffer, nsamples);
				*pOfsR += chn.nROfs;
				*pOfsL += chn.nLOfs;
				chn.nROfs = chn.nLOfs = 0;
				chn.dwFlags.reset(CHN_PINGPONGFLAG);
				break;
			}
			// Should we mix this channel ?
			if((nchmixed >= m_MixerSettings.m_nMaxMixChannels && realtimeMix)	// Too many channels
				|| (!chn.nRampLength && !(chn.leftVol | chn.rightVol)))			// Channel is completely silent
			{
				int32 delta = BufferLengthToSamples(nSmpCount, chn);
				chn.nPosLo = delta & 0xFFFF;
				chn.nPos += (delta >> 16);
				chn.nROfs = chn.nLOfs = 0;
				pbuffer += nSmpCount * 2;
				naddmix = 0;
			} else
			{
				// Do mixing

				// Loop wrap-around magic.
				if(lookaheadPointer != nullptr)
				{
					const int32 readLength = BufferLengthToSamples(nSmpCount, chn) >> 16;
					
					chn.pCurrentSample = samplePointer;
					if(chn.nPos >= lookaheadStart)
					{
						const int32 oldCount = nSmpCount;

						// When going backwards - we can only go back up to lookaheadStart.
						// When going forwards - read through the whole pre-computed wrap-around buffer if possible.
						const int32 samplesToRead = chn.nInc < 0
							? (chn.nPos - lookaheadStart)
							: 2 * InterpolationMaxLookahead - (chn.nPos - lookaheadStart);
						nSmpCount = SamplesToBufferLength(samplesToRead, chn);
						Limit(nSmpCount, 1, oldCount);
						chn.pCurrentSample = lookaheadPointer;
					} else if(chn.nInc > 0 && chn.nPos + readLength >= lookaheadStart && nSmpCount > 1)
					{
						// We shouldn't read that far if we're not using the pre-computed wrap-around buffer.
						const int32 oldCount = nSmpCount;
						nSmpCount = SamplesToBufferLength(lookaheadStart - chn.nPos, chn);
						Limit(nSmpCount, 1, oldCount - 1);
					}
				}


				int *pbufmax = pbuffer + (nSmpCount*2);
				chn.nROfs = - *(pbufmax-2);
				chn.nLOfs = - *(pbufmax-1);
#ifdef MPT_INTMIXER
				// Choose function for mixing
				LPMIXINTERFACE pMixFunc = (chn.nRampLength) ? pMixFuncTable[functionNdx|MIXNDX_RAMP] : pMixFuncTable[functionNdx];
				pMixFunc(&chn, &m_Resampler, pbuffer, pbufmax);
#else
				float buf[MIXBUFFERSIZE * 2];
				float *b2 = buf;
				int *b1 = pbuffer;
				while(b1 < pbufmax)
				{
					*(b2++) = *(b1++) * (1.0f / MIXING_CLIPMAX);
				}
				uint32 targetpos = chn.nPos + (BufferLengthToSamples(nSmpCount, chn) >> 16);
				MixFuncTable::Functions[functionNdx | (chn.nRampLength ? MixFuncTable::ndxRamp : 0)](chn, m_Resampler, buf, nSmpCount);
				ASSERT(chn.nPos == targetpos);
				b1 = pbuffer;
				b2 = buf;
				while(b1 < pbufmax)
				{
					*(b1++) = static_cast<int>(Clamp(*(b2++), -1.0f, 1.0f) * MIXING_CLIPMAX);
				}
#endif
				chn.nROfs += *(pbufmax-2);
				chn.nLOfs += *(pbufmax-1);
				pbuffer = pbufmax;
				naddmix = 1;
			}
			nsamples -= nSmpCount;
			if (chn.nRampLength)
			{
				chn.nRampLength -= nSmpCount;
				if (chn.nRampLength <= 0)
				{
					chn.nRampLength = 0;
					chn.leftVol = chn.newLeftVol;
					chn.rightVol = chn.newRightVol;
					chn.rightRamp = chn.leftRamp = 0;
					if(chn.dwFlags[CHN_NOTEFADE] && !chn.nFadeOutVol)
					{
						chn.nLength = 0;
						chn.pCurrentSample = nullptr;
					}
				}
			}
		} while(nsamples > 0);

		// Restore sample pointer in case it got changed through loop wrap-around
		chn.pCurrentSample = samplePointer;
		nchmixed += naddmix;
	}
	m_nMixStat += nchused;
}


void CSoundFile::ProcessPlugins(UINT nCount)
//------------------------------------------
{
	// Setup float inputs
	for(PLUGINDEX plug = 0; plug < MAX_MIXPLUGINS; plug++)
	{
		SNDMIXPLUGIN &plugin = m_MixPlugins[plug];
		if(plugin.pMixPlugin != nullptr && plugin.pMixState != nullptr
			&& plugin.pMixState->pMixBuffer != nullptr
			&& plugin.pMixState->pOutBufferL != nullptr
			&& plugin.pMixState->pOutBufferR != nullptr)
		{
			SNDMIXPLUGINSTATE *pState = plugin.pMixState;

			//We should only ever reach this point if the song is playing.
			if (!plugin.pMixPlugin->IsSongPlaying())
			{
				//Plugin doesn't know it is in a song that is playing;
				//we must have added it during playback. Initialise it!
				plugin.pMixPlugin->NotifySongPlaying(true);
				plugin.pMixPlugin->Resume();
			}


			// Setup float input
			if (pState->dwFlags & SNDMIXPLUGINSTATE::psfMixReady)
			{
				StereoMixToFloat(pState->pMixBuffer, pState->pOutBufferL, pState->pOutBufferR, nCount);
			} else
			if (pState->nVolDecayR|pState->nVolDecayL)
			{
				StereoFill(pState->pMixBuffer, nCount, &pState->nVolDecayR, &pState->nVolDecayL);
				StereoMixToFloat(pState->pMixBuffer, pState->pOutBufferL, pState->pOutBufferR, nCount);
			} else
			{
				memset(pState->pOutBufferL, 0, nCount * sizeof(float));
				memset(pState->pOutBufferR, 0, nCount * sizeof(float));
			}
			pState->dwFlags &= ~SNDMIXPLUGINSTATE::psfMixReady;
		}
	}
	// Convert mix buffer
	StereoMixToFloat(MixSoundBuffer, MixFloatBuffer[0], MixFloatBuffer[1], nCount);
	float *pMixL = MixFloatBuffer[0];
	float *pMixR = MixFloatBuffer[1];

	// Process Plugins
	for(PLUGINDEX plug = 0; plug < MAX_MIXPLUGINS; plug++)
	{
		SNDMIXPLUGIN &plugin = m_MixPlugins[plug];
		if (plugin.pMixPlugin != nullptr && plugin.pMixState != nullptr
			&& plugin.pMixState->pMixBuffer != nullptr
			&& plugin.pMixState->pOutBufferL != nullptr
			&& plugin.pMixState->pOutBufferR != nullptr)
		{
			bool isMasterMix = false;
			if (pMixL == plugin.pMixState->pOutBufferL)
			{
				isMasterMix = true;
				pMixL = MixFloatBuffer[0];
				pMixR = MixFloatBuffer[1];
			}
			IMixPlugin *pObject = plugin.pMixPlugin;
			SNDMIXPLUGINSTATE *pState = plugin.pMixState;
			float *pOutL = pMixL;
			float *pOutR = pMixR;

			if (!plugin.IsOutputToMaster())
			{
				PLUGINDEX nOutput = plugin.GetOutputPlugin();
				if(nOutput > plug && nOutput != PLUGINDEX_INVALID
					&& m_MixPlugins[nOutput].pMixState != nullptr)
				{
					SNDMIXPLUGINSTATE *pOutState = m_MixPlugins[nOutput].pMixState;

					if(pOutState->pOutBufferL != nullptr && pOutState->pOutBufferR != nullptr)
					{
						pOutL = pOutState->pOutBufferL;
						pOutR = pOutState->pOutBufferR;
					}
				}
			}

			/*
			if (plugin.multiRouting) {
				int nOutput=0;
				for (int nOutput=0; nOutput < plugin.nOutputs / 2; nOutput++) {
					destinationPlug = plugin.multiRoutingDestinations[nOutput];
					pOutState = m_MixPlugins[destinationPlug].pMixState;
					pOutputs[2 * nOutput] = pOutState->pOutBufferL;
					pOutputs[2 * (nOutput + 1)] = pOutState->pOutBufferR;
				}

			}*/

			if (plugin.IsMasterEffect())
			{
				if (!isMasterMix)
				{
					float *pInL = pState->pOutBufferL;
					float *pInR = pState->pOutBufferR;
					for (UINT i=0; i<nCount; i++)
					{
						pInL[i] += pMixL[i];
						pInR[i] += pMixR[i];
						pMixL[i] = 0;
						pMixR[i] = 0;
					}
				}
				pMixL = pOutL;
				pMixR = pOutR;
			}

			if (plugin.IsBypassed())
			{
				const float * const pInL = pState->pOutBufferL;
				const float * const pInR = pState->pOutBufferR;
				for (UINT i=0; i<nCount; i++)
				{
					pOutL[i] += pInL[i];
					pOutR[i] += pInR[i];
				}
			} else
			{
				pObject->Process(pOutL, pOutR, nCount);
			}
		}
	}
	FloatToStereoMix(pMixL, pMixR, MixSoundBuffer, nCount);
}


//////////////////////////////////////////////////////////////////////////////////////////

template<typename Tsample> forceinline Tsample ConvertSample(int val);

template<>
forceinline uint8 ConvertSample(int val)
{
	val = (val + (1<<(23-MIXING_ATTENUATION))) >> (24-MIXING_ATTENUATION);
	if(val < int8_min) val = int8_min;
	if(val > int8_max) val = int8_max;
	return (uint8)(val+0x80); // unsigned
}
template<>
forceinline int16 ConvertSample(int val)
{
	val = (val + (1<<(15-MIXING_ATTENUATION))) >> (16-MIXING_ATTENUATION);
	if(val < int16_min) val = int16_min;
	if(val > int16_max) val = int16_max;
	return (int16)val;
}
template<>
forceinline int24 ConvertSample(int val)
{
	val = (val + (1<<(7-MIXING_ATTENUATION))) >> (8-MIXING_ATTENUATION);
	if(val < int24_min) val = int24_min;
	if(val > int24_max) val = int24_max;
	return (int24)val;
}
template<>
forceinline int32 ConvertSample(int val)
{
	return (int32)(Clamp(val, (int)MIXING_CLIPMIN, (int)MIXING_CLIPMAX) << MIXING_ATTENUATION);
}

template<typename Tsample>
forceinline void C_Convert32ToInterleaved(Tsample *p, const int *mixbuffer, std::size_t count)
{
	for(std::size_t i = 0; i < count; ++i)
	{
		p[i] = ConvertSample<Tsample>(mixbuffer[i]);
	}
}

template<typename Tsample>
forceinline void C_Convert32ToNonInterleaved(Tsample * const * const buffers, const int *mixbuffer, std::size_t channels, std::size_t count)
{
	for(std::size_t i = 0; i < count; ++i)
	{
		for(std::size_t channel = 0; channel < channels; ++channel)
		{
			buffers[channel][i] = ConvertSample<Tsample>(*mixbuffer);
			mixbuffer++;
		}
	}
}


#ifdef ENABLE_X86
static void X86_Convert32To8(uint8 *lp8, const int *pBuffer, DWORD lSampleCount)
//------------------------------------------------------------------------------
{
	_asm {
	mov ebx, lp8			// ebx = 8-bit buffer
	mov edx, pBuffer		// edx = pBuffer
	mov edi, lSampleCount	// edi = lSampleCount
cliploop:
	mov eax, dword ptr [edx]
	inc ebx
	add eax, (1<<(23-MIXING_ATTENUATION))
	add edx, 4
	cmp eax, MIXING_CLIPMIN
	jl cliplow
	cmp eax, MIXING_CLIPMAX
	jg cliphigh
cliprecover:
	sar eax, (24-MIXING_ATTENUATION)
	xor eax, 0x80
	dec edi
	mov byte ptr [ebx-1], al
	jnz cliploop
	jmp done
cliplow:
	mov eax, MIXING_CLIPMIN
	jmp cliprecover
cliphigh:
	mov eax, MIXING_CLIPMAX
	jmp cliprecover
done:
	}
}
#endif

// Clip and convert to 8 bit
void Convert32ToInterleaved(uint8 *dest, const int *mixbuffer, std::size_t count)
//-------------------------------------------------------------------------------
{
	#ifdef ENABLE_X86
		X86_Convert32To8(dest, mixbuffer, count);
	#else
		C_Convert32ToInterleaved(dest, mixbuffer, count);
	#endif
}

void Convert32ToNonInterleaved(uint8 * const * const buffers, const int *mixbuffer, std::size_t channels, std::size_t count)
//--------------------------------------------------------------------------------------------------------------------------
{
	C_Convert32ToNonInterleaved(buffers, mixbuffer, channels, count);
}


#ifdef ENABLE_X86
static void X86_Convert32To16(int16 *lp16, const int *pBuffer, DWORD lSampleCount)
//--------------------------------------------------------------------------------
{
	_asm {
	mov ebx, lp16				// ebx = 16-bit buffer
	mov edx, pBuffer			// edx = pBuffer
	mov edi, lSampleCount		// edi = lSampleCount
cliploop:
	mov eax, dword ptr [edx]
	add ebx, 2
	add eax, (1<<(15-MIXING_ATTENUATION))
	add edx, 4
	cmp eax, MIXING_CLIPMIN
	jl cliplow
	cmp eax, MIXING_CLIPMAX
	jg cliphigh
cliprecover:
	sar eax, 16-MIXING_ATTENUATION
	dec edi
	mov word ptr [ebx-2], ax
	jnz cliploop
	jmp done
cliplow:
	mov eax, MIXING_CLIPMIN
	jmp cliprecover
cliphigh:
	mov eax, MIXING_CLIPMAX
	jmp cliprecover
done:
	}
}
#endif

// Clip and convert to 16 bit
void Convert32ToInterleaved(int16 *dest, const int *mixbuffer, std::size_t count)
//-------------------------------------------------------------------------------
{
	#ifdef ENABLE_X86
		X86_Convert32To16(dest, mixbuffer, count);
	#else
		C_Convert32ToInterleaved(dest, mixbuffer, count);
	#endif
}

void Convert32ToNonInterleaved(int16 * const * const buffers, const int *mixbuffer, std::size_t channels, std::size_t count)
//--------------------------------------------------------------------------------------------------------------------------
{
	C_Convert32ToNonInterleaved(buffers, mixbuffer, channels, count);
}


#ifdef ENABLE_X86
static void X86_Convert32To24(int24 *lp24, const int *pBuffer, DWORD lSampleCount)
//--------------------------------------------------------------------------------
{
	_asm {
	mov ebx, lp24			// ebx = 24-bit buffer
	mov edx, pBuffer		// edx = pBuffer
	mov edi, lSampleCount	// edi = lSampleCount
cliploop:
	mov eax, dword ptr [edx]
	add ebx, 3
	add eax, (1<<(7-MIXING_ATTENUATION))
	add edx, 4
	cmp eax, MIXING_CLIPMIN
	jl cliplow
	cmp eax, MIXING_CLIPMAX
	jg cliphigh
cliprecover:
	sar eax, (8-MIXING_ATTENUATION)
	mov word ptr [ebx-3], ax
	shr eax, 16
	dec edi
	mov byte ptr [ebx-1], al
	jnz cliploop
	jmp done
cliplow:
	mov eax, MIXING_CLIPMIN
	jmp cliprecover
cliphigh:
	mov eax, MIXING_CLIPMAX
	jmp cliprecover
done:
	}
}
#endif

// Clip and convert to 24 bit
void Convert32ToInterleaved(int24 *dest, const int *mixbuffer, std::size_t count)
//-------------------------------------------------------------------------------
{
	#ifdef ENABLE_X86
		X86_Convert32To24(dest, mixbuffer, count);
	#else
		C_Convert32ToInterleaved(dest, mixbuffer, count);
	#endif
}

void Convert32ToNonInterleaved(int24 * const * const buffers, const int *mixbuffer, std::size_t channels, std::size_t count)
//--------------------------------------------------------------------------------------------------------------------------
{
	C_Convert32ToNonInterleaved(buffers, mixbuffer, channels, count);
}


#ifdef ENABLE_X86
static void X86_Convert32To32(int32 *lp32, const int *pBuffer, DWORD lSampleCount)
//--------------------------------------------------------------------------------
{
	_asm {
	mov ebx, lp32			// ebx = 32-bit buffer
	mov edx, pBuffer		// edx = pBuffer
	mov edi, lSampleCount	// edi = lSampleCount
cliploop:
	mov eax, dword ptr [edx]
	add ebx, 4
	add edx, 4
	cmp eax, MIXING_CLIPMIN
	jl cliplow
	cmp eax, MIXING_CLIPMAX
	jg cliphigh
cliprecover:
	shl eax, MIXING_ATTENUATION
	dec edi
	mov dword ptr [ebx-4], eax
	jnz cliploop
	jmp done
cliplow:
	mov eax, MIXING_CLIPMIN
	jmp cliprecover
cliphigh:
	mov eax, MIXING_CLIPMAX
	jmp cliprecover
done:
	}
}
#endif

// Clip and convert to 32 bit
void Convert32ToInterleaved(int32 *dest, const int *mixbuffer, std::size_t count)
//-------------------------------------------------------------------------------
{
	#ifdef ENABLE_X86
		X86_Convert32To32(dest, mixbuffer, count);
	#else
		C_Convert32ToInterleaved(dest, mixbuffer, count);
	#endif
}

void Convert32ToNonInterleaved(int32 * const * const buffers, const int *mixbuffer, std::size_t channels, std::size_t count)
//--------------------------------------------------------------------------------------------------------------------------
{
	C_Convert32ToNonInterleaved(buffers, mixbuffer, channels, count);
}


// convert to 32 bit floats and do NOT clip to [-1,1]
void Convert32ToInterleaved(float *dest, const int *mixbuffer, std::size_t count)
//-------------------------------------------------------------------------------
{
	const float factor = (1.0f/(float)MIXING_CLIPMAX);
	for(std::size_t i=0; i<count; i++)
	{
		dest[i] = mixbuffer[i] * factor;
	}
}

void Convert32ToNonInterleaved(float * const * const buffers, const int *mixbuffer, std::size_t channels, std::size_t count)
//--------------------------------------------------------------------------------------------------------------------------
{
	const float factor = (1.0f/(float)MIXING_CLIPMAX);
	for(std::size_t i = 0; i < count; ++i)
	{
		for(std::size_t channel = 0; channel < channels; ++channel)
		{
			buffers[channel][i] = *mixbuffer * factor;
			mixbuffer++;
		}
	}
}


void InitMixBuffer(int *pBuffer, UINT nSamples)
//---------------------------------------------
{
	memset(pBuffer, 0, nSamples * sizeof(int));
}


//////////////////////////////////////////////////////////////////////////
// Noise Shaping (Dither)

#if MPT_COMPILER_MSVC
#pragma warning(disable:4731) // ebp modified
#endif


#ifdef ENABLE_X86

void X86_Dither(int *pBuffer, UINT nSamples, UINT nBits)
//------------------------------------------------------
{
	static int gDitherA, gDitherB;

	_asm {
	mov esi, pBuffer	// esi = pBuffer+i
	mov eax, nSamples	// ebp = i
	mov ecx, nBits		// ecx = number of bits of noise
	mov edi, gDitherA	// Noise generation
	mov ebx, gDitherB
	add ecx, MIXING_ATTENUATION+1
	push ebp
	mov ebp, eax
noiseloop:
	rol edi, 1
	mov eax, dword ptr [esi]
	xor edi, 0x10204080
	add esi, 4
	lea edi, [ebx*4+edi+0x78649E7D]
	mov edx, edi
	rol edx, 16
	lea edx, [edx*4+edx]
	add ebx, edx
	mov edx, ebx
	sar edx, cl
	add eax, edx
	dec ebp
	mov dword ptr [esi-4], eax
	jnz noiseloop
	pop ebp
	mov gDitherA, edi
	mov gDitherB, ebx
	}
}

#endif // ENABLE_X86


static forceinline int32 dither_rand(uint32 &a, uint32 &b)
//--------------------------------------------------------
{
	a = (a << 1) | (a >> 31);
	a ^= 0x10204080u;
	a += 0x78649E7Du + (b * 4);
	b += ((a << 16 ) | (a >> 16)) * 5;
	return (int32)b;
}

static void C_Dither(int *pBuffer, UINT nSamples, UINT nBits)
//-----------------------------------------------------------
{

	static uint32 global_a = 0;
	static uint32 global_b = 0;

	uint32 a = global_a;
	uint32 b = global_b;

	while(nSamples--)
	{
		*pBuffer += dither_rand(a, b) >> (nBits + MIXING_ATTENUATION + 1);
		pBuffer++;
	}

	global_a = a;
	global_b = b;

}

void Dither(int *pBuffer, UINT nSamples, UINT nBits)
//--------------------------------------------------
{
	#if defined(ENABLE_X86)
		X86_Dither(pBuffer, nSamples, nBits);
	#else
		C_Dither(pBuffer, nSamples, nBits);
	#endif
}


#ifdef ENABLE_X86
static void X86_InterleaveFrontRear(int *pFrontBuf, int *pRearBuf, DWORD nFrames)
//-------------------------------------------------------------------------------
{
	_asm {
	mov ecx, nFrames	// ecx = framecount
	mov esi, pFrontBuf	// esi = front buffer
	mov edi, pRearBuf	// edi = rear buffer
	lea esi, [esi+ecx*8]	// esi = &front[N*2]
	lea edi, [edi+ecx*8]	// edi = &rear[N*2]
	lea ebx, [esi+ecx*8]	// ebx = &front[N*4]
	push ebp
interleaveloop:
	mov eax, dword ptr [esi-8]
	mov edx, dword ptr [esi-4]
	sub ebx, 16
	mov ebp, dword ptr [edi-8]
	mov dword ptr [ebx], eax
	mov dword ptr [ebx+4], edx
	mov eax, dword ptr [edi-4]
	sub esi, 8
	sub edi, 8
	dec ecx
	mov dword ptr [ebx+8], ebp
	mov dword ptr [ebx+12], eax
	jnz interleaveloop
	pop ebp
	}
}
#endif

static void C_InterleaveFrontRear(int *pFrontBuf, int *pRearBuf, DWORD nFrames)
//-----------------------------------------------------------------------------
{
	// copy backwards as we are writing back into FrontBuf
	for(int i=nFrames-1; i>=0; i--)
	{
		pFrontBuf[i*4+3] = pRearBuf[i*2+1];
		pFrontBuf[i*4+2] = pRearBuf[i*2+0];
		pFrontBuf[i*4+1] = pFrontBuf[i*2+1];
		pFrontBuf[i*4+0] = pFrontBuf[i*2+0];
	}
}

void InterleaveFrontRear(int *pFrontBuf, int *pRearBuf, DWORD nFrames)
//--------------------------------------------------------------------
{
	#ifdef ENABLE_X86
		X86_InterleaveFrontRear(pFrontBuf, pRearBuf, nFrames);
	#else
		C_InterleaveFrontRear(pFrontBuf, pRearBuf, nFrames);
	#endif
}


#ifdef ENABLE_X86
static void X86_MonoFromStereo(int *pMixBuf, UINT nSamples)
//---------------------------------------------------------
{
	_asm {
	mov ecx, nSamples
	mov esi, pMixBuf
	mov edi, esi
stloop:
	mov eax, dword ptr [esi]
	mov edx, dword ptr [esi+4]
	add edi, 4
	add esi, 8
	add eax, edx
	sar eax, 1
	dec ecx
	mov dword ptr [edi-4], eax
	jnz stloop
	}
}
#endif

static void C_MonoFromStereo(int *pMixBuf, UINT nSamples)
//-------------------------------------------------------
{
	for(UINT i=0; i<nSamples; ++i)
	{
		pMixBuf[i] = (pMixBuf[i*2] + pMixBuf[i*2+1]) / 2;
	}
}

void MonoFromStereo(int *pMixBuf, UINT nSamples)
//----------------------------------------------
{
	#ifdef ENABLE_X86
		X86_MonoFromStereo(pMixBuf, nSamples);
	#else
		C_MonoFromStereo(pMixBuf, nSamples);
	#endif
}


#define OFSDECAYSHIFT	8
#define OFSDECAYMASK	0xFF


#ifdef ENABLE_X86
static void X86_StereoFill(int *pBuffer, UINT nSamples, LPLONG lpROfs, LPLONG lpLOfs)
//-----------------------------------------------------------------------------------
{
	_asm {
	mov edi, pBuffer
	mov ecx, nSamples
	mov eax, lpROfs
	mov edx, lpLOfs
	mov eax, [eax]
	mov edx, [edx]
	or ecx, ecx
	jz fill_loop
	mov ebx, eax
	or ebx, edx
	jz fill_loop
ofsloop:
	mov ebx, eax
	mov esi, edx
	neg ebx
	neg esi
	sar ebx, 31
	sar esi, 31
	and ebx, OFSDECAYMASK
	and esi, OFSDECAYMASK
	add ebx, eax
	add esi, edx
	sar ebx, OFSDECAYSHIFT
	sar esi, OFSDECAYSHIFT
	sub eax, ebx
	sub edx, esi
	mov ebx, eax
	or ebx, edx
	jz fill_loop
	add edi, 8
	dec ecx
	mov [edi-8], eax
	mov [edi-4], edx
	jnz ofsloop
fill_loop:
	mov ebx, ecx
	and ebx, 3
	jz fill4x
fill1x:
	mov [edi], eax
	mov [edi+4], edx
	add edi, 8
	dec ebx
	jnz fill1x
fill4x:
	shr ecx, 2
	or ecx, ecx
	jz done
fill4xloop:
	mov [edi], eax
	mov [edi+4], edx
	mov [edi+8], eax
	mov [edi+12], edx
	add edi, 8*4
	dec ecx
	mov [edi-16], eax
	mov [edi-12], edx
	mov [edi-8], eax
	mov [edi-4], edx
	jnz fill4xloop
done:
	mov esi, lpROfs
	mov edi, lpLOfs
	mov [esi], eax
	mov [edi], edx
	}
}
#endif

// c implementation taken from libmodplug
static void C_StereoFill(int *pBuffer, UINT nSamples, LPLONG lpROfs, LPLONG lpLOfs)
//---------------------------------------------------------------------------------
{
	int rofs = *lpROfs;
	int lofs = *lpLOfs;

	if ((!rofs) && (!lofs))
	{
		InitMixBuffer(pBuffer, nSamples*2);
		return;
	}
	for (UINT i=0; i<nSamples; i++)
	{
		int x_r = (rofs + (((-rofs)>>31) & OFSDECAYMASK)) >> OFSDECAYSHIFT;
		int x_l = (lofs + (((-lofs)>>31) & OFSDECAYMASK)) >> OFSDECAYSHIFT;
		rofs -= x_r;
		lofs -= x_l;
		pBuffer[i*2] = x_r;
		pBuffer[i*2+1] = x_l;
	}
	*lpROfs = rofs;
	*lpLOfs = lofs;
}

void StereoFill(int *pBuffer, UINT nSamples, LPLONG lpROfs, LPLONG lpLOfs)
//------------------------------------------------------------------------
{
	#ifdef ENABLE_X86
		X86_StereoFill(pBuffer, nSamples, lpROfs, lpLOfs);
	#else
		C_StereoFill(pBuffer, nSamples, lpROfs, lpLOfs);
	#endif
}


#ifdef ENABLE_X86
typedef ModChannel ModChannel_;
static void X86_EndChannelOfs(ModChannel *pChannel, int *pBuffer, UINT nSamples)
//------------------------------------------------------------------------------
{
	_asm {
	mov esi, pChannel
	mov edi, pBuffer
	mov ecx, nSamples
	mov eax, dword ptr [esi+ModChannel_.nROfs]
	mov edx, dword ptr [esi+ModChannel_.nLOfs]
	or ecx, ecx
	jz brkloop
ofsloop:
	mov ebx, eax
	mov esi, edx
	neg ebx
	neg esi
	sar ebx, 31
	sar esi, 31
	and ebx, OFSDECAYMASK
	and esi, OFSDECAYMASK
	add ebx, eax
	add esi, edx
	sar ebx, OFSDECAYSHIFT
	sar esi, OFSDECAYSHIFT
	sub eax, ebx
	sub edx, esi
	mov ebx, eax
	add dword ptr [edi], eax
	add dword ptr [edi+4], edx
	or ebx, edx
	jz brkloop
	add edi, 8
	dec ecx
	jnz ofsloop
brkloop:
	mov esi, pChannel
	mov dword ptr [esi+ModChannel_.nROfs], eax
	mov dword ptr [esi+ModChannel_.nLOfs], edx
	}
}
#endif

// c implementation taken from libmodplug
static void C_EndChannelOfs(ModChannel *pChannel, int *pBuffer, UINT nSamples)
//----------------------------------------------------------------------------
{

	int rofs = pChannel->nROfs;
	int lofs = pChannel->nLOfs;

	if ((!rofs) && (!lofs)) return;
	for (UINT i=0; i<nSamples; i++)
	{
		int x_r = (rofs + (((-rofs)>>31) & OFSDECAYMASK)) >> OFSDECAYSHIFT;
		int x_l = (lofs + (((-lofs)>>31) & OFSDECAYMASK)) >> OFSDECAYSHIFT;
		rofs -= x_r;
		lofs -= x_l;
		pBuffer[i*2] += x_r;
		pBuffer[i*2+1] += x_l;
	}
	pChannel->nROfs = rofs;
	pChannel->nLOfs = lofs;
}

void EndChannelOfs(ModChannel *pChannel, int *pBuffer, UINT nSamples)
//-------------------------------------------------------------------
{
	#ifdef ENABLE_X86
		X86_EndChannelOfs(pChannel, pBuffer, nSamples);
	#else
		C_EndChannelOfs(pChannel, pBuffer, nSamples);
	#endif
}
