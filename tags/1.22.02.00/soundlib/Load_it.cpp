/*
 * Load_it.cpp
 * -----------
 * Purpose: IT (Impulse Tracker) module loader / saver
 * Notes  : Also handles MPTM loading / saving, as the formats are almost identical.
 * Authors: Olivier Lapicque
 *          OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"
#include "Loaders.h"
#include "tuningcollection.h"
#include "../mptrack/moddoc.h"
#include "../common/serialization_utils.h"
#include <fstream>
#include <strstream>
#include <list>
#include "../common/version.h"
#include "ITTools.h"

#define str_tooMuchPatternData	(GetStrI18N((_TEXT("Warning: File format limit was reached. Some pattern data may not get written to file."))))
#define str_pattern				(GetStrI18N((_TEXT("pattern"))))
#define str_PatternSetTruncationNote (GetStrI18N((_TEXT("The module contains %u patterns but only %u patterns can be loaded in this OpenMPT version."))))
#define str_SequenceTruncationNote (GetStrI18N((_TEXT("Module has sequence of length %u; it will be truncated to maximum supported length, %u."))))
#define str_LoadingIncompatibleVersion	TEXT("The file informed that it is incompatible with this version of OpenMPT. Loading was terminated.")
#define str_LoadingMoreRecentVersion	TEXT("The loaded file was made with a more recent OpenMPT version and this version may not be able to load all the features or play the file correctly.")

const uint16 verMptFileVer = 0x891;
const uint16 verMptFileVerLoadLimit = 0x1000; // If cwtv-field is greater or equal to this value,
											  // the MPTM file will not be loaded.

/*
MPTM version history for cwtv-field in "IT" header (only for MPTM files!):
0x890(1.18.02.00) -> 0x891(1.19.00.00): Pattern-specific time signatures
										Fixed behaviour of Pattern Loop command for rows > 255 (r617)
0x88F(1.18.01.00) -> 0x890(1.18.02.00): Removed volume command velocity :xy, added delay-cut command :xy.
0x88E(1.17.02.50) -> 0x88F(1.18.01.00): Numerous changes
0x88D(1.17.02.49) -> 0x88E(1.17.02.50): Changed ID to that of IT and undone the orderlist change done in
				       0x88A->0x88B. Now extended orderlist is saved as extension.
0x88C(1.17.02.48) -> 0x88D(1.17.02.49): Some tuning related changes - that part fails to read on older versions.
0x88B -> 0x88C: Changed type in which tuning number is printed to file: size_t -> uint16.
0x88A -> 0x88B: Changed order-to-pattern-index table type from BYTE-array to vector<UINT>.
*/


static bool AreNonDefaultTuningsUsed(CSoundFile& sf)
//--------------------------------------------------
{
	const INSTRUMENTINDEX iCount = sf.GetNumInstruments();
	for(INSTRUMENTINDEX i = 1; i <= iCount; i++)
	{
		if(sf.Instruments[i] != nullptr && sf.Instruments[i]->pTuning != 0)
			return true;
	}
	return false;
}

static void ReadTuningCollection(istream& iStrm, CTuningCollection& tc, const size_t) {tc.Deserialize(iStrm);}
static void WriteTuningCollection(ostream& oStrm, const CTuningCollection& tc) {tc.Serialize(oStrm);}

static void WriteTuningMap(ostream& oStrm, const CSoundFile& sf)
//--------------------------------------------------------------
{
	if(sf.GetNumInstruments() > 0)
	{
		//Writing instrument tuning data: first creating
		//tuning name <-> tuning id number map,
		//and then writing the tuning id for every instrument.
		//For example if there are 6 instruments and
		//first half use tuning 'T1', and the other half
		//tuning 'T2', the output would be something like
		//T1 1 T2 2 1 1 1 2 2 2

		//Creating the tuning address <-> tuning id number map.
		typedef map<CTuning*, uint16> TNTS_MAP;
		typedef TNTS_MAP::iterator TNTS_MAP_ITER;
		TNTS_MAP tNameToShort_Map;

		unsigned short figMap = 0;
		for(UINT i = 1; i <= sf.GetNumInstruments(); i++) if (sf.Instruments[i] != nullptr)
		{
			TNTS_MAP_ITER iter = tNameToShort_Map.find(sf.Instruments[i]->pTuning);
			if(iter != tNameToShort_Map.end())
				continue; //Tuning already mapped.

			tNameToShort_Map[sf.Instruments[i]->pTuning] = figMap;
			figMap++;
		}

		//...and write the map with tuning names replacing
		//the addresses.
		const uint16 tuningMapSize = static_cast<uint16>(tNameToShort_Map.size());
		oStrm.write(reinterpret_cast<const char*>(&tuningMapSize), sizeof(tuningMapSize));
		for(TNTS_MAP_ITER iter = tNameToShort_Map.begin(); iter != tNameToShort_Map.end(); iter++)
		{
			if(iter->first)
				StringToBinaryStream<uint8>(oStrm, iter->first->GetName());
			else //Case: Using original IT tuning.
				StringToBinaryStream<uint8>(oStrm, "->MPT_ORIGINAL_IT<-");

			srlztn::Binarywrite<uint16>(oStrm, iter->second);
		}

		//Writing tuning data for instruments.
		for(UINT i = 1; i <= sf.GetNumInstruments(); i++)
		{
			TNTS_MAP_ITER iter = tNameToShort_Map.find(sf.Instruments[i]->pTuning);
			if(iter == tNameToShort_Map.end()) //Should never happen
			{
				sf.AddToLog("Error: 210807_1");
				return;
			}
			srlztn::Binarywrite(oStrm, iter->second);
		}
	}
}


template<class TUNNUMTYPE, class STRSIZETYPE>
static bool ReadTuningMap(istream& iStrm, map<uint16, string>& shortToTNameMap, const size_t maxNum = 500)
//--------------------------------------------------------------------------------------------------------
{
	typedef map<uint16, string> MAP;
	typedef MAP::iterator MAP_ITER;
	TUNNUMTYPE numTuning = 0;
	iStrm.read(reinterpret_cast<char*>(&numTuning), sizeof(numTuning));
	if(numTuning > maxNum)
		return true;

	for(size_t i = 0; i<numTuning; i++)
	{
		string temp;
		uint16 ui;
		if(StringFromBinaryStream<STRSIZETYPE>(iStrm, temp, 255))
			return true;

		iStrm.read(reinterpret_cast<char*>(&ui), sizeof(ui));
		shortToTNameMap[ui] = temp;
	}
	if(iStrm.good())
		return false;
	else
		return true;
}


static void ReadTuningMap(istream& iStrm, CSoundFile& csf, const size_t = 0)
//--------------------------------------------------------------------------
{
	typedef map<WORD, string> MAP;
	typedef MAP::iterator MAP_ITER;
	MAP shortToTNameMap;
	ReadTuningMap<uint16, uint8>(iStrm, shortToTNameMap);

	//Read & set tunings for instruments
	std::list<string> notFoundTunings;
	for(UINT i = 1; i<=csf.GetNumInstruments(); i++)
	{
		uint16 ui;
		iStrm.read(reinterpret_cast<char*>(&ui), sizeof(ui));
		MAP_ITER iter = shortToTNameMap.find(ui);
		if(csf.Instruments[i] && iter != shortToTNameMap.end())
		{
			const string str = iter->second;

			if(str == string("->MPT_ORIGINAL_IT<-"))
			{
				csf.Instruments[i]->pTuning = nullptr;
				continue;
			}

			csf.Instruments[i]->pTuning = csf.GetTuneSpecificTunings().GetTuning(str);
			if(csf.Instruments[i]->pTuning)
				continue;

#ifdef MODPLUG_TRACKER
			csf.Instruments[i]->pTuning = csf.GetLocalTunings().GetTuning(str);
			if(csf.Instruments[i]->pTuning)
				continue;

			csf.Instruments[i]->pTuning = csf.GetBuiltInTunings().GetTuning(str);
			if(csf.Instruments[i]->pTuning)
				continue;

			if(str == "TET12" && csf.GetBuiltInTunings().GetNumTunings() > 0)
				csf.Instruments[i]->pTuning = &csf.GetBuiltInTunings().GetTuning(0);
#endif

			if(csf.Instruments[i]->pTuning)
				continue;

			//Checking if not found tuning already noticed.
			std::list<std::string>::iterator iter;
			iter = find(notFoundTunings.begin(), notFoundTunings.end(), str);
			if(iter == notFoundTunings.end())
			{
				notFoundTunings.push_back(str);
				std::string erm = std::string("Tuning ") + str + std::string(" used by the module was not found.");
				csf.AddToLog(erm);
#ifdef MODPLUG_TRACKER
				if(csf.GetpModDoc() != nullptr)
				{
					csf.GetpModDoc()->SetModified(); //The tuning is changed so the modified flag is set.
				}
#endif // MODPLUG_TRACKER

			}
			csf.Instruments[i]->pTuning = csf.GetDefaultTuning();

		}
		else //This 'else' happens probably only in case of corrupted file.
		{
			if(csf.Instruments[i])
				csf.Instruments[i]->pTuning = csf.GetDefaultTuning();
		}

	}
	//End read&set instrument tunings
}



//////////////////////////////////////////////////////////
// Impulse Tracker IT file support


static uint8 ConvertVolParam(const ModCommand *m)
//-----------------------------------------------
{
	return MIN(m->vol, 9);
}


size_t CSoundFile::ITInstrToMPT(FileReader &file, ModInstrument &ins, uint16 trkvers)
//-----------------------------------------------------------------------------------
{
	if(trkvers < 0x0200)
	{
		// Load old format (IT 1.xx) instrument
		ITOldInstrument instrumentHeader;
		if(!file.ReadConvertEndianness(instrumentHeader))
		{
			return 0;
		} else
		{
			instrumentHeader.ConvertToMPT(ins);
			return sizeof(ITOldInstrument);
		}
	} else
	{
		const FileReader::off_t offset = file.GetPosition();

		// Try loading extended instrument... instSize will differ between normal and extended instruments.
		ITInstrumentEx instrumentHeader;
		file.ReadStructPartial(instrumentHeader);
		instrumentHeader.ConvertEndianness();
		size_t instSize = instrumentHeader.ConvertToMPT(ins, GetType());
		file.Seek(offset + instSize);

		// Try reading modular instrument data
		instSize += LoadModularInstrumentData(file, ins);

		return instSize;
	}
}


static void CopyPatternName(CPattern &pattern, FileReader &file)
//--------------------------------------------------------------
{
	char name[MAX_PATTERNNAME] = "";
	file.ReadString<StringFixer::maybeNullTerminated>(name, MAX_PATTERNNAME);
	pattern.SetName(name);
}


