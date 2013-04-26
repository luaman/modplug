/*
 * Load_digi.cpp
 * -------------
 * Purpose: Digi Booster module loader
 * Notes  : Basically these are like ProTracker MODs with a few extra features such as more channels, longer channels and a few more effects.
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"
#include "Loaders.h"

#ifdef NEEDS_PRAGMA_PACK
#pragma pack(push, 1)
#endif

// DIGI File Header
struct PACKED DIGIFileHeader
{
	char   signature[20];
	char   versionStr[4];	// Supposed to be "V1.6" or similar, but other values like "TAP!" have been found as well.
	uint8  versionInt;		// e.g. 0x16 = 1.6
	uint8  numChannels;
	uint8  packEnable;
	char   unknown[19];
	uint8  lastPatIndex;
	uint8  lastOrdIndex;
	uint8  orders[128];
	uint32 smpLength[31];
	uint32 smpLoopStart[31];
	uint32 smpLoopLength[31];
	uint8  smpVolume[31];
	uint8  smpFinetune[31];

	// Convert all multi-byte numeric values to current platform's endianness or vice versa.
	void ConvertEndianness()
	{
		for(SAMPLEINDEX i = 0; i < 31; i++)
		{
			SwapBytesBE(smpLength[i]);
			SwapBytesBE(smpLoopStart[i]);
			SwapBytesBE(smpLoopLength[i]);
		}
	}
};

STATIC_ASSERT(sizeof(DIGIFileHeader) == 610);

#ifdef NEEDS_PRAGMA_PACK
#pragma pack(pop)
#endif


bool CSoundFile::ReadDIGI(FileReader &file)
//-----------------------------------------
{
	file.Rewind();

	DIGIFileHeader fileHeader;
	if(!file.ReadConvertEndianness(fileHeader)
		|| memcmp(fileHeader.signature, "DIGI Booster module\0", 20)
		|| !fileHeader.numChannels
		|| fileHeader.numChannels > 8
		|| fileHeader.lastOrdIndex > 127)
	{
		return false;
	}

	Order.ReadFromArray(fileHeader.orders, fileHeader.lastOrdIndex + 1);

	// Globals
	m_nType = MOD_TYPE_DIGI;
	m_nChannels = fileHeader.numChannels;
	m_nInstruments = 0;
	m_nSamples = 31;
	m_nSamplePreAmp = 256 / m_nChannels;
	m_nDefaultSpeed = 6;
	m_nDefaultTempo = 125;
	m_nDefaultGlobalVolume = MAX_GLOBAL_VOLUME;

	// Read sample headers
	for(SAMPLEINDEX smp = 0; smp < 31; smp++)
	{
		ModSample &sample = Samples[smp + 1];
		sample.Initialize(MOD_TYPE_MOD);
		sample.nLength = fileHeader.smpLength[smp];
		sample.nLoopStart = fileHeader.smpLoopStart[smp];
		sample.nLoopEnd = sample.nLoopStart + fileHeader.smpLoopLength[smp];
		if(fileHeader.smpLoopLength[smp])
		{
			sample.uFlags.set(CHN_LOOP);
		}
		sample.SanitizeLoops();
	
		sample.nVolume = std::min(fileHeader.smpVolume[smp], uint8(64)) * 4;
		sample.nFineTune = MOD2XMFineTune(fileHeader.smpFinetune[smp]);
	}

	// Read song + sample names
	file.ReadString<StringFixer::maybeNullTerminated>(m_szNames[0], 32);
	for(SAMPLEINDEX smp = 1; smp <= 31; smp++)
	{
		file.ReadString<StringFixer::maybeNullTerminated>(m_szNames[smp], 30);
	}

	for(PATTERNINDEX pat = 0; pat <= fileHeader.lastPatIndex; pat++)
	{
		if(Patterns.Insert(pat, 64))
		{
			break;
		}

		vector<uint8> eventMask(64, 0xFF);
		FileReader patternChunk;

		if(fileHeader.packEnable)
		{
			patternChunk = file.GetChunk(file.ReadUint16BE());
			patternChunk.ReadVector(eventMask, 64);
		} else
		{
			patternChunk = file.GetChunk(4 * 64 * GetNumChannels());
		}

		size_t i = 0;
		for(ROWINDEX row = 0; row < 64; row++)
		{
			PatternRow patRow = Patterns[pat].GetRow(row);
			uint8 bit = 0x80;
			for(CHANNELINDEX chn = 0; chn < GetNumChannels(); chn++, bit >>= 1)
			{
				ModCommand &m = patRow[chn];
				if(eventMask[row] & bit)
				{
					ReadMODPatternEntry(patternChunk, m);
					ConvertModCommand(m);
					if(m.command == CMD_MODCMDEX)
					{
						switch(m.param & 0xF0)
						{
						case 0x30:
							// E30 / E31: Play sample backwards (with some weird parameters that we won't support for now)
							if(m.param <= 0x31)
							{
								m.command = CMD_S3MCMDEX;
								m.param = 0x9F;
							}
							break;
						case 0x40:
							// E40: Stop playing sample
							if(m.param == 0x40)
							{
								m.note = NOTE_NOTECUT;
								m.command = CMD_NONE;
							}
							break;
						case 0x80:
							// E8x: High sample offset
							m.command = CMD_S3MCMDEX;
							m.param = 0xA0 | (m.param & 0x0F);
						}
					} else if(m.command == CMD_PANNING8)
					{
						// 8xx "Robot" effect (not supported)
						m.command = CMD_NONE;
					}
					i += 4;
				}
			}
		}
	}

	// Reading Samples
	const SampleIO sampleIO(
		SampleIO::_8bit,
		SampleIO::mono,
		SampleIO::bigEndian,
		SampleIO::signedPCM);

	for(SAMPLEINDEX smp = 1; smp <= 31; smp++)
	{
		sampleIO.ReadSample(Samples[smp], file);
	}

	return true;
}
