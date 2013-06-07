/*
 * Mixer.h
 * -------
 * Purpose: Basic mixer constants
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#pragma once

//#define MPT_INTMIXER

#ifdef MPT_INTMIXER
typedef int32 mixsample_t;
#else
typedef float mixsample_t;
#endif

#define MIXBUFFERSIZE 512

#define VOLUMERAMPPRECISION 12	// Fractional bits in volume ramp variables

enum
{
	MIXING_ATTENUATION = 4,
};

enum
{
	MIXING_CLIPMAX = ((1 << (32 - MIXING_ATTENUATION - 1)) - 1),
	MIXING_CLIPMIN = -(MIXING_CLIPMAX),
};

// The absolute maximum number of sampling points any interpolation algorithm is going to look at
#define InterpolationMaxLookahead 4u