bool CSoundFile::ReadIT(FileReader &file)
//---------------------------------------
{
	file.Rewind();

	ITFileHeader fileHeader;
	if(!file.ReadConvertEndianness(fileHeader)
		|| (fileHeader.id != ITFileHeader::itMagic && fileHeader.id != ITFileHeader::mptmMagic)
		|| fileHeader.insnum > 0xFF
		|| fileHeader.smpnum >= MAX_SAMPLES
		|| !file.CanRead(fileHeader.ordnum + (fileHeader.insnum + fileHeader.smpnum + fileHeader.patnum) * 4))
	{
		return false;
	}

	bool interpretModPlugMade = false;

	// OpenMPT crap at the end of file
	file.Seek(file.GetLength() - 4);
	size_t mptStartPos = file.ReadUint32LE();
	if(mptStartPos >= file.GetLength() || mptStartPos < 0x100)
	{
		mptStartPos = file.GetLength();
	}

	if(fileHeader.id == ITFileHeader::mptmMagic)
	{
		ChangeModTypeTo(MOD_TYPE_MPT);
	} else
	{
		if(mptStartPos <= file.GetLength() - 3 && fileHeader.cwtv > 0x888)
		{
			file.Seek(mptStartPos);
			ChangeModTypeTo(file.ReadMagic("228") ? MOD_TYPE_MPT : MOD_TYPE_IT);
		} else
		{
			ChangeModTypeTo(MOD_TYPE_IT);
		}

		if(GetType() == MOD_TYPE_IT)
		{
			// Which tracker was used to made this?
			if((fileHeader.cwtv & 0xF000) == 0x5000)
			{
				// OpenMPT Version number (Major.Minor)
				// This will only be interpreted as "made with ModPlug" (i.e. disable compatible playback etc) if the "reserved" field is set to "OMPT" - else, compatibility was used.
				m_dwLastSavedWithVersion = (fileHeader.cwtv & 0x0FFF) << 16;
				if(fileHeader.reserved == ITFileHeader::omptMagic)
					interpretModPlugMade = true;
			} else if(fileHeader.cmwt == 0x888 || fileHeader.cwtv == 0x888)
			{
				// OpenMPT 1.17 and 1.18 (raped IT format)
				// Exact version number will be determined later.
				interpretModPlugMade = true;
				m_dwLastSavedWithVersion = MAKE_VERSION_NUMERIC(1, 17, 00, 00);
			} else if(fileHeader.cwtv == 0x0217 && fileHeader.cmwt == 0x0200 && fileHeader.reserved == 0)
			{
				if(memchr(fileHeader.chnpan, 0xFF, sizeof(fileHeader.chnpan)) != NULL)
				{
					// ModPlug Tracker 1.16 (semi-raped IT format)
					m_dwLastSavedWithVersion = MAKE_VERSION_NUMERIC(1, 16, 00, 00);
				} else
				{
					// OpenMPT 1.17 disguised as this in compatible mode,
					// but never writes 0xFF in the pan map for unused channels (which is an invalid value).
					m_dwLastSavedWithVersion = MAKE_VERSION_NUMERIC(1, 17, 00, 00);
				}
				interpretModPlugMade = true;
			} else if(fileHeader.cwtv == 0x0214 && fileHeader.cmwt == 0x0202 && fileHeader.reserved == 0)
			{
				// ModPlug Tracker b3.3 - 1.09, instruments 557 bytes apart
				m_dwLastSavedWithVersion = MAKE_VERSION_NUMERIC(1, 09, 00, 00);
				interpretModPlugMade = true;
			}
			else if(fileHeader.cwtv == 0x0214 && fileHeader.cmwt == 0x0200 && fileHeader.reserved == 0)
			{
				// ModPlug Tracker 1.00a5, instruments 560 bytes apart
				m_dwLastSavedWithVersion = MAKE_VERSION_NUMERIC(1, 00, 00, A5);
				interpretModPlugMade = true;
			}
		} else // case: type == MOD_TYPE_MPT
		{
			if (fileHeader.cwtv >= verMptFileVerLoadLimit)
			{
				AddToLog(str_LoadingIncompatibleVersion);
				return false;
			}
			else if (fileHeader.cwtv > verMptFileVer)
			{
				AddToLog(str_LoadingMoreRecentVersion);
			}
		}
	}

	if(GetType() == MOD_TYPE_IT) mptStartPos = file.GetLength();

	// Read row highlights
	if((fileHeader.special & ITFileHeader::embedPatternHighlights))
	{
		// MPT 1.09, 1.07 and most likely other old MPT versions leave this blank (0/0), but have the "special" flag set.
		// Newer versions of MPT and OpenMPT 1.17 *always* write 4/16 here.
		// Thus, we will just ignore those old versions.
		if(m_dwLastSavedWithVersion == 0 || m_dwLastSavedWithVersion >= MAKE_VERSION_NUMERIC(1, 17, 03, 02))
		{
			m_nDefaultRowsPerBeat = fileHeader.highlight_minor;
			m_nDefaultRowsPerMeasure = fileHeader.highlight_major;
		}
#ifdef _DEBUG
		if((fileHeader.highlight_minor | fileHeader.highlight_major) == 0)
		{
			Log("IT Header: Row highlight is 0");
		}
#endif
	}

	m_SongFlags.set(SONG_LINEARSLIDES, (fileHeader.flags & ITFileHeader::linearSlides) != 0);
	m_SongFlags.set(SONG_ITOLDEFFECTS, (fileHeader.flags & ITFileHeader::itOldEffects) != 0);
	m_SongFlags.set(SONG_ITCOMPATGXX, (fileHeader.flags & ITFileHeader::itCompatGxx) != 0);
	m_SongFlags.set(SONG_EMBEDMIDICFG, (fileHeader.flags & ITFileHeader::reqEmbeddedMIDIConfig) || (fileHeader.special & ITFileHeader::embedMIDIConfiguration));
	m_SongFlags.set(SONG_EXFILTERRANGE, (fileHeader.flags & ITFileHeader::extendedFilterRange) != 0);

	StringFixer::ReadString<StringFixer::spacePadded>(m_szNames[0], fileHeader.songname);

	// Global Volume
	m_nDefaultGlobalVolume = fileHeader.globalvol << 1;
	if(m_nDefaultGlobalVolume > MAX_GLOBAL_VOLUME) m_nDefaultGlobalVolume = MAX_GLOBAL_VOLUME;
	if(fileHeader.speed) m_nDefaultSpeed = fileHeader.speed;
	m_nDefaultTempo = std::max(uint8(32), fileHeader.tempo); // Tempo 31 is possible. due to conflicts with the rest of the engine, let's just clamp it to 32.
	m_nSamplePreAmp = std::min(fileHeader.mv, uint8(128));

	// Reading Channels Pan Positions
	for(CHANNELINDEX i = 0; i < 64; i++) if(fileHeader.chnpan[i] != 0xFF)
	{
		ChnSettings[i].nVolume = Clamp(fileHeader.chnvol[i], uint8(0), uint8(64));
		ChnSettings[i].nPan = 128;
		ChnSettings[i].dwFlags.reset();
		if(fileHeader.chnpan[i] & 0x80) ChnSettings[i].dwFlags.set(CHN_MUTE);
		uint8 n = fileHeader.chnpan[i] & 0x7F;
		if(n <= 64) ChnSettings[i].nPan = n * 4;
		if(n == 100) ChnSettings[i].dwFlags.set(CHN_SURROUND);
	}

	// Reading orders
	file.Seek(sizeof(ITFileHeader));
	if(GetType() == MOD_TYPE_IT)
	{
		Order.ReadAsByte(file, fileHeader.ordnum);
	} else
	{
		ORDERINDEX ordSize = fileHeader.ordnum;
		if(fileHeader.ordnum > GetModSpecifications().ordersMax)
		{
			mpt::String str;
			str.Format(str_SequenceTruncationNote, fileHeader.ordnum, GetModSpecifications().ordersMax);
			AddToLog(str);
			ordSize = GetModSpecifications().ordersMax;
		}

		if(fileHeader.cwtv > 0x88A && fileHeader.cwtv <= 0x88D)
		{
			Order.Deserialize(file);
		} else
		{
			Order.ReadAsByte(file, fileHeader.ordnum);
			// Replacing 0xFF and 0xFE with new corresponding indexes
			Order.Replace(0xFE, Order.GetIgnoreIndex());
			Order.Replace(0xFF, Order.GetInvalidPatIndex());
		}
	}

	// Reading instrument, sample and pattern offsets
	vector<uint32> insPos, smpPos, patPos;
	file.ReadVector(insPos, fileHeader.insnum);
	file.ReadVector(smpPos, fileHeader.smpnum);
	file.ReadVector(patPos, fileHeader.patnum);

	// Find the first parapointer.
	// This is used for finding out whether the edit history is actually stored in the file or not,
	// as some early versions of Schism Tracker set the history flag, but didn't save anything.
	// We will consider the history invalid if it ends after the first parapointer.
	uint32 minPtr = Util::MaxValueOfType(minPtr);
	for(uint16 n = 0; n < fileHeader.insnum; n++)
	{
		if(insPos[n] > 0)
		{
			minPtr = std::min(minPtr, insPos[n]);
		}
	}

	for(uint16 n = 0; n < fileHeader.smpnum; n++)
	{
		if(smpPos[n] > 0)
		{
			minPtr = std::min(minPtr, smpPos[n]);
		}
	}

	for(uint16 n = 0; n < fileHeader.patnum; n++)
	{
		if(patPos[n] > 0)
		{
			minPtr = std::min(minPtr, patPos[n]);
		}
	}

	if(fileHeader.special & ITFileHeader::embedSongMessage)
	{
		minPtr = std::min(minPtr, fileHeader.msgoffset);
	}

	// Reading IT Edit History Info
	// This is only supposed to be present if bit 1 of the special flags is set.
	// However, old versions of Schism and probably other trackers always set this bit
	// even if they don't write the edit history count. So we have to filter this out...
	// This is done by looking at the parapointers. If the history data end after
	// the first parapointer, we assume that it's actually no history data.
	if(fileHeader.special & ITFileHeader::embedEditHistory)
	{
		const uint16 nflt = file.ReadUint16LE();

		if(file.CanRead(nflt * sizeof(ITHistoryStruct)) && file.GetPosition() + nflt * sizeof(ITHistoryStruct) <= minPtr)
		{
#ifdef MODPLUG_TRACKER
			if(GetpModDoc() != nullptr)
			{
				GetpModDoc()->GetFileHistory().reserve(nflt);
				for(size_t n = 0; n < nflt; n++)
				{
					FileHistory mptHistory;
					ITHistoryStruct itHistory;
					file.Read(itHistory);
					itHistory.ConvertToMPT(mptHistory);
					GetpModDoc()->GetFileHistory().push_back(mptHistory);
				}
			} else
#endif // MODPLUG_TRACKER
			{
				file.Skip(nflt * sizeof(ITHistoryStruct));
			}
		} else
		{
			// Oops, we were not supposed to read this.
			file.SkipBack(2);
		}
	} else if(fileHeader.highlight_major == 0 && fileHeader.highlight_minor == 0 && fileHeader.cmwt == 0x0214 && fileHeader.cwtv == 0x0214 && fileHeader.reserved == 0 && (fileHeader.special & (ITFileHeader::embedEditHistory | ITFileHeader::embedPatternHighlights)) == 0)
	{
		// Another non-conforming application is unmo3 < v2.4.0.1, which doesn't set the special bit
		// at all, but still writes the two edit history length bytes (zeroes)...
		if(file.ReadUint16LE() != 0)
		{
			// These were not zero bytes -> We're in the wrong place!
			file.SkipBack(2);
		}
	}

	// Reading MIDI Output & Macros
	if(m_SongFlags[SONG_EMBEDMIDICFG] && file.Read(m_MidiCfg))
	{
			m_MidiCfg.Sanitize();
	}

	// Ignore MIDI data. Fixes some files like denonde.it that were made with old versions of Impulse Tracker (which didn't support Zxx filters) and have Zxx effects in the patterns.
	if(fileHeader.cwtv < 0x0214)
	{
		MemsetZero(m_MidiCfg.szMidiSFXExt);
		MemsetZero(m_MidiCfg.szMidiZXXExt);
		m_SongFlags.set(SONG_EMBEDMIDICFG);
	}

	// Read pattern names: "PNAM"
	FileReader patNames;
	if(file.ReadMagic("PNAM"))
	{
		patNames = file.GetChunk(file.ReadUint32LE());
	}

	m_nChannels = GetModSpecifications().channelsMin;
	// Read channel names: "CNAM"
	if(file.ReadMagic("CNAM"))
	{
		FileReader chnNames = file.GetChunk(file.ReadUint32LE());
		const CHANNELINDEX readChns = std::min(MAX_BASECHANNELS, static_cast<CHANNELINDEX>(chnNames.GetLength() / MAX_CHANNELNAME));
		m_nChannels = readChns;

		for(CHANNELINDEX i = 0; i < readChns; i++)
		{
			chnNames.ReadString<StringFixer::maybeNullTerminated>(ChnSettings[i].szName, MAX_CHANNELNAME);
		}
	}

	// Read mix plugins information
	if(file.BytesLeft() > 8)
	{
		LoadMixPlugins(file);
	}

	// Read Song Message
	if(fileHeader.special & ITFileHeader::embedSongMessage)
	{
		if(fileHeader.msglength > 0 && file.Seek(fileHeader.msgoffset))
		{
			// Generally, IT files should use CR for line endings. However, ChibiTracker uses LF. One could do...
			// if(itHeader.cwtv == 0x0214 && itHeader.cmwt == 0x0214 && itHeader.reserved == ITFileHeader::chibiMagic) --> Chibi detected.
			// But we'll just use autodetection here:
			songMessage.Read(file, fileHeader.msglength, SongMessage::leAutodetect);
		}
	}

	// Reading Instruments
	m_nInstruments = 0;
	if(fileHeader.flags & ITFileHeader::instrumentMode)
	{
		m_nInstruments = std::min(fileHeader.insnum, INSTRUMENTINDEX(MAX_INSTRUMENTS - 1));
	}
	for(INSTRUMENTINDEX i = 0; i < GetNumInstruments(); i++)
	{
		if(insPos[i] > 0 && file.Seek(insPos[i]) && file.CanRead(fileHeader.cmwt < 0x200 ? sizeof(ITOldInstrument) : sizeof(ITInstrument)))
		{
			ModInstrument *instrument = AllocateInstrument(i + 1);
			if(instrument != nullptr)
			{
				ITInstrToMPT(file, *instrument, fileHeader.cmwt);
				// MIDI Pitch Wheel Depth is a global setting in IT. Apply it to all instruments.
				instrument->midiPWD = fileHeader.pwd;
			}
		}
	}

	// In order to properly compute the position, in file, of eventual extended settings
	// such as "attack" we need to keep the "real" size of the last sample as those extra
	// setting will follow this sample in the file
	FileReader::off_t lastSampleOffset = 0;
	if(fileHeader.smpnum > 0)
	{
		lastSampleOffset = smpPos[fileHeader.smpnum - 1] + sizeof(ITSample);
	}

	// Reading Samples
	m_nSamples = std::min(fileHeader.smpnum, SAMPLEINDEX(MAX_SAMPLES - 1));
	for(SAMPLEINDEX i = 0; i < GetNumSamples(); i++)
	{
		ITSample sampleHeader;
		if(smpPos[i] > 0 && file.Seek(smpPos[i]) && file.ReadConvertEndianness(sampleHeader))
		{
			if(sampleHeader.id == ITSample::magic)
			{
				size_t sampleOffset = sampleHeader.ConvertToMPT(Samples[i + 1]);

				StringFixer::ReadString<StringFixer::spacePadded>(m_szNames[i + 1], sampleHeader.name);

				if(file.Seek(sampleOffset))
				{
					sampleHeader.GetSampleFormat(fileHeader.cwtv).ReadSample(Samples[i + 1], file);
					lastSampleOffset = std::max(lastSampleOffset, file.GetPosition());
				}
			}
		}
	}
	m_nSamples = std::max(SAMPLEINDEX(1), GetNumSamples());

	m_nMinPeriod = 8;
	m_nMaxPeriod = 0xF000;

	// Compute extra instruments settings position
	if(lastSampleOffset > 0)
	{
		file.Seek(lastSampleOffset);
	}

	// Load instrument and song extensions.
	LoadExtendedInstrumentProperties(file, &interpretModPlugMade);
	if(interpretModPlugMade)
	{
		m_nMixLevels = mixLevels_original;
	}
	LoadExtendedSongProperties(GetType(), file, &interpretModPlugMade);

	const PATTERNINDEX numPats = std::min(static_cast<PATTERNINDEX>(patPos.size()), GetModSpecifications().patternsMax);

	if(numPats != patPos.size())
	{
		// Hack: Notify user here if file contains more patterns than what can be read.
		mpt::String str;
		str.Format(str_PatternSetTruncationNote, patPos.size(), numPats);
		AddToLog(str);
	}

	// Checking for number of used channels, which is not explicitely specified in the file.
	for(PATTERNINDEX pat = 0; pat < numPats; pat++)
	{
		if(patPos[pat] == 0 || !file.Seek(patPos[pat]))
			continue;

		uint16 len = file.ReadUint16LE();
		ROWINDEX numRows = file.ReadUint16LE();

		if(numRows < GetModSpecifications().patternRowsMin
			|| numRows > GetModSpecifications().patternRowsMax
			|| !file.Skip(4))
			continue;

		FileReader patternData = file.GetChunk(len);
		ROWINDEX row = 0;
		vector<uint8> chnMask(GetNumChannels());

		while(row < numRows && patternData.BytesLeft())
		{
			uint8 b = patternData.ReadUint8();
			if(!b)
			{
				row++;
				continue;
			}

			CHANNELINDEX ch = (b & IT_bitmask_patternChanField_c);   // 0x7f We have some data grab a byte keeping only 7 bits
			if(ch)
			{
				ch = (ch - 1);// & IT_bitmask_patternChanMask_c;   // 0x3f mask of the byte again, keeping only 6 bits
			}

			if(ch >= chnMask.size())
			{
				chnMask.resize(ch + 1, 0);
			}

			if(b & IT_bitmask_patternChanEnabled_c)            // 0x80 check if the upper bit is enabled.
			{
				chnMask[ch] = patternData.ReadUint8();       // set the channel mask for this channel.
			}
			// Channel used
			if(chnMask[ch] & 0x0F)         // if this channel is used set m_nChannels
			{
				if(ch >= GetNumChannels() && ch < MAX_BASECHANNELS)
				{
					m_nChannels = ch + 1;
				}
			}
			// Now we actually update the pattern-row entry the note,instrument etc.
			// Note
			if(chnMask[ch] & 1) patternData.Skip(1);
			// Instrument
			if(chnMask[ch] & 2) patternData.Skip(1);
			// Volume
			if(chnMask[ch] & 4) patternData.Skip(1);
			// Effect
			if(chnMask[ch] & 8) patternData.Skip(2);
		}
	}
	// Reading Patterns
	Patterns.ResizeArray(std::max(MAX_PATTERNS, numPats));
	for(PATTERNINDEX pat = 0; pat < numPats; pat++)
	{
		if(patPos[pat] == 0 || !file.Seek(patPos[pat]))
		{
			// Empty 64-row pattern
			if(Patterns.Insert(pat, 64))
			{
				mpt::String s;
				s.Format("Allocating patterns failed starting from pattern %u", pat);
				AddToLog(s);
				break;
			}
			// Now (after the Insert() call), we can read the pattern name.
			CopyPatternName(Patterns[pat], patNames);
			continue;
		}

		uint16 len = file.ReadUint16LE();
		ROWINDEX numRows = file.ReadUint16LE();

		if(numRows < GetModSpecifications().patternRowsMin
			|| numRows > GetModSpecifications().patternRowsMax
			|| !file.Skip(4)
			|| Patterns.Insert(pat, numRows))
			continue;
			
		FileReader patternData = file.GetChunk(len);

		// Now (after the Insert() call), we can read the pattern name.
		CopyPatternName(Patterns[pat], patNames);

		vector<uint8> chnMask(GetNumChannels());
		vector<ModCommand> lastValue(GetNumChannels(), ModCommand::Empty());

		ModCommand *m = Patterns[pat];
		ROWINDEX row = 0;
		while(row < numRows && patternData.BytesLeft())
		{
			uint8 b = patternData.ReadUint8();
			if(!b)
			{
				row++;
				m += GetNumChannels();
				continue;
			}

			CHANNELINDEX ch = b & IT_bitmask_patternChanField_c; // 0x7f

			if(ch)
			{
				ch = (ch - 1); //& IT_bitmask_patternChanMask_c; // 0x3f
			}

			if(ch >= chnMask.size())
			{
				chnMask.resize(ch + 1, 0);
				lastValue.resize(ch + 1, ModCommand::Empty());
				ASSERT(chnMask.size() <= GetNumChannels());
			}

			if(b & IT_bitmask_patternChanEnabled_c)  // 0x80
			{
				chnMask[ch] = patternData.ReadUint8();
			}

			// Now we grab the data for this particular row/channel.

			if((chnMask[ch] & 0x10) && (ch < m_nChannels))
			{
				m[ch].note = lastValue[ch].note;
			}
			if((chnMask[ch] & 0x20) && (ch < m_nChannels))
			{
				m[ch].instr = lastValue[ch].instr;
			}
			if((chnMask[ch] & 0x40) && (ch < m_nChannels))
			{
				m[ch].volcmd = lastValue[ch].volcmd;
				m[ch].vol = lastValue[ch].vol;
			}
			if((chnMask[ch] & 0x80) && (ch < m_nChannels))
			{
				m[ch].command = lastValue[ch].command;
				m[ch].param = lastValue[ch].param;
			}
			if(chnMask[ch] & 1)	// Note
			{
				uint8 note = patternData.ReadUint8();
				if(ch < m_nChannels)
				{
					if(note < 0x80) note++;
					if(!(GetType() & MOD_TYPE_MPT))
					{
						if(note > NOTE_MAX && note < 0xFD) note = NOTE_FADE;
						else if(note == 0xFD) note = NOTE_NONE;
					}
					m[ch].note = note;
					lastValue[ch].note = note;
				}
			}
			if(chnMask[ch] & 2)
			{
				uint8 instr = patternData.ReadUint8();
				if(ch < m_nChannels)
				{
					m[ch].instr = instr;
					lastValue[ch].instr = instr;
				}
			}
			if(chnMask[ch] & 4)
			{
				uint8 vol = patternData.ReadUint8();
				if(ch < m_nChannels)
				{
					// 0-64: Set Volume
					if(vol <= 64) { m[ch].volcmd = VOLCMD_VOLUME; m[ch].vol = vol; } else
					// 128-192: Set Panning
					if(vol >= 128 && vol <= 192) { m[ch].volcmd = VOLCMD_PANNING; m[ch].vol = vol - 128; } else
					// 65-74: Fine Volume Up
					if(vol < 75) { m[ch].volcmd = VOLCMD_FINEVOLUP; m[ch].vol = vol - 65; } else
					// 75-84: Fine Volume Down
					if(vol < 85) { m[ch].volcmd = VOLCMD_FINEVOLDOWN; m[ch].vol = vol - 75; } else
					// 85-94: Volume Slide Up
					if(vol < 95) { m[ch].volcmd = VOLCMD_VOLSLIDEUP; m[ch].vol = vol - 85; } else
					// 95-104: Volume Slide Down
					if(vol < 105) { m[ch].volcmd = VOLCMD_VOLSLIDEDOWN; m[ch].vol = vol - 95; } else
					// 105-114: Pitch Slide Up
					if(vol < 115) { m[ch].volcmd = VOLCMD_PORTADOWN; m[ch].vol = vol - 105; } else
					// 115-124: Pitch Slide Down
					if(vol < 125) { m[ch].volcmd = VOLCMD_PORTAUP; m[ch].vol = vol - 115; } else
					// 193-202: Portamento To
					if(vol >= 193 && vol <= 202) { m[ch].volcmd = VOLCMD_TONEPORTAMENTO; m[ch].vol = vol - 193; } else
					// 203-212: Vibrato depth
					if(vol >= 203 && vol <= 212)
					{
						m[ch].volcmd = VOLCMD_VIBRATODEPTH; m[ch].vol = vol - 203;
						// Old versions of ModPlug saved this as vibrato speed instead, so let's fix that.
						if(m[ch].vol && m_dwLastSavedWithVersion && m_dwLastSavedWithVersion <= MAKE_VERSION_NUMERIC(1, 17, 02, 54))
							m[ch].volcmd = VOLCMD_VIBRATOSPEED;
					} else
					// 213-222: Unused (was velocity)
					// 223-232: Offset
					if(vol >= 223 && vol <= 232) { m[ch].volcmd = VOLCMD_OFFSET; m[ch].vol = vol - 223; }
					lastValue[ch].volcmd = m[ch].volcmd;
					lastValue[ch].vol = m[ch].vol;
				}
			}
			// Reading command/param
			if(chnMask[ch] & 8)
			{
				uint8 cmd = patternData.ReadUint8();
				uint8 param = patternData.ReadUint8();
				if(ch < m_nChannels)
				{
					if(cmd)
					{
						m[ch].command = cmd;
						m[ch].param = param;
						S3MConvert(m[ch], true);
						lastValue[ch].command = m[ch].command;
						lastValue[ch].param = m[ch].param;
					}
				}
			}
		}
	}

	UpgradeModFlags();

	if(GetType() == MOD_TYPE_IT)
	{
		// Set appropriate mod flags if the file was not made with MPT.
		if(!interpretModPlugMade)
		{
			SetModFlag(MSF_MIDICC_BUGEMULATION, false);
			SetModFlag(MSF_OLDVOLSWING, false);
			SetModFlag(MSF_COMPATIBLE_PLAY, true);
		}
	} else
	{
		//START - mpt specific:
		//Using member cwtv on pifh as the version number.
		const uint16 version = fileHeader.cwtv;
		if(version > 0x889 && file.Seek(mptStartPos))
		{
			std::istrstream iStrm(file.GetRawData(), file.BytesLeft());

			if(version >= 0x88D)
			{
				srlztn::Ssb ssb(iStrm);
				ssb.BeginRead("mptm", MptVersion::num);
				ssb.ReadItem(GetTuneSpecificTunings(), "0", 1, &ReadTuningCollection);
				ssb.ReadItem(*this, "1", 1, &ReadTuningMap);
				ssb.ReadItem(Order, "2", 1, &ReadModSequenceOld);
				ssb.ReadItem(Patterns, FileIdPatterns, strlen(FileIdPatterns), &ReadModPatterns);
				ssb.ReadItem(Order, FileIdSequences, strlen(FileIdSequences), &ReadModSequences);

				if(ssb.m_Status & srlztn::SNT_FAILURE)
				{
					AddToLog("Unknown error occured while deserializing file.");
				}
			} else //Loading for older files.
			{
				if(GetTuneSpecificTunings().Deserialize(iStrm))
				{
					AddToLog("Error occured - loading failed while trying to load tune specific tunings.");
				} else
				{
					ReadTuningMap(iStrm, *this);
				}
			}
		} //version condition(MPT)
	}

	return true;
}

