/*
 * IntMixer.h
 * ----------
 * Purpose: Fixed point mixer classes
 * Notes  : (currently none)
 * Authors: Olivier Lapicque
 *          OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#pragma once

#include "Resampler.h"
#include "MixerInterface.h"

template<int channelsOut, int channelsIn, typename out, typename in, size_t mixPrecision>
struct IntToIntTraits : public MixerTraits<channelsOut, channelsIn, out, in>
{
	static_assert(std::numeric_limits<input_t>::is_integer, "Input must be integer");
	static_assert(std::numeric_limits<output_t>::is_integer, "Output must be integer");
	static_assert(sizeof(out) * 8 >= mixPrecision, "Mix precision is higher than output type can handle");
	static_assert(sizeof(in) * 8 <= mixPrecision, "Mix precision is lower than input type");

	static forceinline output_t Convert(const input_t x)
	{
		return static_cast<output_t>(x) << (mixPrecision - sizeof(in) * 8));
	}
};

typedef IntToIntTraits<2, 1, mixsample_t, int8,  16> Int8MToIntS;
typedef IntToIntTraits<2, 1, mixsample_t, int16, 16> Int16MToIntS;
typedef IntToIntTraits<2, 2, mixsample_t, int8,  16> Int8SToIntS;
typedef IntToIntTraits<2, 2, mixsample_t, int16, 16> Int16SToIntS;


//////////////////////////////////////////////////////////////////////////
// Interpolation templates
