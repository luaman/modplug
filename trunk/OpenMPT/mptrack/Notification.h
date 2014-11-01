/*
 * Notification.h
 * --------------
 * Purpose: GUI update notification struct
 * Notes  : (currently none)
 * Authors: Olivier Lapicque
 *          OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */

#pragma once

OPENMPT_NAMESPACE_BEGIN

// struct Notification requires working copy constructor / copy assignment, keep in mind when extending
struct Notification
{
	enum Type
	{
		GlobalVU	= 0x00,	// Global VU meters (always enabled)
		Position	= 0x01,	// Pattern playback position
		Sample		= 0x02,	// pos[i] contains sample position on this channel
		VolEnv		= 0x04,	// pos[i] contains volume envelope position
		PanEnv		= 0x08,	// pos[i] contains panning envelope position
		PitchEnv	= 0x10,	// pos[i] contains pitch envelope position
		VUMeters	= 0x20,	// pos[i] contains pattern VU meter for this channel
		EOS			= 0x40,	// End of stream reached, the GUI should stop the audio device
		Stop		= 0x80,	// Audio device has been stopped -> reset GUI

		Default		= GlobalVU,
	};

	typedef uint16 Item;

	static const SmpLength PosInvalid = SmpLength(-1);	// pos[i] is not valid (if it contains sample or envelope position)
	static const uint32 ClipVU = 0x80000000;			// Master VU clip indicator bit (sound output has previously clipped)

	int64 timestampSamples;
	FlagSet<Type, uint16> type;
	Item item;						// Sample or instrument number, depending on type
	ROWINDEX row;					// Always valid
	uint32 tick;					// dito
	ORDERINDEX order;				// dito
	PATTERNINDEX pattern;			// dito
	uint32 mixedChannels;			// dito
	uint32 masterVU[4];				// dito
	uint8 masterVUchannels;			// dito
	SmpLength pos[MAX_CHANNELS];	// Sample / envelope pos for each channel if != PosInvalid, or pattern channel VUs

	Notification(Type t = Default, Item i = 0, int64 s = 0, ROWINDEX r = 0, uint32 ti = 0, ORDERINDEX o = 0, PATTERNINDEX p = 0, uint32 x = 0, uint8 outChannels = 2) : timestampSamples(s), type(t), item(i), row(r), tick(ti), order(o), pattern(p), mixedChannels(x), masterVUchannels(outChannels)
	{
		MemsetZero(masterVU);
	}
};

DECLARE_FLAGSET(Notification::Type);

OPENMPT_NAMESPACE_END