#ifndef MODPLUG_NO_FILESAVE

// Save edit history. Pass a null pointer for *f to retrieve the number of bytes that would be written.
DWORD SaveITEditHistory(const CSoundFile *pSndFile, FILE *f)
//----------------------------------------------------------
{
#ifdef MODPLUG_TRACKER
	CModDoc *pModDoc = pSndFile->GetpModDoc();
	const size_t num = (pModDoc != nullptr) ? pModDoc->GetFileHistory().size() + 1 : 0;	// + 1 for this session
#else
	const size_t num = 0;
#endif // MODPLUG_TRACKER

	uint16 fnum = (uint16)MIN(num, uint16_max);	// Number of entries that are actually going to be written
	const size_t bytes_written = 2 + fnum * 8;	// Number of bytes that are actually going to be written

	if(f == nullptr)
		return bytes_written;

	// Write number of history entries
	SwapBytesLE(fnum);
	fwrite(&fnum, 2, 1, f);

#ifdef MODPLUG_TRACKER
	// Write history data
	const size_t start = (num > uint16_max) ? num - uint16_max : 0;
	for(size_t n = start; n < num; n++)
	{
		FileHistory mptHistory;

		if(n < num - 1)
		{
			// Previous timestamps
			mptHistory = pModDoc->GetFileHistory().at(n);
		} else
		{
			// Current ("new") timestamp
			const time_t creationTime = pModDoc->GetCreationTime();

			MemsetZero(mptHistory.loadDate);
			//localtime_s(&loadDate, &creationTime);
			const tm* const p = localtime(&creationTime);
			if (p != nullptr)
				mptHistory.loadDate = *p;
			else
				pSndFile->AddToLog("localtime() returned nullptr.");

			mptHistory.openTime = (uint32)(difftime(time(nullptr), creationTime) * (double)HISTORY_TIMER_PRECISION);
		}

		ITHistoryStruct itHistory;
		itHistory.ConvertToIT(mptHistory);

		fwrite(&itHistory, 1, sizeof(itHistory), f);
	}
#endif // MODPLUG_TRACKER

	return bytes_written;
}


#ifdef MODPLUG_TRACKER
#include "../mptrack/Mptrack.h"	// For config filename
#endif // MODPLUG_TRACKER

bool CSoundFile::SaveIT(LPCSTR lpszFileName, bool compatibilityExport)
//--------------------------------------------------------------------
{
	const CModSpecifications &specs = (GetType() == MOD_TYPE_MPT ? ModSpecs::mptm : (compatibilityExport ? ModSpecs::it : ModSpecs::itEx));

	DWORD dwChnNamLen;
	ITFileHeader itHeader;
	DWORD dwPos = 0, dwHdrPos = 0, dwExtra = 0;
	FILE *f;

	if ((!lpszFileName) || ((f = fopen(lpszFileName, "wb")) == NULL)) return false;

	// Writing Header
	MemsetZero(itHeader);
	dwChnNamLen = 0;
	itHeader.id = ITFileHeader::itMagic;
	StringFixer::WriteString<StringFixer::nullTerminated>(itHeader.songname, m_szNames[0]);

	itHeader.highlight_minor = (uint8)std::min(m_nDefaultRowsPerBeat, ROWINDEX(uint8_max));
	itHeader.highlight_major = (uint8)std::min(m_nDefaultRowsPerMeasure, ROWINDEX(uint8_max));

	if(GetType() == MOD_TYPE_MPT)
	{
		if(!Order.NeedsExtraDatafield()) itHeader.ordnum = Order.size();
		else itHeader.ordnum = MIN(Order.size(), MAX_ORDERS); //Writing MAX_ORDERS at max here, and if there's more, writing them elsewhere.

		//Crop unused orders from the end.
		while(itHeader.ordnum > 1 && Order[itHeader.ordnum - 1] == Order.GetInvalidPatIndex()) itHeader.ordnum--;
	} else
	{
		// An additional "---" pattern is appended so Impulse Tracker won't ignore the last order item.
		// Interestingly, this can exceed IT's 256 order limit. Also, IT will always save at least two orders.
		itHeader.ordnum = std::min(Order.GetLengthTailTrimmed(), specs.ordersMax) + 1;
		if(itHeader.ordnum < 2) itHeader.ordnum = 2;
	}

	itHeader.insnum = std::min(m_nInstruments, specs.instrumentsMax);
	itHeader.smpnum = std::min(m_nSamples, specs.samplesMax);
	itHeader.patnum = std::min(Patterns.GetNumPatterns(), specs.patternsMax);

	// Parapointers
	vector<uint32> patpos(itHeader.patnum, 0);
	vector<uint32> smppos(itHeader.smpnum, 0);
	vector<uint32> inspos(itHeader.insnum, 0);

	//VERSION
	if(GetType() == MOD_TYPE_MPT)
	{
		// MPTM
		itHeader.cwtv = verMptFileVer;	// Used in OMPT-hack versioning.
		itHeader.cmwt = 0x888;
	} else
	{
		// IT
		MptVersion::VersionNum vVersion = MptVersion::num;
		itHeader.cwtv = 0x5000 | (uint16)((vVersion >> 16) & 0x0FFF); // format: txyy (t = tracker ID, x = version major, yy = version minor), e.g. 0x5117 (OpenMPT = 5, 117 = v1.17)
		itHeader.cmwt = 0x0214;	// Common compatible tracker :)
		// Hack from schism tracker:
		for(INSTRUMENTINDEX nIns = 1; nIns <= GetNumInstruments(); nIns++)
		{
			if(Instruments[nIns] && Instruments[nIns]->PitchEnv.dwFlags[ENV_FILTER])
			{
				itHeader.cmwt = 0x0216;
				break;
			}
		}

		if(!compatibilityExport)
		{
			// This way, we indicate that the file will most likely contain OpenMPT hacks. Compatibility export puts 0 here.
			itHeader.reserved = ITFileHeader::omptMagic;
		}
	}

	itHeader.flags = ITFileHeader::useStereoPlayback | ITFileHeader::useMIDIPitchController;
	itHeader.special = ITFileHeader::embedEditHistory | ITFileHeader::embedPatternHighlights;
	if(m_nInstruments) itHeader.flags |= ITFileHeader::instrumentMode;
	if(m_SongFlags[SONG_LINEARSLIDES]) itHeader.flags |= ITFileHeader::linearSlides;
	if(m_SongFlags[SONG_ITOLDEFFECTS]) itHeader.flags |= ITFileHeader::itOldEffects;
	if(m_SongFlags[SONG_ITCOMPATGXX]) itHeader.flags |= ITFileHeader::itCompatGxx;
	if(m_SongFlags[SONG_EXFILTERRANGE] && !compatibilityExport) itHeader.flags |= ITFileHeader::extendedFilterRange;

	itHeader.globalvol = (uint8)(m_nDefaultGlobalVolume >> 1);
	itHeader.mv = (uint8)MIN(m_nSamplePreAmp, 128);
	itHeader.speed = (uint8)MIN(m_nDefaultSpeed, 255);
 	itHeader.tempo = (uint8)MIN(m_nDefaultTempo, 255);  //Limit this one to 255, we save the real one as an extension below.
	itHeader.sep = 128; // pan separation
	// IT doesn't have a per-instrument Pitch Wheel Depth setting, so we just store the first non-zero PWD setting in the header.
	for(INSTRUMENTINDEX ins = 1; ins < GetNumInstruments(); ins++)
	{
		if(Instruments[ins] != nullptr && Instruments[ins]->midiPWD != 0)
		{
			itHeader.pwd = (uint8)abs(Instruments[ins]->midiPWD);
			break;
		}
	}

	dwHdrPos = sizeof(itHeader) + itHeader.ordnum;
	// Channel Pan and Volume
	memset(itHeader.chnpan, 0xA0, 64);
	memset(itHeader.chnvol, 64, 64);

	for (CHANNELINDEX ich = 0; ich < MIN(m_nChannels, 64); ich++) // Header only has room for settings for 64 chans...
	{
		itHeader.chnpan[ich] = (uint8)(ChnSettings[ich].nPan >> 2);
		if (ChnSettings[ich].dwFlags[CHN_SURROUND]) itHeader.chnpan[ich] = 100;
		itHeader.chnvol[ich] = (uint8)(ChnSettings[ich].nVolume);
		if (ChnSettings[ich].dwFlags[CHN_MUTE]) itHeader.chnpan[ich] |= 0x80;
	}

	// Channel names
	if(!compatibilityExport)
	{
		for (UINT ich=0; ich<m_nChannels; ich++)
		{
			if (ChnSettings[ich].szName[0])
			{
				dwChnNamLen = (ich + 1) * MAX_CHANNELNAME;
			}
		}
		if (dwChnNamLen) dwExtra += dwChnNamLen + 8;
	}

	if(m_SongFlags[SONG_EMBEDMIDICFG])
	{
		itHeader.flags |= ITFileHeader::reqEmbeddedMIDIConfig;
		itHeader.special |= ITFileHeader::embedMIDIConfiguration;
		dwExtra += sizeof(MIDIMacroConfigData);
	}

	// Pattern Names
	const PATTERNINDEX numNamedPats = compatibilityExport ? 0 : Patterns.GetNumNamedPatterns();
	if(numNamedPats > 0)
	{
		dwExtra += (numNamedPats * MAX_PATTERNNAME) + 8;
	}

	// Mix Plugins. Just calculate the size of this extra block for now.
	if(!compatibilityExport)
	{
		dwExtra += SaveMixPlugins(NULL, TRUE);
	}

	// Edit History. Just calculate the size of this extra block for now.
	dwExtra += SaveITEditHistory(this, nullptr);

	// Comments
	uint16 msglength = 0;
	if(!songMessage.empty())
	{
		itHeader.special |= ITFileHeader::embedSongMessage;
		itHeader.msglength = msglength = (uint16)MIN(songMessage.length() + 1, uint16_max);
		itHeader.msgoffset = dwHdrPos + dwExtra + (itHeader.insnum + itHeader.smpnum + itHeader.patnum) * 4;
	}

	// Write file header
	itHeader.ConvertEndianness();
	fwrite(&itHeader, 1, sizeof(itHeader), f);
	// Convert endianness again as we access some of the header variables with native endianness here.
	itHeader.ConvertEndianness();

	Order.WriteAsByte(f, itHeader.ordnum);
	if(itHeader.insnum) fwrite(&inspos[0], 4, itHeader.insnum, f);
	if(itHeader.smpnum) fwrite(&smppos[0], 4, itHeader.smpnum, f);
	if(itHeader.patnum) fwrite(&patpos[0], 4, itHeader.patnum, f);

	// Writing edit history information
	SaveITEditHistory(this, f);

	// Writing midi cfg
	if(itHeader.flags & ITFileHeader::reqEmbeddedMIDIConfig)
	{
		fwrite(static_cast<MIDIMacroConfigData*>(&m_MidiCfg), 1, sizeof(MIDIMacroConfigData), f);
	}

	// Writing pattern names
	if(numNamedPats)
	{
		char magic[4];
		memcpy(magic, "PNAM", 4);
		fwrite(magic, 4, 1, f);
		uint32 d = numNamedPats * MAX_PATTERNNAME;
		fwrite(&d, 4, 1, f);

		for(PATTERNINDEX nPat = 0; nPat < numNamedPats; nPat++)
		{
			char name[MAX_PATTERNNAME];
			MemsetZero(name);
			Patterns[nPat].GetName(name);
			fwrite(name, 1, MAX_PATTERNNAME, f);
		}
	}

	// Writing channel names
	if(dwChnNamLen && !compatibilityExport)
	{
		char magic[4];
		memcpy(magic, "CNAM", 4);
		fwrite(magic, 4, 1, f);
		fwrite(&dwChnNamLen, 1, 4, f);
		UINT nChnNames = dwChnNamLen / MAX_CHANNELNAME;
		for(UINT inam = 0; inam < nChnNames; inam++)
		{
			fwrite(ChnSettings[inam].szName, 1, MAX_CHANNELNAME, f);
		}
	}

	// Writing mix plugins info
	if(!compatibilityExport)
	{
		SaveMixPlugins(f, FALSE);
	}

	// Writing song message
	dwPos = dwHdrPos + dwExtra + (itHeader.insnum + itHeader.smpnum + itHeader.patnum) * 4;
	if(itHeader.special & ITFileHeader::embedSongMessage)
	{
		dwPos += msglength;
		fwrite(songMessage.c_str(), 1, msglength, f);
	}

	// Writing instruments
	for(UINT nins = 1; nins <= itHeader.insnum; nins++)
	{
		ITInstrumentEx iti;
		size_t instSize;

		if(Instruments[nins])
		{
			instSize = iti.ConvertToIT(*Instruments[nins], compatibilityExport, *this);
		} else
		{
			// Save Empty Instrument
			ModInstrument dummy;
			instSize = iti.ConvertToIT(dummy, compatibilityExport, *this);
		}

		// Writing instrument
		inspos[nins - 1] = dwPos;
		dwPos += instSize;
		iti.ConvertEndianness();
		fwrite(&iti, 1, instSize, f);

		//------------ rewbs.modularInstData
		if (Instruments[nins] && !compatibilityExport)
		{
			dwPos += SaveModularInstrumentData(f, Instruments[nins]);
		}
		//------------ end rewbs.modularInstData
	}

	// Writing sample headers
	ITSample itss;
	MemsetZero(itss);
	for(UINT hsmp=0; hsmp<itHeader.smpnum; hsmp++)
	{
		smppos[hsmp] = dwPos;
		dwPos += sizeof(ITSample);
		fwrite(&itss, 1, sizeof(ITSample), f);
	}

	// Writing Patterns
	bool bNeedsMptPatSave = false;
	for(PATTERNINDEX pat = 0; pat < itHeader.patnum; pat++)
	{
		uint32 dwPatPos = dwPos;
		if (!Patterns[pat]) continue;

		if(Patterns[pat].GetOverrideSignature())
			bNeedsMptPatSave = true;

		// Check for empty pattern
		if(Patterns[pat].GetNumRows() == 64 && Patterns.IsPatternEmpty(pat))
		{
			patpos[pat] = 0;
			continue;
		}

		patpos[pat] = dwPos;

		// Write pattern header
		ROWINDEX writeRows = std::min(Patterns[pat].GetNumRows(), ROWINDEX(uint16_max));
		uint16 patinfo[4];
		patinfo[0] = 0;
		patinfo[1] = (uint16)writeRows;
		patinfo[2] = 0;
		patinfo[3] = 0;
		SwapBytesLE(patinfo[1]);

		fwrite(patinfo, 8, 1, f);
		dwPos += 8;

		const CHANNELINDEX maxChannels = MIN(specs.channelsMax, GetNumChannels());
		vector<BYTE> chnmask(maxChannels, 0xFF);
		vector<ModCommand> lastvalue(maxChannels, ModCommand::Empty());

		for(ROWINDEX row = 0; row < writeRows; row++)
		{
			uint32 len = 0;
			uint8 buf[8 * MAX_BASECHANNELS];
			const ModCommand *m = Patterns[pat].GetRow(row);

			for(CHANNELINDEX ch = 0; ch < maxChannels; ch++, m++)
			{
				// Skip mptm-specific notes.
				if(m->IsPcNote())
				{
					bNeedsMptPatSave = true;
					continue;
				}

				uint8 b = 0;
				uint8 command = m->command;
				uint8 param = m->param;
				uint8 vol = 0xFF;
				uint8 note = m->note;
				if (note != NOTE_NONE) b |= 1;
				if (m->IsNote()) note--;
				if (note == NOTE_FADE && GetType() != MOD_TYPE_MPT) note = 0xF6;
				if (m->instr) b |= 2;
				if (m->volcmd)
				{
					uint8 volcmd = m->volcmd;
					switch(volcmd)
					{
					case VOLCMD_VOLUME:			vol = m->vol; if (vol > 64) vol = 64; break;
					case VOLCMD_PANNING:		vol = m->vol + 128; if (vol > 192) vol = 192; break;
					case VOLCMD_VOLSLIDEUP:		vol = 85 + ConvertVolParam(m); break;
					case VOLCMD_VOLSLIDEDOWN:	vol = 95 + ConvertVolParam(m); break;
					case VOLCMD_FINEVOLUP:		vol = 65 + ConvertVolParam(m); break;
					case VOLCMD_FINEVOLDOWN:	vol = 75 + ConvertVolParam(m); break;
					case VOLCMD_VIBRATODEPTH:	vol = 203 + ConvertVolParam(m); break;
					case VOLCMD_VIBRATOSPEED:	if(command == CMD_NONE)
												{
													// illegal command -> move if possible
													command = CMD_VIBRATO;
													param = ConvertVolParam(m) << 4;
												} else
												{
													vol = 203;
												}
												break;
					case VOLCMD_TONEPORTAMENTO:	vol = 193 + ConvertVolParam(m); break;
					case VOLCMD_PORTADOWN:		vol = 105 + ConvertVolParam(m); break;
					case VOLCMD_PORTAUP:		vol = 115 + ConvertVolParam(m); break;
					case VOLCMD_OFFSET:			if(!compatibilityExport) vol = 223 + ConvertVolParam(m); //rewbs.volOff
												break;
					default:					vol = 0xFF;
					}
				}
				if (vol != 0xFF) b |= 4;
				if (command)
				{
					S3MSaveConvert(command, param, true, compatibilityExport);
					if (command) b |= 8;
				}
				// Packing information
				if (b)
				{
					// Same note ?
					if (b & 1)
					{
						if ((note == lastvalue[ch].note) && (lastvalue[ch].volcmd & 1))
						{
							b &= ~1;
							b |= 0x10;
						} else
						{
							lastvalue[ch].note = note;
							lastvalue[ch].volcmd |= 1;
						}
					}
					// Same instrument ?
					if (b & 2)
					{
						if ((m->instr == lastvalue[ch].instr) && (lastvalue[ch].volcmd & 2))
						{
							b &= ~2;
							b |= 0x20;
						} else
						{
							lastvalue[ch].instr = m->instr;
							lastvalue[ch].volcmd |= 2;
						}
					}
					// Same volume column byte ?
					if (b & 4)
					{
						if ((vol == lastvalue[ch].vol) && (lastvalue[ch].volcmd & 4))
						{
							b &= ~4;
							b |= 0x40;
						} else
						{
							lastvalue[ch].vol = vol;
							lastvalue[ch].volcmd |= 4;
						}
					}
					// Same command / param ?
					if (b & 8)
					{
						if ((command == lastvalue[ch].command) && (param == lastvalue[ch].param) && (lastvalue[ch].volcmd & 8))
						{
							b &= ~8;
							b |= 0x80;
						} else
						{
							lastvalue[ch].command = command;
							lastvalue[ch].param = param;
							lastvalue[ch].volcmd |= 8;
						}
					}
					if (b != chnmask[ch])
					{
						chnmask[ch] = b;
						buf[len++] = uint8((ch + 1) | 0x80);
						buf[len++] = b;
					} else
					{
						buf[len++] = uint8(ch + 1);
					}
					if (b & 1) buf[len++] = note;
					if (b & 2) buf[len++] = m->instr;
					if (b & 4) buf[len++] = vol;
					if (b & 8)
					{
						buf[len++] = command;
						buf[len++] = param;
					}
				}
			}
			buf[len++] = 0;
			if(patinfo[0] > uint16_max - len)
			{
				mpt::String str;
				str.Format("%s (%s %u)", str_tooMuchPatternData, str_pattern, pat);
				AddToLog(str);
				break;
			} else
			{
				dwPos += len;
				patinfo[0] += (uint16)len;
				fwrite(buf, 1, len, f);
			}
		}
		fseek(f, dwPatPos, SEEK_SET);
		SwapBytesLE(patinfo[0]);
		fwrite(patinfo, 8, 1, f);
		fseek(f, dwPos, SEEK_SET);
	}
	// Writing Sample Data
	for (UINT nsmp=1; nsmp<=itHeader.smpnum; nsmp++)
	{
#ifdef MODPLUG_TRACKER
		int type = GetType() == MOD_TYPE_IT ? 1 : 4;
		if(compatibilityExport) type = 2;
		bool compress = (::GetPrivateProfileInt("Misc", Samples[nsmp].GetNumChannels() > 1 ? "ITCompressionStereo" : "ITCompressionMono", 0, theApp.GetConfigFileName()) & type) != 0;
#else
		bool compress = false;
#endif // MODPLUG_TRACKER
		// Old MPT will only consider the IT2.15 compression flag if the header version also indicates IT2.15.
		itss.ConvertToIT(Samples[nsmp], GetType(), compress, itHeader.cmwt >= 0x215);

		StringFixer::WriteString<StringFixer::nullTerminated>(itss.name, m_szNames[nsmp]);

		itss.samplepointer = dwPos;
		itss.ConvertEndianness();
		fseek(f, smppos[nsmp - 1], SEEK_SET);
		fwrite(&itss, 1, sizeof(ITSample), f);
		fseek(f, dwPos, SEEK_SET);
		if ((Samples[nsmp].pSample) && (Samples[nsmp].nLength))
		{
			dwPos += itss.GetSampleFormat().WriteSample(f, Samples[nsmp]);
		}
	}

	//Save hacked-on extra info
	if(!compatibilityExport)
	{
		SaveExtendedInstrumentProperties(itHeader.insnum, f);
		SaveExtendedSongProperties(f);
	}

	// Updating offsets
	fseek(f, dwHdrPos, SEEK_SET);
	if(itHeader.insnum) fwrite(&inspos[0], 4, itHeader.insnum, f);
	if(itHeader.smpnum) fwrite(&smppos[0], 4, itHeader.smpnum, f);
	if(itHeader.patnum) fwrite(&patpos[0], 4, itHeader.patnum, f);

	if(GetType() == MOD_TYPE_IT)
	{
		fclose(f);
		return true;
	}

	//hack
	//BEGIN: MPT SPECIFIC:
	//--------------------

	fseek(f, 0, SEEK_END);
	std::ofstream fout(f);
	const uint32 MPTStartPos = (uint32)fout.tellp();

	srlztn::Ssb ssb(fout);
	ssb.BeginWrite("mptm", MptVersion::num);

	if(GetTuneSpecificTunings().GetNumTunings() > 0)
		ssb.WriteItem(GetTuneSpecificTunings(), "0", 1, &WriteTuningCollection);
	if(AreNonDefaultTuningsUsed(*this))
		ssb.WriteItem(*this, "1", 1, &WriteTuningMap);
	if(Order.NeedsExtraDatafield())
		ssb.WriteItem(Order, "2", 1, &WriteModSequenceOld);
	if(bNeedsMptPatSave)
		ssb.WriteItem(Patterns, FileIdPatterns, strlen(FileIdPatterns), &WriteModPatterns);
	ssb.WriteItem(Order, FileIdSequences, strlen(FileIdSequences), &WriteModSequences);

	ssb.FinishWrite();

	if(ssb.m_Status & srlztn::SNT_FAILURE)
	{
		AddToLog("Error occured in writing MPTM extensions.");
	}

	//Last 4 bytes should tell where the hack mpt things begin.
	if(!fout.good())
	{
		fout.clear();
		fout.write(reinterpret_cast<const char*>(&MPTStartPos), sizeof(MPTStartPos));
		return false;
	}
	fout.write(reinterpret_cast<const char*>(&MPTStartPos), sizeof(MPTStartPos));
	fout.close();
	//END  : MPT SPECIFIC
	//-------------------

	//NO WRITING HERE ANYMORE.

	return true;
}


#endif // MODPLUG_NO_FILESAVE


#ifndef MODPLUG_NO_FILESAVE

UINT CSoundFile::SaveMixPlugins(FILE *f, BOOL bUpdate)
//----------------------------------------------------
{
	uint32 chinfo[MAX_BASECHANNELS];
	char id[4];
	uint32 nPluginSize;
	uint32 nTotalSize = 0;
	uint32 nChInfo = 0;

	for(PLUGINDEX i = 0; i < MAX_MIXPLUGINS; i++)
	{
		const SNDMIXPLUGIN &plugin = m_MixPlugins[i];
		if(plugin.IsValidPlugin())
		{
			nPluginSize = sizeof(SNDMIXPLUGININFO) + 4; // plugininfo+4 (datalen)
			if((plugin.pMixPlugin) && (bUpdate))
			{
				plugin.pMixPlugin->SaveAllParameters();
			}
			if(plugin.pPluginData)
			{
				nPluginSize += plugin.nPluginDataSize;
			}

			// rewbs.modularPlugData
			DWORD MPTxPlugDataSize = 4 + (sizeof(m_MixPlugins[i].fDryRatio)) +     //4 for ID and size of dryRatio
									 4 + (sizeof(m_MixPlugins[i].defaultProgram)); //rewbs.plugDefaultProgram
			 					// for each extra entity, add 4 for ID, plus size of entity, plus optionally 4 for size of entity.

			nPluginSize += MPTxPlugDataSize + 4; //+4 is for size itself: sizeof(DWORD) is 4
			// rewbs.modularPlugData
			if(f)
			{
				// write plugin ID
				id[0] = 'F';
				id[1] = 'X';
				id[2] = '0' + (i / 10);
				id[3] = '0' + (i % 10);
				fwrite(id, 1, 4, f);

				// write plugin size:
				fwrite(&nPluginSize, 1, 4, f);
				fwrite(&plugin.Info, 1, sizeof(SNDMIXPLUGININFO), f);
				fwrite(&m_MixPlugins[i].nPluginDataSize, 1, 4, f);
				if(m_MixPlugins[i].pPluginData)
				{
					fwrite(m_MixPlugins[i].pPluginData, 1, m_MixPlugins[i].nPluginDataSize, f);
				}

				fwrite(&MPTxPlugDataSize, 1, 4, f);

				//write ID for this xPlugData chunk:
				memcpy(id, "DWRT", 4);
				fwrite(id, 1, 4, f);
				//Write chunk data itself (Could include size if you want variable size. Not necessary here.)
				fwrite(&(m_MixPlugins[i].fDryRatio), 1, sizeof(float), f);

				memcpy(id, "PROG", 4);
				fwrite(id, 1, 4, f);
				//Write chunk data itself (Could include size if you want variable size. Not necessary here.)
				fwrite(&(m_MixPlugins[i].defaultProgram), 1, sizeof(long), f);

			}
			nTotalSize += nPluginSize + 8;
		}
	}
	for(CHANNELINDEX j = 0; j < m_nChannels; j++)
	{
		if(j < MAX_BASECHANNELS)
		{
			if((chinfo[j] = ChnSettings[j].nMixPlugin) != 0)
			{
				nChInfo = j + 1;
			}
		}
	}
	if(nChInfo)
	{
		if(f)
		{
			memcpy(id, "CHFX", 4);
			fwrite(id, 1, 4, f);
			nPluginSize = nChInfo * 4;
			fwrite(&nPluginSize, 1, 4, f);
			fwrite(chinfo, 1, nPluginSize, f);
		}
		nTotalSize += nChInfo * 4 + 8;
	}
	return nTotalSize;
}

#endif // MODPLUG_NO_FILESAVE


void CSoundFile::LoadMixPlugins(FileReader &file)
//-----------------------------------------------
{
	while(file.BytesLeft() > 8)
	{
		char code[4];
		file.ReadArray(code);
		const uint32 chunkSize = file.ReadUint32LE();
		if(!file.CanRead(chunkSize))
		{
			file.SkipBack(8);
			return;
		}
		FileReader chunk = file.GetChunk(chunkSize);

		// Channel FX
		if(!memcmp(code, "CHFX", 4))
		{
			for (size_t ch = 0; ch < MAX_BASECHANNELS; ch++)
			{
				ChnSettings[ch].nMixPlugin = (uint8)chunk.ReadUint32LE();
			}
		}
		// Plugin Data
		else if(memcmp(code, "FX00", 4) >= 0 && memcmp(code, "FX99", 4) <= 0)
		{
			PLUGINDEX plug = (code[2] - '0') * 10 + (code[3] - '0');	//calculate plug-in number.

			if(plug < MAX_MIXPLUGINS)
			{
				// MPT's standard plugin data. Size not specified in file.. grrr..
				chunk.ReadConvertEndianness(m_MixPlugins[plug].Info);
				StringFixer::SetNullTerminator(m_MixPlugins[plug].Info.szName);
				StringFixer::SetNullTerminator(m_MixPlugins[plug].Info.szLibraryName);

				//data for VST setchunk? size lies just after standard plugin data.
				FileReader pluginDataChunk = chunk.GetChunk(chunk.ReadUint32LE());

				if(pluginDataChunk.IsValid())
				{
					m_MixPlugins[plug].nPluginDataSize = 0;
					m_MixPlugins[plug].pPluginData = new char [pluginDataChunk.BytesLeft()];
					if(m_MixPlugins[plug].pPluginData)
					{
						m_MixPlugins[plug].nPluginDataSize = pluginDataChunk.BytesLeft();
						memcpy(m_MixPlugins[plug].pPluginData, pluginDataChunk.GetRawData(), pluginDataChunk.BytesLeft());
					}
				}

				//rewbs.modularPlugData
				FileReader modularData = chunk.GetChunk(chunk.ReadUint32LE());

				//if dwMPTExtra is positive and there are dwMPTExtra bytes left in nPluginSize, we have some more data!
				if(modularData.IsValid())
				{
					while(modularData.BytesLeft() > 4)
					{
						// do we recognize this chunk?
						modularData.ReadArray(code);
						//rewbs.dryRatio
						//TODO: turn this into a switch statement like for modular instrument data
						if(!memcmp(code, "DWRT", 4))
						{
							m_MixPlugins[plug].fDryRatio = modularData.ReadFloatLE();
						}
						//end rewbs.dryRatio
						//rewbs.plugDefaultProgram
						else if(!memcmp(code, "PROG", 4))
						{
							m_MixPlugins[plug].defaultProgram = modularData.ReadUint32LE();
						}
						//end rewbs.plugDefaultProgram
						//else if.. (add extra attempts to recognize chunks here)
						else // otherwise move forward a byte.
						{
							// Why on earth would you use a modular chunk structure, but not store the size of chunks in the file?!
							modularData.Skip(1);
						}

					}

				}
				//end rewbs.modularPlugData
			}
		} else if(!memcmp(code, "XTPM", 4))
		{
			// Read too far, chicken out...
			file.SkipBack(8);
			return;
		}
	}
}


#ifndef MODPLUG_NO_FILESAVE

// Used only when saving IT, XM and MPTM.
// ITI, ITP saves using Ericus' macros etc...
// The reason is that ITs and XMs save [code][size][ins1.Value][ins2.Value]...
// whereas ITP saves [code][size][ins1.Value][code][size][ins2.Value]...
// too late to turn back....
void CSoundFile::SaveExtendedInstrumentProperties(UINT nInstruments, FILE* f) const
//---------------------------------------------------------------------------------
{
	__int32 code=0;

/*	if(Instruments[1] == NULL) {
		return;
	}*/

	code = 'MPTX';							// write extension header code
	fwrite(&code, 1, sizeof(__int32), f);

	if (nInstruments == 0)
		return;

	WriteInstrumentPropertyForAllInstruments('VR..', sizeof(ModInstrument().nVolRampUp),  f, nInstruments);
	WriteInstrumentPropertyForAllInstruments('MiP.', sizeof(ModInstrument().nMixPlug),    f, nInstruments);
	WriteInstrumentPropertyForAllInstruments('MC..', sizeof(ModInstrument().nMidiChannel),f, nInstruments);
	WriteInstrumentPropertyForAllInstruments('MP..', sizeof(ModInstrument().nMidiProgram),f, nInstruments);
	WriteInstrumentPropertyForAllInstruments('MB..', sizeof(ModInstrument().wMidiBank),   f, nInstruments);
	WriteInstrumentPropertyForAllInstruments('P...', sizeof(ModInstrument().nPan),        f, nInstruments);
	WriteInstrumentPropertyForAllInstruments('GV..', sizeof(ModInstrument().nGlobalVol),  f, nInstruments);
	WriteInstrumentPropertyForAllInstruments('FO..', sizeof(ModInstrument().nFadeOut),    f, nInstruments);
	WriteInstrumentPropertyForAllInstruments('R...', sizeof(ModInstrument().nResampling), f, nInstruments);
	WriteInstrumentPropertyForAllInstruments('CS..', sizeof(ModInstrument().nCutSwing),   f, nInstruments);
	WriteInstrumentPropertyForAllInstruments('RS..', sizeof(ModInstrument().nResSwing),   f, nInstruments);
	WriteInstrumentPropertyForAllInstruments('FM..', sizeof(ModInstrument().nFilterMode), f, nInstruments);
	WriteInstrumentPropertyForAllInstruments('PERN', sizeof(ModInstrument().PitchEnv.nReleaseNode ), f, nInstruments);
	WriteInstrumentPropertyForAllInstruments('AERN', sizeof(ModInstrument().PanEnv.nReleaseNode), f, nInstruments);
	WriteInstrumentPropertyForAllInstruments('VERN', sizeof(ModInstrument().VolEnv.nReleaseNode), f, nInstruments);
	WriteInstrumentPropertyForAllInstruments('PTTL', sizeof(ModInstrument().wPitchToTempoLock),  f, nInstruments);
	WriteInstrumentPropertyForAllInstruments('PVEH', sizeof(ModInstrument().nPluginVelocityHandling),  f, nInstruments);
	WriteInstrumentPropertyForAllInstruments('PVOH', sizeof(ModInstrument().nPluginVolumeHandling),  f, nInstruments);

	if(!(GetType() & MOD_TYPE_XM))
	{
		// XM instrument headers already have support for this
		WriteInstrumentPropertyForAllInstruments('MPWD', sizeof(ModInstrument().midiPWD), f, nInstruments);
	}

	if(GetType() & MOD_TYPE_MPT)
	{
		UINT maxNodes = 0;
		for(INSTRUMENTINDEX nIns = 1; nIns <= m_nInstruments; nIns++) if(Instruments[nIns] != nullptr)
		{
			maxNodes = MAX(maxNodes, Instruments[nIns]->VolEnv.nNodes);
			maxNodes = MAX(maxNodes, Instruments[nIns]->PanEnv.nNodes);
			maxNodes = MAX(maxNodes, Instruments[nIns]->PitchEnv.nNodes);
		}
		// write full envelope information for MPTM files (more env points)
		if(maxNodes > 25)
		{
			WriteInstrumentPropertyForAllInstruments('VE..', sizeof(ModInstrument().VolEnv.nNodes), f, nInstruments);
			WriteInstrumentPropertyForAllInstruments('VP[.', sizeof(ModInstrument().VolEnv.Ticks),  f, nInstruments);
			WriteInstrumentPropertyForAllInstruments('VE[.', sizeof(ModInstrument().VolEnv.Values), f, nInstruments);

			WriteInstrumentPropertyForAllInstruments('PE..', sizeof(ModInstrument().PanEnv.nNodes), f, nInstruments);
			WriteInstrumentPropertyForAllInstruments('PP[.', sizeof(ModInstrument().PanEnv.Ticks),  f, nInstruments);
			WriteInstrumentPropertyForAllInstruments('PE[.', sizeof(ModInstrument().PanEnv.Values), f, nInstruments);

			WriteInstrumentPropertyForAllInstruments('PiE.', sizeof(ModInstrument().PitchEnv.nNodes), f, nInstruments);
			WriteInstrumentPropertyForAllInstruments('PiP[', sizeof(ModInstrument().PitchEnv.Ticks),  f, nInstruments);
			WriteInstrumentPropertyForAllInstruments('PiE[', sizeof(ModInstrument().PitchEnv.Values), f, nInstruments);
		}
	}

	return;
}

void CSoundFile::WriteInstrumentPropertyForAllInstruments(__int32 code, __int16 size, FILE* f, UINT nInstruments) const
//---------------------------------------------------------------------------------------------------------------------
{
	fwrite(&code, 1, sizeof(__int32), f);		//write code
	fwrite(&size, 1, sizeof(__int16), f);		//write size
	for(UINT nins=1; nins<=nInstruments; nins++)	//for all instruments...
	{
		char *pField;
		if (Instruments[nins])
		{
			pField = GetInstrumentHeaderFieldPointer(Instruments[nins], code, size); //get ptr to field
		} else
		{
			ModInstrument *emptyInstrument = new ModInstrument();
			pField = GetInstrumentHeaderFieldPointer(emptyInstrument, code, size); //get ptr to field
			delete emptyInstrument;
		}
		fwrite(pField, 1, size, f);				//write field data
	}
}

void CSoundFile::SaveExtendedSongProperties(FILE* f) const
//--------------------------------------------------------
{
	//Extra song data - Yet Another Hack.
	__int16 size;
	__int32 code = 'MPTS';					//Extra song file data
	fwrite(&code, 1, sizeof(__int32), f);

	code = 'DT..';							//write m_nDefaultTempo field code
	fwrite(&code, 1, sizeof(__int32), f);
	size = sizeof(m_nDefaultTempo);			//write m_nDefaultTempo field size
	fwrite(&size, 1, sizeof(__int16), f);
	fwrite(&m_nDefaultTempo, 1, size, f);	//write m_nDefaultTempo

	code = 'RPB.';							//write m_nRowsPerBeat
	fwrite(&code, 1, sizeof(__int32), f);
	size = sizeof(m_nDefaultRowsPerBeat);
	fwrite(&size, 1, sizeof(__int16), f);
	fwrite(&m_nDefaultRowsPerBeat, 1, size, f);

	code = 'RPM.';							//write m_nRowsPerMeasure
	fwrite(&code, 1, sizeof(__int32), f);
	size = sizeof(m_nDefaultRowsPerMeasure);
	fwrite(&size, 1, sizeof(__int16), f);
	fwrite(&m_nDefaultRowsPerMeasure, 1, size, f);

	code = 'C...';							//write m_nChannels
	fwrite(&code, 1, sizeof(__int32), f);
	size = sizeof(m_nChannels);
	fwrite(&size, 1, sizeof(__int16), f);
	fwrite(&m_nChannels, 1, size, f);

	if(TypeIsIT_MPT() && GetNumChannels() > 64)	//IT header has room only for 64 channels. Save the
	{											//settings that do not fit to the header here as an extension.
		code = 'ChnS';
		fwrite(&code, 1, sizeof(__int32), f);
		size = (GetNumChannels() - 64) * 2;
		fwrite(&size, 1, sizeof(__int16), f);
		for(CHANNELINDEX chn = 64; chn < GetNumChannels(); chn++)
		{
			uint8 panvol[2];
			panvol[0] = (uint8)(ChnSettings[chn].nPan >> 2);
			if (ChnSettings[chn].dwFlags[CHN_SURROUND]) panvol[0] = 100;
			if (ChnSettings[chn].dwFlags[CHN_MUTE]) panvol[0] |= 0x80;
			panvol[1] = (uint8)ChnSettings[chn].nVolume;
			fwrite(&panvol, sizeof(panvol), 1, f);
		}
	}

	code = 'TM..';							//write m_nTempoMode
	fwrite(&code, 1, sizeof(__int32), f);
	size = sizeof(m_nTempoMode);
	fwrite(&size, 1, sizeof(__int16), f);
	fwrite(&m_nTempoMode, 1, size, f);

	code = 'PMM.';							//write m_nMixLevels
	fwrite(&code, 1, sizeof(__int32), f);
	size = sizeof(m_nMixLevels);
	fwrite(&size, 1, sizeof(__int16), f);
	fwrite(&m_nMixLevels, 1, size, f);

	code = 'CWV.';							//write m_dwCreatedWithVersion
	fwrite(&code, 1, sizeof(__int32), f);
	size = sizeof(m_dwCreatedWithVersion);
	fwrite(&size, 1, sizeof(__int16), f);
	fwrite(&m_dwCreatedWithVersion, 1, size, f);

	code = 'LSWV';							//write m_dwLastSavedWithVersion
	fwrite(&code, 1, sizeof(__int32), f);
	size = sizeof(m_dwLastSavedWithVersion);
	fwrite(&size, 1, sizeof(__int16), f);
	fwrite(&m_dwLastSavedWithVersion, 1, size, f);

	code = 'SPA.';							//write m_nSamplePreAmp
	fwrite(&code, 1, sizeof(__int32), f);
	size = sizeof(m_nSamplePreAmp);
	fwrite(&size, 1, sizeof(__int16), f);
	fwrite(&m_nSamplePreAmp, 1, size, f);

	code = 'VSTV';							//write m_nVSTiVolume
	fwrite(&code, 1, sizeof(__int32), f);
	size = sizeof(m_nVSTiVolume);
	fwrite(&size, 1, sizeof(__int16), f);
	fwrite(&m_nVSTiVolume, 1, size, f);

	code = 'DGV.';							//write m_nDefaultGlobalVolume
	fwrite(&code, 1, sizeof(__int32), f);
	size = sizeof(m_nDefaultGlobalVolume);
	fwrite(&size, 1, sizeof(__int16), f);
	fwrite(&m_nDefaultGlobalVolume, 1, size, f);

	code = 'RP..';							//write m_nRestartPos
	fwrite(&code, 1, sizeof(__int32), f);
	size = sizeof(m_nRestartPos);
	fwrite(&size, 1, sizeof(__int16), f);
	fwrite(&m_nRestartPos, 1, size, f);


	//Additional flags for XM/IT/MPTM
	if(m_ModFlags)
	{
		code = 'MSF.';
		fwrite(&code, 1, sizeof(__int32), f);
		size = sizeof(m_ModFlags);
		fwrite(&size, 1, sizeof(__int16), f);
		fwrite(&m_ModFlags, 1, size, f);
	}

	//MIMA, MIDI mapping directives
	if(GetMIDIMapper().GetCount() > 0)
	{
		const size_t objectsize = GetMIDIMapper().GetSerializationSize();
		if(objectsize > size_t(int16_max))
		{
			AddToLog("Datafield overflow with MIDI to plugparam mappings; data won't be written.");
		}
		else
		{
			code = 'MIMA';
			fwrite(&code, 1, sizeof(__int32), f);
			size = static_cast<int16>(objectsize);
			fwrite(&size, 1, sizeof(__int16), f);
			GetMIDIMapper().Serialize(f);
		}
	}


	return;
}

#endif // MODPLUG_NO_FILESAVE


void CSoundFile::LoadExtendedInstrumentProperties(FileReader &file, bool *pInterpretMptMade)
//------------------------------------------------------------------------------------------
{
	if(!file.ReadMagic("XTPM"))	// 'MPTX'
	{
		return;
	}

	// Found MPTX, interpret the file MPT made.
	if(pInterpretMptMade != nullptr)
		*pInterpretMptMade = true;

	while(file.BytesLeft() >= 6) //Loop 'till beginning of end of file/mpt specific looking for inst. extensions
	{
		uint32 code = file.ReadUint32LE();

		if(code == 'MPTS')					//Reached song extensions, break out of this loop
		{
			file.SkipBack(4);
			return;
		}

		// Read size of this property for *one* instrument
		const uint16 size = file.ReadUint16LE();

		for(INSTRUMENTINDEX i = 1; i <= GetNumInstruments(); i++)
		{
			if(Instruments[i])
			{
				ReadInstrumentExtensionField(Instruments[i], code, size, file);
			}
		}
	}
}


void CSoundFile::LoadExtendedSongProperties(const MODTYPE modtype, FileReader &file, bool *pInterpretMptMade)
//-----------------------------------------------------------------------------------------------------------
{
	if(!file.ReadMagic("STPM"))	// 'MPTS'
	{
		return;
	}

	// Found MPTS, interpret the file MPT made.
	if(pInterpretMptMade != nullptr)
		*pInterpretMptMade = true;

	// HACK: Reset mod flags to default values here, as they are not always written.
	m_ModFlags.reset();

	// Case macros.
	#define CASE(id, data)	\
		case id: fadr = reinterpret_cast<char *>(&data); maxReadCount = std::min(size_t(size), sizeof(data)); break;
	#define CASE_NOTXM(id, data) \
		case id: if(modtype != MOD_TYPE_XM) { fadr = reinterpret_cast<char *>(&data); maxReadCount = std::min(size_t(size), sizeof(data));} break;

	while(file.BytesLeft() > 6)
	{
		const uint32 code = file.ReadUint32LE();
		const uint16 size = file.ReadUint16LE();

		if(!file.CanRead(size))
		{
			break;
		}

		size_t maxReadCount = 0;
		char *fadr = nullptr;
		FileReader chunk = file.GetChunk(size);

		switch (code)					// interpret field code
		{
			CASE('DT..', m_nDefaultTempo);
			CASE('RPB.', m_nDefaultRowsPerBeat);
			CASE('RPM.', m_nDefaultRowsPerMeasure);
			CASE_NOTXM('C...', m_nChannels);
			CASE('TM..', m_nTempoMode);
			CASE('PMM.', m_nMixLevels);
			CASE('CWV.', m_dwCreatedWithVersion);
			CASE('LSWV', m_dwLastSavedWithVersion);
			CASE('SPA.', m_nSamplePreAmp);
			CASE('VSTV', m_nVSTiVolume);
			CASE('DGV.', m_nDefaultGlobalVolume);
			CASE_NOTXM('RP..', m_nRestartPos);
			CASE('MSF.', m_ModFlags);
			case 'MIMA': GetMIDIMapper().Deserialize(chunk.GetRawData(), size); break;
			case 'ChnS':
				if(size <= (MAX_BASECHANNELS - 64) * 2 && (size % 2u) == 0)
				{
					STATIC_ASSERT(CountOf(ChnSettings) >= 64);
					const CHANNELINDEX loopLimit = std::min(uint16(size / 2), uint16(CountOf(ChnSettings) - 64));

					for(CHANNELINDEX i = 0; i < loopLimit; i++)
					{
						uint8 pan = chunk.ReadUint8(), vol = chunk.ReadUint8();
						if(pan != 0xFF)
						{
							ChnSettings[i + 64].nVolume = vol;
							ChnSettings[i + 64].nPan = 128;
							ChnSettings[i + 64].dwFlags.reset();
							if(pan & 0x80) ChnSettings[i + 64].dwFlags.set(CHN_MUTE);
							pan &= 0x7F;
							if(pan <= 64) ChnSettings[i + 64].nPan = pan << 2;
							if(pan == 100) ChnSettings[i + 64].dwFlags.set(CHN_SURROUND);
						}
					}
				}

			break;
		}

		// Read field data
		if(fadr != nullptr)
		{
			memcpy(fadr, chunk.GetRawData(), maxReadCount);
		}
	}

	// Validate read values.
	Limit(m_nDefaultTempo, GetModSpecifications().tempoMin, GetModSpecifications().tempoMax);
	//m_nRowsPerBeat
	//m_nRowsPerMeasure
	LimitMax(m_nChannels, GetModSpecifications().channelsMax);
	//m_nTempoMode
	//m_nMixLevels
	//m_dwCreatedWithVersion
	//m_dwLastSavedWithVersion
	//m_nSamplePreAmp
	//m_nVSTiVolume
	//m_nDefaultGlobalVolume);
	//m_nRestartPos
	//m_ModFlags


	#undef CASE
	#undef CASE_NOTXM
}


#ifndef MODPLUG_NO_FILESAVE

size_t CSoundFile::SaveModularInstrumentData(FILE *f, const ModInstrument *pIns) const
//------------------------------------------------------------------------------------
{
	// As the only stuff that is actually written here is the plugin ID,
	// we can actually chicken out if there's no plugin.
	if(!pIns->nMixPlug)
	{
		return 0;
	}

	uint32 modularInstSize = 0;
	uint32 id = 'INSM';
	SwapBytesLE(id);
	fwrite(&id, 1, sizeof(id), f);				// mark this as an instrument with modular extensions
	long sizePos = ftell(f);									// we will want to write the modular data's total size here
	fwrite(&modularInstSize, 1, sizeof(modularInstSize), f);	// write a DUMMY size, just to move file pointer by a long

	// Write chunks
	{	//VST Slot chunk:
		id = 'PLUG';
		SwapBytesLE(id);
		fwrite(&id, 1, sizeof(uint32), f);
		fwrite(&(pIns->nMixPlug), 1, sizeof(uint8), f);
		modularInstSize += sizeof(uint32) + sizeof(uint8);
	}
	//How to save your own modular instrument chunk:
/*	{
		ID='MYID';
		fwrite(&ID, 1, sizeof(int), f);
		instModularDataSize+=sizeof(int);

		//You can save your chunk size somwhere here if you need variable chunk size.
		fwrite(myData, 1, myDataSize, f);
		instModularDataSize+=myDataSize;
	}
*/
	//write modular data's total size
	long curPos = ftell(f);			// remember current pos
	fseek(f, sizePos, SEEK_SET);	// go back to  sizePos
	SwapBytesLE(modularInstSize);
	fwrite(&modularInstSize, 1, sizeof(modularInstSize), f);	// write data
	fseek(f, curPos, SEEK_SET);		// go back to where we were.

	// Compute the size that we just wasted.
	return sizeof(id) + sizeof(modularInstSize) + modularInstSize;
}

#endif // MODPLUG_NO_FILESAVE


size_t CSoundFile::LoadModularInstrumentData(FileReader &file, ModInstrument &ins)
//--------------------------------------------------------------------------------
{
	// find end of standard header

	//If the next piece of data is 'INSM' we have modular extensions to our instrument...
	if(!file.ReadMagic("MSNI"))
	{
		return 0;
	}
	//...the next piece of data must be the total size of the modular data
	FileReader modularData = file.GetChunk(file.ReadUint32LE());

	// Handle chunks
	while(modularData.BytesLeft())
	{
		const uint32 chunkID = modularData.ReadUint32LE();

		switch (chunkID)
		{
		case 'PLUG':
			// Chunks don't tell us their length - stupid!
			ins.nMixPlug = modularData.ReadUint8();
			break;

		default:
			// move forward one byte and try to recognize again.
			modularData.Skip(1);

		}
	}

	return 8 + modularData.GetLength();
}