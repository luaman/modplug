/*
 * TrackerSettings.cpp
 * -------------------
 * Purpose: Code for managing, loading and saving all applcation settings.
 * Notes  : (currently none)
 * Authors: Olivier Lapicque
 *          OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"
#include "Mptrack.h"
#include "Moddoc.h"
#include "Mainfrm.h"
#include "../sounddev/SoundDevice.h"
#include "../sounddev/SoundDeviceASIO.h"
#include "../common/version.h"
#include "UpdateCheck.h"
#include "Mpdlgs.h"
#include "AutoSaver.h"
#include "../common/StringFixer.h"
#include "TrackerSettings.h"
#include "../common/misc_util.h"
#include "PatternClipboard.h"
#include "../sounddev/SoundDevice.h"


#define OLD_SNDDEV_MINBUFFERLEN			1    // 1ms
#define OLD_SNDDEV_MAXBUFFERLEN			1000 // 1sec

#define OLD_SOUNDSETUP_REVERSESTEREO         0x20
#define OLD_SOUNDSETUP_SECONDARY             0x40
#define OLD_SOUNDSETUP_NOBOOSTTHREADPRIORITY 0x80


TrackerDirectories TrackerDirectories::directories;

const TCHAR *TrackerDirectories::m_szDirectoryToSettingsName[NUM_DIRS] = { _T("Songs_Directory"), _T("Samples_Directory"), _T("Instruments_Directory"), _T("Plugins_Directory"), _T("Plugin_Presets_Directory"), _T("Export_Directory"), _T(""), _T("") };


TrackerSettings &TrackerSettings::Instance()
//------------------------------------------
{
	return theApp.GetTrackerSettings();
}


static MptVersion::VersionNum GetStoredVersion(const std::string &iniVersion, uint32 regVersion = 0)
//--------------------------------------------------------------------------------------------------
{
	MptVersion::VersionNum result = regVersion;
	if(!iniVersion.empty())
	{
		result = std::max(result, MptVersion::ToNum(iniVersion));
	}
	return result;
}


TrackerSettings::TrackerSettings(SettingsContainer &conf)
//-------------------------------------------------------
	: conf(conf)
	, RegVersion(conf, "Settings", "Version", 0)
	, IniVersion(conf, "Version", "Version", "")
	, gcsPreviousVersion(GetStoredVersion(IniVersion, RegVersion))
	, gcsInstallGUID(conf, "Version", "InstallGUID", "")
	, m_ShowSplashScreen(conf, "Display", "ShowSplashScreen", true)
{

	// Fixups:
	if(gcsInstallGUID == "")
	{
		// No GUID found - generate one.
		GUID guid;
		CoCreateGuid(&guid);
		BYTE* Str;
		UuidToString((UUID*)&guid, &Str);
		gcsInstallGUID = Str;
		RpcStringFree(&Str);
	}
	
	// Last fixup: update config version
	IniVersion = MptVersion::str;
	conf.Remove("Settings", "Version");

	// Write updated settings
	conf.Flush();



	gnPatternSpacing = 0;
	gbPatternRecord = TRUE;
	gbPatternVUMeters = FALSE;
	gbPatternPluginNames = TRUE;
	gbMdiMaximize = TRUE;
	gbShowHackControls = false;
	//rewbs.varWindowSize
	glGeneralWindowHeight = 178;
	glPatternWindowHeight = 152;
	glSampleWindowHeight = 188;
	glInstrumentWindowHeight = 300;
	glCommentsWindowHeight = 288;
	glGraphWindowHeight = 288; //rewbs.graph
	//end rewbs.varWindowSize
	glTreeWindowWidth = 160;
	glTreeSplitRatio = 128;
	VuMeterUpdateInterval = 15;

	// Audio Setup
	gnAutoChordWaitTime = 60;
	gnMsgBoxVisiblityFlags = uint32_max;

	// Audio device
	m_MorePortaudio = false;
	m_nWaveDevice = SNDDEV_BUILD_ID(0, SNDDEV_WAVEOUT);	// Default value will be overridden
	m_LatencyMS = SNDDEV_DEFAULT_LATENCY_MS;
	m_UpdateIntervalMS = SNDDEV_DEFAULT_UPDATEINTERVAL_MS;
	m_SoundDeviceExclusiveMode = false;
	m_SoundDeviceBoostThreadPriority = true;
	m_SampleFormat = SampleFormatInt16;

#ifndef NO_EQ
	// Default EQ settings
	MemCopy(m_EqSettings, CEQSetupDlg::gEQPresets[0]);
#endif

	// MIDI Setup
	m_nMidiDevice = 0;
	m_dwMidiSetup = MIDISETUP_RECORDVELOCITY | MIDISETUP_RECORDNOTEOFF | MIDISETUP_TRANSPOSEKEYBOARD | MIDISETUP_MIDITOPLUG;
	aftertouchBehaviour = atDoNotRecord;
	midiVelocityAmp = 100;
	
	// MIDI Import
	midiImportPatternLen = 128;
	midiImportSpeed = 3;

	// Pattern Setup
	gbLoopSong = TRUE;
	m_dwPatternSetup = PATTERN_PLAYNEWNOTE | PATTERN_EFFECTHILIGHT
		| PATTERN_SMALLFONT | PATTERN_CENTERROW | PATTERN_DRAGNDROPEDIT
		| PATTERN_FLATBUTTONS | PATTERN_NOEXTRALOUD | PATTERN_2NDHIGHLIGHT
		| PATTERN_STDHIGHLIGHT | PATTERN_SHOWPREVIOUS | PATTERN_CONTSCROLL
		| PATTERN_SYNCMUTE | PATTERN_AUTODELAY | PATTERN_NOTEFADE
		| PATTERN_LARGECOMMENTS | PATTERN_SHOWDEFAULTVOLUME;
	m_nRowHighlightMeasures = 16;
	m_nRowHighlightBeats = 4;
	recordQuantizeRows = 0;
	rowDisplayOffset = 0;

	// Sample Editor
	m_nSampleUndoMaxBuffer = 0;	// Real sample buffer undo size will be set later.
	m_MayNormalizeSamplesOnLoad = true;

	GetDefaultColourScheme(rgbCustomColors);

	m_szKbdFile[0] = '\0';

	// Default chords
	MemsetZero(Chords);
	for(UINT ichord = 0; ichord < 3 * 12; ichord++)
	{
		Chords[ichord].key = (uint8)ichord;
		Chords[ichord].notes[0] = 0;
		Chords[ichord].notes[1] = 0;
		Chords[ichord].notes[2] = 0;

		if(ichord < 12)
		{
			// Major Chords
			Chords[ichord].notes[0] = (uint8)(ichord+5);
			Chords[ichord].notes[1] = (uint8)(ichord+8);
			Chords[ichord].notes[2] = (uint8)(ichord+11);
		} else if(ichord < 24)
		{
			// Minor Chords
			Chords[ichord].notes[0] = (uint8)(ichord-8);
			Chords[ichord].notes[1] = (uint8)(ichord-4);
			Chords[ichord].notes[2] = (uint8)(ichord-1);
		}
	}

	defaultModType = MOD_TYPE_IT;

	DefaultPlugVolumeHandling = PLUGIN_VOLUMEHANDLING_IGNORE;

	gnPlugWindowX = 243;
	gnPlugWindowY = 273;
	gnPlugWindowWidth = 370;
	gnPlugWindowHeight = 332;
	gnPlugWindowLast = 0;

	// dynamic defaults:

	MEMORYSTATUS gMemStatus;
	MemsetZero(gMemStatus);
	GlobalMemoryStatus(&gMemStatus);
#if 0
	Log("Physical: %lu\n", gMemStatus.dwTotalPhys);
	Log("Page File: %lu\n", gMemStatus.dwTotalPageFile);
	Log("Virtual: %lu\n", gMemStatus.dwTotalVirtual);
#endif
	// Allow allocations of at least 16MB
	if (gMemStatus.dwTotalPhys < 16*1024*1024) gMemStatus.dwTotalPhys = 16*1024*1024;
	m_nSampleUndoMaxBuffer = gMemStatus.dwTotalPhys / 10; // set sample undo buffer size
	if(m_nSampleUndoMaxBuffer < (1 << 20)) m_nSampleUndoMaxBuffer = (1 << 20);

#ifdef ENABLE_ASM
	// rough heuristic to select less cpu consuming defaults for old CPUs
	if(GetProcSupport() & PROCSUPPORT_MMX)
	{
		m_ResamplerSettings.SrcMode = SRCMODE_SPLINE;
	}
	if(GetProcSupport() & PROCSUPPORT_SSE)
	{
		m_ResamplerSettings.SrcMode = SRCMODE_POLYPHASE;
	}
#else
	// just use a sane default
	m_ResamplerSettings.SrcMode = SRCMODE_POLYPHASE;
#endif

}


DWORD TrackerSettings::GetSoundDeviceFlags() const
//------------------------------------------------
{
	return (m_SoundDeviceExclusiveMode ? SNDDEV_OPTIONS_EXCLUSIVE : 0) | (m_SoundDeviceBoostThreadPriority ? SNDDEV_OPTIONS_BOOSTTHREADPRIORITY : 0);
}


void TrackerSettings::SetSoundDeviceFlags(DWORD flags)
//----------------------------------------------------
{
	m_SoundDeviceExclusiveMode = (flags & SNDDEV_OPTIONS_EXCLUSIVE) ? true : false;
	m_SoundDeviceBoostThreadPriority = (flags & SNDDEV_OPTIONS_BOOSTTHREADPRIORITY) ? true : false;
}


void TrackerSettings::GetDefaultColourScheme(COLORREF (&colours)[MAX_MODCOLORS])
//------------------------------------------------------------------------------
{
	colours[MODCOLOR_BACKNORMAL] = RGB(0xFF, 0xFF, 0xFF);
	colours[MODCOLOR_TEXTNORMAL] = RGB(0x00, 0x00, 0x00);
	colours[MODCOLOR_BACKCURROW] = RGB(0xC0, 0xC0, 0xC0);
	colours[MODCOLOR_TEXTCURROW] = RGB(0x00, 0x00, 0x00);
	colours[MODCOLOR_BACKSELECTED] = RGB(0x00, 0x00, 0x00);
	colours[MODCOLOR_TEXTSELECTED] = RGB(0xFF, 0xFF, 0xFF);
	colours[MODCOLOR_SAMPLE] = RGB(0xFF, 0x00, 0x00);
	colours[MODCOLOR_BACKPLAYCURSOR] = RGB(0xFF, 0xFF, 0x80);
	colours[MODCOLOR_TEXTPLAYCURSOR] = RGB(0x00, 0x00, 0x00);
	colours[MODCOLOR_BACKHILIGHT] = RGB(0xE0, 0xE8, 0xE0);
	// Effect Colors
	colours[MODCOLOR_NOTE] = RGB(0x00, 0x00, 0x80);
	colours[MODCOLOR_INSTRUMENT] = RGB(0x00, 0x80, 0x80);
	colours[MODCOLOR_VOLUME] = RGB(0x00, 0x80, 0x00);
	colours[MODCOLOR_PANNING] = RGB(0x00, 0x80, 0x80);
	colours[MODCOLOR_PITCH] = RGB(0x80, 0x80, 0x00);
	colours[MODCOLOR_GLOBALS] = RGB(0x80, 0x00, 0x00);
	colours[MODCOLOR_ENVELOPES] = RGB(0x00, 0x00, 0xFF);
	// VU-Meters
	colours[MODCOLOR_VUMETER_LO] = RGB(0x00, 0xC8, 0x00);
	colours[MODCOLOR_VUMETER_MED] = RGB(0xFF, 0xC8, 0x00);
	colours[MODCOLOR_VUMETER_HI] = RGB(0xE1, 0x00, 0x00);
	// Channel separators
	colours[MODCOLOR_SEPSHADOW] = GetSysColor(COLOR_BTNSHADOW);
	colours[MODCOLOR_SEPFACE] = GetSysColor(COLOR_BTNFACE);
	colours[MODCOLOR_SEPHILITE] = GetSysColor(COLOR_BTNHIGHLIGHT);
	// Pattern blend colour
	colours[MODCOLOR_BLENDCOLOR] = GetSysColor(COLOR_BTNFACE);
	// Dodgy commands
	colours[MODCOLOR_DODGY_COMMANDS] = RGB(0xC0, 0x00, 0x00);
}


void TrackerSettings::LoadSettings()
//----------------------------------
{
	SettingsContainer & conf = theApp.GetSettings();

	CString storedVersion = IniVersion.Get().c_str();

	// If version number stored in INI is 1.17.02.40 or later, always load setting from INI file.
	// If it isn't, try loading from Registry first, then from the INI file.
	if (storedVersion >= "1.17.02.40" || !LoadRegistrySettings())
	{
		LoadINISettings(conf);
	}

	// The following stuff was also stored in mptrack.ini while the registry was still being used...

	// Load Chords
	LoadChords(Chords);

	// Load default macro configuration
	MIDIMacroConfig macros;
	theApp.GetDefaultMidiMacro(macros);
	for(int isfx = 0; isfx < 16; isfx++)
	{
		CHAR snam[8];
		wsprintf(snam, "SF%X", isfx);
		mpt::String::Copy(macros.szMidiSFXExt[isfx], conf.Read<std::string>("Zxx Macros", snam, macros.szMidiSFXExt[isfx]));
		mpt::String::SetNullTerminator(macros.szMidiSFXExt[isfx]);
	}
	for(int izxx = 0; izxx < 128; izxx++)
	{
		CHAR snam[8];
		wsprintf(snam, "Z%02X", izxx | 0x80);
		mpt::String::Copy(macros.szMidiZXXExt[izxx], conf.Read<std::string>("Zxx Macros", snam, macros.szMidiZXXExt[izxx]));
		mpt::String::SetNullTerminator(macros.szMidiZXXExt[izxx]);
	}
	// Fix old nasty broken (non-standard) MIDI configs in INI file.
	if(storedVersion >= "1.17" && storedVersion < "1.20")
	{
		macros.UpgradeMacros();
	}
	theApp.SetDefaultMidiMacro(macros);

	// Default directory location
	for(UINT i = 0; i < NUM_DIRS; i++)
	{
		_tcscpy(TrackerDirectories::Instance().m_szWorkingDirectory[i], TrackerDirectories::Instance().m_szDefaultDirectory[i]);
	}
	if (TrackerDirectories::Instance().m_szDefaultDirectory[DIR_MODS][0]) SetCurrentDirectory(TrackerDirectories::Instance().m_szDefaultDirectory[DIR_MODS]);
}


void TrackerSettings::LoadINISettings(SettingsContainer &conf)
//------------------------------------------------------------
{
	MptVersion::VersionNum vIniVersion = gcsPreviousVersion;

	// GUI Stuff
	gbMdiMaximize = conf.Read<int32>("Display", "MDIMaximize", gbMdiMaximize);
	glTreeWindowWidth = conf.Read<int32>("Display", "MDITreeWidth", glTreeWindowWidth);
	glTreeSplitRatio = conf.Read<int32>("Display", "MDITreeRatio", glTreeSplitRatio);
	glGeneralWindowHeight = conf.Read<int32>("Display", "MDIGeneralHeight", glGeneralWindowHeight);
	glPatternWindowHeight = conf.Read<int32>("Display", "MDIPatternHeight", glPatternWindowHeight);
	glSampleWindowHeight = conf.Read<int32>("Display", "MDISampleHeight", glSampleWindowHeight);
	glInstrumentWindowHeight = conf.Read<int32>("Display", "MDIInstrumentHeight", glInstrumentWindowHeight);
	glCommentsWindowHeight = conf.Read<int32>("Display", "MDICommentsHeight", glCommentsWindowHeight);
	glGraphWindowHeight = conf.Read<int32>("Display", "MDIGraphHeight", glGraphWindowHeight); //rewbs.graph
	gnPlugWindowX = conf.Read<int32>("Display", "PlugSelectWindowX", gnPlugWindowX);
	gnPlugWindowY = conf.Read<int32>("Display", "PlugSelectWindowY", gnPlugWindowY);
	gnPlugWindowWidth = conf.Read<int32>("Display", "PlugSelectWindowWidth", gnPlugWindowWidth);
	gnPlugWindowHeight = conf.Read<int32>("Display", "PlugSelectWindowHeight", gnPlugWindowHeight);
	gnPlugWindowLast = conf.Read<int32>("Display", "PlugSelectWindowLast", gnPlugWindowLast);
	gnMsgBoxVisiblityFlags = conf.Read<uint32>("Display", "MsgBoxVisibilityFlags", gnMsgBoxVisiblityFlags);
	VuMeterUpdateInterval = conf.Read<uint32>("Display", "VuMeterUpdateInterval", VuMeterUpdateInterval);

	// Internet Update
	{
		tm lastUpdate;
		MemsetZero(lastUpdate);
		CString s = conf.Read<CString>("Update", "LastUpdateCheck", "1970-01-01 00:00");
		if(sscanf(s, "%04d-%02d-%02d %02d:%02d", &lastUpdate.tm_year, &lastUpdate.tm_mon, &lastUpdate.tm_mday, &lastUpdate.tm_hour, &lastUpdate.tm_min) == 5)
		{
			lastUpdate.tm_year -= 1900;
			lastUpdate.tm_mon--;
		}

		time_t outTime = Util::sdTime::MakeGmTime(lastUpdate);

		if(outTime < 0) outTime = 0;

		CUpdateCheck::SetUpdateSettings
			(
			outTime,
			conf.Read<int32>("Update", "UpdateCheckPeriod", CUpdateCheck::GetUpdateCheckPeriod()),
			conf.Read<CString>("Update", "UpdateURL", CUpdateCheck::GetUpdateURL()),
			conf.Read<int32>("Update", "SendGUID", CUpdateCheck::GetSendGUID() ? 1 : 0) != 0,
			conf.Read<int32>("Update", "ShowUpdateHint", CUpdateCheck::GetShowUpdateHint() ? 1 : 0) != 0
			);
	}

	CHAR s[16];
	for (int ncol = 0; ncol < MAX_MODCOLORS; ncol++)
	{
		wsprintf(s, "Color%02d", ncol);
		rgbCustomColors[ncol] = conf.Read<uint32>("Display", s, rgbCustomColors[ncol]);
	}

	m_MorePortaudio = conf.Read<bool>("Sound Settings", "MorePortaudio", m_MorePortaudio);
	DWORD defaultDevice = SNDDEV_BUILD_ID(0, SNDDEV_WAVEOUT); // first WaveOut device
#ifndef NO_ASIO
	// If there's an ASIO device available, prefer it over DirectSound
	if(EnumerateSoundDevices(SNDDEV_ASIO, 0, nullptr, 0))
	{
		defaultDevice = SNDDEV_BUILD_ID(0, SNDDEV_ASIO);
	}
	CASIODevice::baseChannel = conf.Read<int32>("Sound Settings", "ASIOBaseChannel", CASIODevice::baseChannel);
#endif // NO_ASIO
	m_nWaveDevice = conf.Read<int32>("Sound Settings", "WaveDevice", defaultDevice);
	if(vIniVersion < MAKE_VERSION_NUMERIC(1, 22, 01, 03)) m_MixerSettings.MixerFlags |= OLD_SOUNDSETUP_SECONDARY;
	m_MixerSettings.MixerFlags = conf.Read<uint32>("Sound Settings", "SoundSetup", m_MixerSettings.MixerFlags);
	m_SoundDeviceExclusiveMode = conf.Read<bool>("Sound Settings", "ExclusiveMode", m_SoundDeviceExclusiveMode);
	m_SoundDeviceBoostThreadPriority = conf.Read<bool>("Sound Settings", "BoostThreadPriority", m_SoundDeviceBoostThreadPriority);
	if(vIniVersion < MAKE_VERSION_NUMERIC(1, 21, 01, 26)) m_MixerSettings.MixerFlags &= ~OLD_SOUNDSETUP_REVERSESTEREO;
	if(vIniVersion < MAKE_VERSION_NUMERIC(1, 22, 01, 03))
	{
		m_SoundDeviceExclusiveMode = ((m_MixerSettings.MixerFlags & OLD_SOUNDSETUP_SECONDARY) == 0);
		m_SoundDeviceBoostThreadPriority = ((m_MixerSettings.MixerFlags & OLD_SOUNDSETUP_NOBOOSTTHREADPRIORITY) == 0);
		m_MixerSettings.MixerFlags &= ~OLD_SOUNDSETUP_SECONDARY;
		m_MixerSettings.MixerFlags &= ~OLD_SOUNDSETUP_NOBOOSTTHREADPRIORITY;
	}
	m_MixerSettings.DSPMask = conf.Read<uint32>("Sound Settings", "Quality", m_MixerSettings.DSPMask);
	m_ResamplerSettings.SrcMode = (ResamplingMode)conf.Read<uint32>("Sound Settings", "SrcMode", m_ResamplerSettings.SrcMode);
	m_MixerSettings.gdwMixingFreq = conf.Read<uint32>("Sound Settings", "Mixing_Rate", 0);
	m_SampleFormat = conf.Read<uint32>("Sound Settings", "BitsPerSample", m_SampleFormat);
	m_MixerSettings.gnChannels = conf.Read<uint32>("Sound Settings", "ChannelMode", m_MixerSettings.gnChannels);
	DWORD LatencyMS = conf.Read<uint32>("Sound Settings", "Latency", 0);
	DWORD UpdateIntervalMS = conf.Read<uint32>("Sound Settings", "UpdateInterval", 0);
	if(LatencyMS == 0 || UpdateIntervalMS == 0)
	{
		// old versions have set BufferLength which meant different things than the current ISoundDevice interface wants to know
		DWORD BufferLengthMS = conf.Read<uint32>("Sound Settings", "BufferLength", 0);
		if(BufferLengthMS != 0)
		{
			if(BufferLengthMS < OLD_SNDDEV_MINBUFFERLEN) BufferLengthMS = OLD_SNDDEV_MINBUFFERLEN;
			if(BufferLengthMS > OLD_SNDDEV_MAXBUFFERLEN) BufferLengthMS = OLD_SNDDEV_MAXBUFFERLEN;
			if(SNDDEV_GET_TYPE(m_nWaveDevice) == SNDDEV_ASIO)
			{
				m_LatencyMS = BufferLengthMS;
				m_UpdateIntervalMS = BufferLengthMS / 8;
			} else
			{
				m_LatencyMS = BufferLengthMS * 3;
				m_UpdateIntervalMS = BufferLengthMS / 8;
			}
		} else
		{
			// use defaults
			m_LatencyMS = SNDDEV_DEFAULT_LATENCY_MS;
			m_UpdateIntervalMS = SNDDEV_DEFAULT_UPDATEINTERVAL_MS;
		}
	} else
	{
		m_LatencyMS = LatencyMS;
		m_UpdateIntervalMS = UpdateIntervalMS;
	}
	if(m_MixerSettings.gdwMixingFreq == 0)
	{
		m_MixerSettings.gdwMixingFreq = 44100;
#ifndef NO_ASIO
		// If no mixing rate is specified and we're using ASIO, get a mixing rate supported by the device.
		if(SNDDEV_GET_TYPE(m_nWaveDevice) == SNDDEV_ASIO)
		{
			ISoundDevice *dummy = CreateSoundDevice(SNDDEV_ASIO);
			if(dummy)
			{
				m_MixerSettings.gdwMixingFreq = dummy->GetCurrentSampleRate(SNDDEV_GET_NUMBER(m_nWaveDevice));
				delete dummy;
			}
		}
#endif // NO_ASIO
	}

	m_MixerSettings.m_nPreAmp = conf.Read<uint32>("Sound Settings", "PreAmp", m_MixerSettings.m_nPreAmp);
	m_MixerSettings.m_nStereoSeparation = conf.Read<int32>("Sound Settings", "StereoSeparation", m_MixerSettings.m_nStereoSeparation);
	m_MixerSettings.m_nMaxMixChannels = conf.Read<int32>("Sound Settings", "MixChannels", m_MixerSettings.m_nMaxMixChannels);
	m_ResamplerSettings.gbWFIRType = static_cast<BYTE>(conf.Read<uint32>("Sound Settings", "XMMSModplugResamplerWFIRType", m_ResamplerSettings.gbWFIRType));
	//gdWFIRCutoff = static_cast<double>(conf.Read<int32>("Sound Settings", "ResamplerWFIRCutoff", gdWFIRCutoff * 100.0)) / 100.0;
	m_ResamplerSettings.gdWFIRCutoff = static_cast<double>(conf.Read<int32>("Sound Settings", "ResamplerWFIRCutoff", Util::Round<long>(m_ResamplerSettings.gdWFIRCutoff * 100.0))) / 100.0;
	
	// Ramping... first try to read the old setting, then the new ones
	const long volRamp = conf.Read<int32>("Sound Settings", "VolumeRampSamples", -1);
	if(volRamp != -1)
	{
		m_MixerSettings.glVolumeRampUpSamples = m_MixerSettings.glVolumeRampDownSamples = volRamp;
	}
	m_MixerSettings.glVolumeRampUpSamples = conf.Read<int32>("Sound Settings", "VolumeRampUpSamples", m_MixerSettings.glVolumeRampUpSamples);
	m_MixerSettings.glVolumeRampDownSamples = conf.Read<int32>("Sound Settings", "VolumeRampDownSamples", m_MixerSettings.glVolumeRampDownSamples);

	// MIDI Setup
	m_dwMidiSetup = conf.Read<uint32>("MIDI Settings", "MidiSetup", m_dwMidiSetup);
	m_nMidiDevice = conf.Read<uint32>("MIDI Settings", "MidiDevice", m_nMidiDevice);
	aftertouchBehaviour = static_cast<RecordAftertouchOptions>(conf.Read<uint32>("MIDI Settings", "AftertouchBehaviour", aftertouchBehaviour));
	midiVelocityAmp = conf.Read<uint16>("MIDI Settings", "MidiVelocityAmp", midiVelocityAmp);
	midiImportSpeed = conf.Read<int32>("MIDI Settings", "MidiImportSpeed", midiImportSpeed);
	midiImportPatternLen = conf.Read<int32>("MIDI Settings", "MidiImportPatLen", midiImportPatternLen);
	if((m_dwMidiSetup & 0x40) != 0 && vIniVersion < MAKE_VERSION_NUMERIC(1, 20, 00, 86))
	{
		// This flag used to be "amplify MIDI Note Velocity" - with a fixed amplification factor of 2.
		midiVelocityAmp = 200;
		m_dwMidiSetup &= ~0x40;
	}
	ParseIgnoredCCs(conf.Read<CString>("MIDI Settings", "IgnoredCCs", ""));

	m_dwPatternSetup = conf.Read<uint32>("Pattern Editor", "PatternSetup", m_dwPatternSetup);
	if(vIniVersion < MAKE_VERSION_NUMERIC(1, 17, 02, 50))
		m_dwPatternSetup |= PATTERN_NOTEFADE;
	if(vIniVersion < MAKE_VERSION_NUMERIC(1, 17, 03, 01))
		m_dwPatternSetup |= PATTERN_RESETCHANNELS;
	if(vIniVersion < MAKE_VERSION_NUMERIC(1, 19, 00, 07))
		m_dwPatternSetup &= ~0x800;					// this was previously deprecated and is now used for something else
	if(vIniVersion < MAKE_VERSION_NUMERIC(1, 20, 00, 04))
		m_dwPatternSetup &= ~0x200000;				// dito
	if(vIniVersion < MAKE_VERSION_NUMERIC(1, 20, 00, 07))
		m_dwPatternSetup &= ~0x400000;				// dito
	if(vIniVersion < MAKE_VERSION_NUMERIC(1, 20, 00, 39))
		m_dwPatternSetup &= ~0x10000000;			// dito

	m_nRowHighlightMeasures = conf.Read<uint32>("Pattern Editor", "RowSpacing", m_nRowHighlightMeasures);
	m_nRowHighlightBeats = conf.Read<uint32>("Pattern Editor", "RowSpacing2", m_nRowHighlightBeats);
	gbLoopSong = conf.Read<uint32>("Pattern Editor", "LoopSong", gbLoopSong);
	gnPatternSpacing = conf.Read<uint32>("Pattern Editor", "Spacing", gnPatternSpacing);
	gbPatternVUMeters = conf.Read<uint32>("Pattern Editor", "VU-Meters", gbPatternVUMeters);
	gbPatternPluginNames = conf.Read<uint32>("Pattern Editor", "Plugin-Names", gbPatternPluginNames);
	gbPatternRecord = conf.Read<uint32>("Pattern Editor", "Record", gbPatternRecord);
	gnAutoChordWaitTime = conf.Read<uint32>("Pattern Editor", "AutoChordWaitTime", gnAutoChordWaitTime);
	orderlistMargins = conf.Read<int32>("Pattern Editor", "DefaultSequenceMargins", orderlistMargins);
	rowDisplayOffset = conf.Read<int32>("Pattern Editor", "RowDisplayOffset", rowDisplayOffset);
	recordQuantizeRows = conf.Read<uint32>("Pattern Editor", "RecordQuantize", recordQuantizeRows);
	gbShowHackControls = (0 != conf.Read<uint32>("Misc", "ShowHackControls", gbShowHackControls ? 1 : 0));
	DefaultPlugVolumeHandling = static_cast<uint8>(conf.Read<int32>("Misc", "DefaultPlugVolumeHandling", DefaultPlugVolumeHandling));
	if(DefaultPlugVolumeHandling >= PLUGIN_VOLUMEHANDLING_MAX) DefaultPlugVolumeHandling = PLUGIN_VOLUMEHANDLING_IGNORE;
	autoApplySmoothFT2Ramping = (0 != conf.Read<uint32>("Misc", "SmoothFT2Ramping", false));

	m_nSampleUndoMaxBuffer = conf.Read<int32>("Sample Editor" , "UndoBufferSize", m_nSampleUndoMaxBuffer >> 20);
	m_nSampleUndoMaxBuffer = MAX(1, m_nSampleUndoMaxBuffer) << 20;
	m_MayNormalizeSamplesOnLoad = conf.Read<bool>("Sample Editor" , "MayNormalizeSamplesOnLoad", m_MayNormalizeSamplesOnLoad);

	PatternClipboard::SetClipboardSize(conf.Read<int32>("Pattern Editor", "NumClipboards", PatternClipboard::GetClipboardSize()));
	
	// Default Paths
	for(size_t i = 0; i < NUM_DIRS; i++)
	{
		if(TrackerDirectories::Instance().m_szDirectoryToSettingsName[i][0] == '\0')
			continue;

		TCHAR szPath[_MAX_PATH] = "";
		mpt::String::Copy(szPath, conf.Read<std::string>("Paths", TrackerDirectories::Instance().m_szDirectoryToSettingsName[i], GetDefaultDirectory(static_cast<Directory>(i))));
		theApp.RelativePathToAbsolute(szPath);
		SetDefaultDirectory(szPath, static_cast<Directory>(i), false);

	}
	mpt::String::Copy(m_szKbdFile, conf.Read<std::string>("Paths", "Key_Config_File", m_szKbdFile));
	theApp.RelativePathToAbsolute(m_szKbdFile);


	// Effects Settings
#ifndef NO_DSP
	m_DSPSettings.m_nXBassDepth = conf.Read<int32>("Effects", "XBassDepth", m_DSPSettings.m_nXBassDepth);
	m_DSPSettings.m_nXBassRange = conf.Read<int32>("Effects", "XBassRange", m_DSPSettings.m_nXBassRange);
#endif
#ifndef NO_REVERB
	m_ReverbSettings.m_nReverbDepth = conf.Read<int32>("Effects", "ReverbDepth", m_ReverbSettings.m_nReverbDepth);
	m_ReverbSettings.m_nReverbType = conf.Read<int32>("Effects", "ReverbType", m_ReverbSettings.m_nReverbType);
#endif
#ifndef NO_DSP
	m_DSPSettings.m_nProLogicDepth = conf.Read<int32>("Effects", "ProLogicDepth", m_DSPSettings.m_nProLogicDepth);
	m_DSPSettings.m_nProLogicDelay = conf.Read<int32>("Effects", "ProLogicDelay", m_DSPSettings.m_nProLogicDelay);
#endif


#ifndef NO_EQ
	// EQ Settings
	m_EqSettings = conf.Read<EQPreset>("Effects", "EQ_Settings", m_EqSettings);
	CEQSetupDlg::gUserPresets[0] = conf.Read<EQPreset>("Effects", "EQ_User1", CEQSetupDlg::gUserPresets[0]);
	CEQSetupDlg::gUserPresets[1] = conf.Read<EQPreset>("Effects", "EQ_User2", CEQSetupDlg::gUserPresets[1]);
	CEQSetupDlg::gUserPresets[2] = conf.Read<EQPreset>("Effects", "EQ_User3", CEQSetupDlg::gUserPresets[2]);
	CEQSetupDlg::gUserPresets[3] = conf.Read<EQPreset>("Effects", "EQ_User4", CEQSetupDlg::gUserPresets[3]);
	mpt::String::SetNullTerminator(m_EqSettings.szName);
	mpt::String::SetNullTerminator(CEQSetupDlg::gUserPresets[0].szName);
	mpt::String::SetNullTerminator(CEQSetupDlg::gUserPresets[1].szName);
	mpt::String::SetNullTerminator(CEQSetupDlg::gUserPresets[2].szName);
	mpt::String::SetNullTerminator(CEQSetupDlg::gUserPresets[3].szName);
#endif


	// Auto saver settings
	CMainFrame::m_pAutoSaver = new CAutoSaver();
	if(conf.Read<int32>("AutoSave", "Enabled", true))
	{
		CMainFrame::m_pAutoSaver->Enable();
	} else
	{
		CMainFrame::m_pAutoSaver->Disable();
	}
	CMainFrame::m_pAutoSaver->SetSaveInterval(conf.Read<int32>("AutoSave", "IntervalMinutes", 10));
	CMainFrame::m_pAutoSaver->SetHistoryDepth(conf.Read<int32>("AutoSave", "BackupHistory", 3));
	CMainFrame::m_pAutoSaver->SetUseOriginalPath(conf.Read<int32>("AutoSave", "UseOriginalPath", true) != 0);
	{
		TCHAR szPath[_MAX_PATH] = "";
		mpt::String::Copy(szPath, conf.Read<std::string>("AutoSave", "Path", ""));
		theApp.RelativePathToAbsolute(szPath);
		CMainFrame::m_pAutoSaver->SetPath(szPath);
	}
	CMainFrame::m_pAutoSaver->SetFilenameTemplate(conf.Read<CString>("AutoSave", "FileNameTemplate", ""));


	// Default mod type when using the "New" button
	const MODTYPE oldDefault = defaultModType;
	defaultModType = CModSpecifications::ExtensionToType(conf.Read<CString>("Misc", "DefaultModType", CSoundFile::GetModSpecifications(defaultModType).fileExtension).GetString());
	if(defaultModType == MOD_TYPE_NONE)
	{
		defaultModType = oldDefault;
	}
}


#define SETTINGS_REGKEY_BASE		"Software\\Olivier Lapicque\\"
#define SETTINGS_REGKEY_DEFAULT		"ModPlug Tracker"
#define SETTINGS_REGEXT_WINDOW		"\\Window"
#define SETTINGS_REGEXT_PATTERNEDITOR	"\\Pattern Editor"

void TrackerSettings::LoadRegistryEQ(HKEY key, LPCSTR pszName, EQPreset *pEqSettings)
//-----------------------------------------------------------------------------------
{
	DWORD dwType = REG_BINARY;
	DWORD dwSize = sizeof(EQPreset);
	RegQueryValueEx(key, pszName, NULL, &dwType, (LPBYTE)pEqSettings, &dwSize);
	for (UINT i=0; i<MAX_EQ_BANDS; i++)
	{
		if (pEqSettings->Gains[i] > 32) pEqSettings->Gains[i] = 16;
		if ((pEqSettings->Freqs[i] < 100) || (pEqSettings->Freqs[i] > 10000)) pEqSettings->Freqs[i] = CEQSetupDlg::gEQPresets[0].Freqs[i];
	}
	mpt::String::SetNullTerminator(pEqSettings->szName);
}


bool TrackerSettings::LoadRegistrySettings()
//------------------------------------------
{
	CString m_csRegKey;
	CString m_csRegExt;
	CString m_csRegWindow;
	CString m_csRegPatternEditor;
	m_csRegKey.Format("%s%s", SETTINGS_REGKEY_BASE, SETTINGS_REGKEY_DEFAULT);
	m_csRegWindow.Format("%s%s", m_csRegKey, SETTINGS_REGEXT_WINDOW);
	m_csRegPatternEditor.Format("%s%s", m_csRegKey, SETTINGS_REGEXT_PATTERNEDITOR);

	HKEY key;
	DWORD dwREG_DWORD = REG_DWORD;
	DWORD dwREG_SZ = REG_SZ;
	DWORD dwDWORDSize = sizeof(UINT);
	DWORD dwCRSIZE = sizeof(COLORREF);

	bool asEnabled=true;
	int asInterval=10;
	int asBackupHistory=3;
	bool asUseOriginalPath=true;
	CString asPath ="";
	CString asFileNameTemplate="";

	if (RegOpenKeyEx(HKEY_CURRENT_USER,	m_csRegWindow, 0, KEY_READ, &key) == ERROR_SUCCESS)
	{
		DWORD d = 0;
		RegQueryValueEx(key, "Maximized", NULL, &dwREG_DWORD, (LPBYTE)&d, &dwDWORDSize);
		if (d) theApp.m_nCmdShow = SW_SHOWMAXIMIZED;
		RegQueryValueEx(key, "MDIMaximize", NULL, &dwREG_DWORD, (LPBYTE)&gbMdiMaximize, &dwDWORDSize);
		RegQueryValueEx(key, "MDITreeWidth", NULL, &dwREG_DWORD, (LPBYTE)&glTreeWindowWidth, &dwDWORDSize);
		RegQueryValueEx(key, "MDIGeneralHeight", NULL, &dwREG_DWORD, (LPBYTE)&glGeneralWindowHeight, &dwDWORDSize);
		RegQueryValueEx(key, "MDIPatternHeight", NULL, &dwREG_DWORD, (LPBYTE)&glPatternWindowHeight, &dwDWORDSize);
		RegQueryValueEx(key, "MDISampleHeight", NULL, &dwREG_DWORD,  (LPBYTE)&glSampleWindowHeight, &dwDWORDSize);
		RegQueryValueEx(key, "MDIInstrumentHeight", NULL, &dwREG_DWORD,  (LPBYTE)&glInstrumentWindowHeight, &dwDWORDSize);
		RegQueryValueEx(key, "MDICommentsHeight", NULL, &dwREG_DWORD,  (LPBYTE)&glCommentsWindowHeight, &dwDWORDSize);
		RegQueryValueEx(key, "MDIGraphHeight", NULL, &dwREG_DWORD,  (LPBYTE)&glGraphWindowHeight, &dwDWORDSize); //rewbs.graph
		RegQueryValueEx(key, "MDITreeRatio", NULL, &dwREG_DWORD, (LPBYTE)&glTreeSplitRatio, &dwDWORDSize);
		// Colors
		for (int ncol = 0; ncol < MAX_MODCOLORS; ncol++)
		{
			CHAR s[16];
			wsprintf(s, "Color%02d", ncol);
			RegQueryValueEx(key, s, NULL, &dwREG_DWORD, (LPBYTE)&rgbCustomColors[ncol], &dwCRSIZE);
		}
		RegCloseKey(key);
	}

	if (RegOpenKeyEx(HKEY_CURRENT_USER,	m_csRegKey, 0, KEY_READ, &key) == ERROR_SUCCESS)
	{
		RegQueryValueEx(key, "SoundSetup", NULL, &dwREG_DWORD, (LPBYTE)&m_MixerSettings.MixerFlags, &dwDWORDSize);
		m_MixerSettings.MixerFlags &= ~OLD_SOUNDSETUP_REVERSESTEREO;
		m_SoundDeviceExclusiveMode = ((m_MixerSettings.MixerFlags & OLD_SOUNDSETUP_SECONDARY) == 0);
		m_MixerSettings.MixerFlags &= ~OLD_SOUNDSETUP_SECONDARY;
		RegQueryValueEx(key, "Quality", NULL, &dwREG_DWORD, (LPBYTE)&m_MixerSettings.DSPMask, &dwDWORDSize);
		DWORD dummysrcmode = m_ResamplerSettings.SrcMode;
		RegQueryValueEx(key, "SrcMode", NULL, &dwREG_DWORD, (LPBYTE)&dummysrcmode, &dwDWORDSize);
		m_ResamplerSettings.SrcMode = (ResamplingMode)dummysrcmode;
		RegQueryValueEx(key, "Mixing_Rate", NULL, &dwREG_DWORD, (LPBYTE)&m_MixerSettings.gdwMixingFreq, &dwDWORDSize);
		DWORD BufferLengthMS = 0;
		RegQueryValueEx(key, "BufferLength", NULL, &dwREG_DWORD, (LPBYTE)&BufferLengthMS, &dwDWORDSize);
		if(BufferLengthMS != 0)
		{
			if((BufferLengthMS < OLD_SNDDEV_MINBUFFERLEN) || (BufferLengthMS > OLD_SNDDEV_MAXBUFFERLEN)) BufferLengthMS = 100;
			if(SNDDEV_GET_TYPE(m_nWaveDevice) == SNDDEV_ASIO)
			{
				m_LatencyMS = BufferLengthMS;
				m_UpdateIntervalMS = BufferLengthMS / 8;
			} else
			{
				m_LatencyMS = BufferLengthMS * 3;
				m_UpdateIntervalMS = BufferLengthMS / 8;
			}
		}
		RegQueryValueEx(key, "PreAmp", NULL, &dwREG_DWORD, (LPBYTE)&m_MixerSettings.m_nPreAmp, &dwDWORDSize);

		CHAR sPath[_MAX_PATH] = "";
		DWORD dwSZSIZE = sizeof(sPath);
		RegQueryValueEx(key, "Songs_Directory", NULL, &dwREG_SZ, (LPBYTE)sPath, &dwSZSIZE);
		SetDefaultDirectory(sPath, DIR_MODS);
		dwSZSIZE = sizeof(sPath);
		RegQueryValueEx(key, "Samples_Directory", NULL, &dwREG_SZ, (LPBYTE)sPath, &dwSZSIZE);
		SetDefaultDirectory(sPath, DIR_SAMPLES);
		dwSZSIZE = sizeof(sPath);
		RegQueryValueEx(key, "Instruments_Directory", NULL, &dwREG_SZ, (LPBYTE)sPath, &dwSZSIZE);
		SetDefaultDirectory(sPath, DIR_INSTRUMENTS);
		dwSZSIZE = sizeof(sPath);
		RegQueryValueEx(key, "Plugins_Directory", NULL, &dwREG_SZ, (LPBYTE)sPath, &dwSZSIZE);
		SetDefaultDirectory(sPath, DIR_PLUGINS);
		dwSZSIZE = sizeof(m_szKbdFile);
		RegQueryValueEx(key, "Key_Config_File", NULL, &dwREG_SZ, (LPBYTE)m_szKbdFile, &dwSZSIZE);

#ifndef NO_DSP
		RegQueryValueEx(key, "XBassDepth", NULL, &dwREG_DWORD, (LPBYTE)&m_DSPSettings.m_nXBassDepth, &dwDWORDSize);
		RegQueryValueEx(key, "XBassRange", NULL, &dwREG_DWORD, (LPBYTE)&m_DSPSettings.m_nXBassRange, &dwDWORDSize);
#endif
#ifndef NO_REVERB
		RegQueryValueEx(key, "ReverbDepth", NULL, &dwREG_DWORD, (LPBYTE)&m_ReverbSettings.m_nReverbDepth, &dwDWORDSize);
		RegQueryValueEx(key, "ReverbType", NULL, &dwREG_DWORD, (LPBYTE)&m_ReverbSettings.m_nReverbType, &dwDWORDSize);
#endif NO_REVERB
#ifndef NO_DSP
		RegQueryValueEx(key, "ProLogicDepth", NULL, &dwREG_DWORD, (LPBYTE)&m_DSPSettings.m_nProLogicDepth, &dwDWORDSize);
		RegQueryValueEx(key, "ProLogicDelay", NULL, &dwREG_DWORD, (LPBYTE)&m_DSPSettings.m_nProLogicDelay, &dwDWORDSize);
#endif
		RegQueryValueEx(key, "StereoSeparation", NULL, &dwREG_DWORD, (LPBYTE)&m_MixerSettings.m_nStereoSeparation, &dwDWORDSize);
		RegQueryValueEx(key, "MixChannels", NULL, &dwREG_DWORD, (LPBYTE)&m_MixerSettings.m_nMaxMixChannels, &dwDWORDSize);
		RegQueryValueEx(key, "MidiSetup", NULL, &dwREG_DWORD, (LPBYTE)&m_dwMidiSetup, &dwDWORDSize);
		if((m_dwMidiSetup & 0x40) != 0)
		{
			// This flag used to be "amplify MIDI Note Velocity" - with a fixed amplification factor of 2.
			midiVelocityAmp = 200;
			m_dwMidiSetup &= ~0x40;
		}
		RegQueryValueEx(key, "MidiDevice", NULL, &dwREG_DWORD, (LPBYTE)&m_nMidiDevice, &dwDWORDSize);
		RegQueryValueEx(key, "PatternSetup", NULL, &dwREG_DWORD, (LPBYTE)&m_dwPatternSetup, &dwDWORDSize);
		m_dwPatternSetup &= ~(0x800|0x200000|0x400000);	// various deprecated old options
		m_dwPatternSetup |= PATTERN_NOTEFADE; // Set flag to maintain old behaviour (was changed in 1.17.02.50).
		m_dwPatternSetup |= PATTERN_RESETCHANNELS; // Set flag to reset channels on loop was changed in 1.17.03.01).
		RegQueryValueEx(key, "RowSpacing", NULL, &dwREG_DWORD, (LPBYTE)&m_nRowHighlightMeasures, &dwDWORDSize);
		RegQueryValueEx(key, "RowSpacing2", NULL, &dwREG_DWORD, (LPBYTE)&m_nRowHighlightBeats, &dwDWORDSize);
		RegQueryValueEx(key, "LoopSong", NULL, &dwREG_DWORD, (LPBYTE)&gbLoopSong, &dwDWORDSize);
		DWORD dummy_sampleformat = m_SampleFormat;
		RegQueryValueEx(key, "BitsPerSample", NULL, &dwREG_DWORD, (LPBYTE)&dummy_sampleformat, &dwDWORDSize);
		m_SampleFormat = (long)dummy_sampleformat;
		RegQueryValueEx(key, "ChannelMode", NULL, &dwREG_DWORD, (LPBYTE)&m_MixerSettings.gnChannels, &dwDWORDSize);
		RegQueryValueEx(key, "MidiImportSpeed", NULL, &dwREG_DWORD, (LPBYTE)&midiImportSpeed, &dwDWORDSize);
		RegQueryValueEx(key, "MidiImportPatLen", NULL, &dwREG_DWORD, (LPBYTE)&midiImportPatternLen, &dwDWORDSize);
#ifndef NO_EQ
		// EQ
		LoadRegistryEQ(key, "EQ_Settings", &m_EqSettings);
		LoadRegistryEQ(key, "EQ_User1", &CEQSetupDlg::gUserPresets[0]);
		LoadRegistryEQ(key, "EQ_User2", &CEQSetupDlg::gUserPresets[1]);
		LoadRegistryEQ(key, "EQ_User3", &CEQSetupDlg::gUserPresets[2]);
		LoadRegistryEQ(key, "EQ_User4", &CEQSetupDlg::gUserPresets[3]);
#endif

		//rewbs.resamplerConf
		dwDWORDSize = sizeof(m_ResamplerSettings.gbWFIRType);
		RegQueryValueEx(key, "XMMSModplugResamplerWFIRType", NULL, &dwREG_DWORD, (LPBYTE)&m_ResamplerSettings.gbWFIRType, &dwDWORDSize);
		dwDWORDSize = sizeof(m_ResamplerSettings.gdWFIRCutoff);
		RegQueryValueEx(key, "ResamplerWFIRCutoff", NULL, &dwREG_DWORD, (LPBYTE)&m_ResamplerSettings.gdWFIRCutoff, &dwDWORDSize);
		dwDWORDSize = sizeof(m_MixerSettings.glVolumeRampUpSamples);
		RegQueryValueEx(key, "VolumeRampSamples", NULL, &dwREG_DWORD, (LPBYTE)&m_MixerSettings.glVolumeRampUpSamples, &dwDWORDSize);
		m_MixerSettings.glVolumeRampDownSamples = m_MixerSettings.glVolumeRampUpSamples;

		//end rewbs.resamplerConf
		//rewbs.autochord
		dwDWORDSize = sizeof(gnAutoChordWaitTime);
		RegQueryValueEx(key, "AutoChordWaitTime", NULL, &dwREG_DWORD, (LPBYTE)&gnAutoChordWaitTime, &dwDWORDSize);
		//end rewbs.autochord

		dwDWORDSize = sizeof(gnPlugWindowX);
		RegQueryValueEx(key, "PlugSelectWindowX", NULL, &dwREG_DWORD, (LPBYTE)&gnPlugWindowX, &dwDWORDSize);
		dwDWORDSize = sizeof(gnPlugWindowY);
		RegQueryValueEx(key, "PlugSelectWindowY", NULL, &dwREG_DWORD, (LPBYTE)&gnPlugWindowY, &dwDWORDSize);
		dwDWORDSize = sizeof(gnPlugWindowWidth);
		RegQueryValueEx(key, "PlugSelectWindowWidth", NULL, &dwREG_DWORD, (LPBYTE)&gnPlugWindowWidth, &dwDWORDSize);
		dwDWORDSize = sizeof(gnPlugWindowHeight);
		RegQueryValueEx(key, "PlugSelectWindowHeight", NULL, &dwREG_DWORD, (LPBYTE)&gnPlugWindowHeight, &dwDWORDSize);
		dwDWORDSize = sizeof(gnPlugWindowLast);
		RegQueryValueEx(key, "PlugSelectWindowLast", NULL, &dwREG_DWORD, (LPBYTE)&gnPlugWindowLast, &dwDWORDSize);


		//rewbs.autoSave
		dwDWORDSize = sizeof(asEnabled);
		RegQueryValueEx(key, "AutoSave_Enabled", NULL, &dwREG_DWORD, (LPBYTE)&asEnabled, &dwDWORDSize);
		dwDWORDSize = sizeof(asInterval);
		RegQueryValueEx(key, "AutoSave_IntervalMinutes", NULL, &dwREG_DWORD, (LPBYTE)&asInterval, &dwDWORDSize);
		dwDWORDSize = sizeof(asBackupHistory);
		RegQueryValueEx(key, "AutoSave_BackupHistory", NULL, &dwREG_DWORD, (LPBYTE)&asBackupHistory, &dwDWORDSize);
		dwDWORDSize = sizeof(asUseOriginalPath);
		RegQueryValueEx(key, "AutoSave_UseOriginalPath", NULL, &dwREG_DWORD, (LPBYTE)&asUseOriginalPath, &dwDWORDSize);

		dwDWORDSize = MAX_PATH;
		RegQueryValueEx(key, "AutoSave_Path", NULL, &dwREG_DWORD, (LPBYTE)asPath.GetBuffer(dwDWORDSize/sizeof(TCHAR)), &dwDWORDSize);
		asPath.ReleaseBuffer();

		dwDWORDSize = MAX_PATH;
		RegQueryValueEx(key, "AutoSave_FileNameTemplate", NULL, &dwREG_DWORD, (LPBYTE)asFileNameTemplate.GetBuffer(dwDWORDSize/sizeof(TCHAR)), &dwDWORDSize);
		asFileNameTemplate.ReleaseBuffer();

		//end rewbs.autoSave

		RegCloseKey(key);
	} else
	{
		return false;
	}

	CMainFrame::m_pAutoSaver = new CAutoSaver(asEnabled, asInterval, asBackupHistory, asUseOriginalPath, asPath, asFileNameTemplate);

	if(RegOpenKeyEx(HKEY_CURRENT_USER, m_csRegPatternEditor, 0, KEY_READ, &key) == ERROR_SUCCESS)
	{
		dwDWORDSize = sizeof(gnPatternSpacing);
		RegQueryValueEx(key, "Spacing", NULL, &dwREG_DWORD, (LPBYTE)&gnPatternSpacing, &dwDWORDSize);
		dwDWORDSize = sizeof(gbPatternVUMeters);
		RegQueryValueEx(key, "VU-Meters", NULL, &dwREG_DWORD, (LPBYTE)&gbPatternVUMeters, &dwDWORDSize);
		dwDWORDSize = sizeof(gbPatternPluginNames);
		RegQueryValueEx(key, "Plugin-Names", NULL, &dwREG_DWORD, (LPBYTE)&gbPatternPluginNames, &dwDWORDSize);
		RegCloseKey(key);
	}

	return true;
}


void TrackerSettings::SaveSettings()
//----------------------------------
{
	SettingsContainer & conf = theApp.GetSettings();

	WINDOWPLACEMENT wpl;
	wpl.length = sizeof(WINDOWPLACEMENT);
	CMainFrame::GetMainFrame()->GetWindowPlacement(&wpl);
	conf.Write<WINDOWPLACEMENT>("Display", "WindowPlacement", wpl);

	conf.Write<int32>("Display", "MDIMaximize", gbMdiMaximize);
	conf.Write<int32>("Display", "MDITreeWidth", glTreeWindowWidth);
	conf.Write<int32>("Display", "MDITreeRatio", glTreeSplitRatio);
	conf.Write<int32>("Display", "MDIGeneralHeight", glGeneralWindowHeight);
	conf.Write<int32>("Display", "MDIPatternHeight", glPatternWindowHeight);
	conf.Write<int32>("Display", "MDISampleHeight", glSampleWindowHeight);
	conf.Write<int32>("Display", "MDIInstrumentHeight", glInstrumentWindowHeight);
	conf.Write<int32>("Display", "MDICommentsHeight", glCommentsWindowHeight);
	conf.Write<int32>("Display", "MDIGraphHeight", glGraphWindowHeight); //rewbs.graph
	conf.Write<int32>("Display", "PlugSelectWindowX", gnPlugWindowX);
	conf.Write<int32>("Display", "PlugSelectWindowY", gnPlugWindowY);
	conf.Write<int32>("Display", "PlugSelectWindowWidth", gnPlugWindowWidth);
	conf.Write<int32>("Display", "PlugSelectWindowHeight", gnPlugWindowHeight);
	conf.Write<int32>("Display", "PlugSelectWindowLast", gnPlugWindowLast);
	conf.Write<uint32>("Display", "MsgBoxVisibilityFlags", gnMsgBoxVisiblityFlags);
	conf.Write<uint32>("Display", "VuMeterUpdateInterval", VuMeterUpdateInterval);
	
	// Internet Update
	{
		CString outDate;
		const time_t t = CUpdateCheck::GetLastUpdateCheck();
		const tm* const lastUpdate = gmtime(&t);
		if(lastUpdate != nullptr)
		{
			outDate.Format("%04d-%02d-%02d %02d:%02d", lastUpdate->tm_year + 1900, lastUpdate->tm_mon + 1, lastUpdate->tm_mday, lastUpdate->tm_hour, lastUpdate->tm_min);
		}
		conf.Write<CString>("Update", "LastUpdateCheck", outDate);
		conf.Write<int32>("Update", "UpdateCheckPeriod", CUpdateCheck::GetUpdateCheckPeriod());
		conf.Write<CString>("Update", "UpdateURL", CUpdateCheck::GetUpdateURL());
		conf.Write<int32>("Update", "SendGUID", CUpdateCheck::GetSendGUID() ? 1 : 0);
		conf.Write<int32>("Update", "ShowUpdateHint", CUpdateCheck::GetShowUpdateHint() ? 1 : 0);
	}

	CHAR s[16];
	for (int ncol = 0; ncol < MAX_MODCOLORS; ncol++)
	{
		wsprintf(s, "Color%02d", ncol);
		conf.Write<uint32>("Display", s, rgbCustomColors[ncol]);
	}

	conf.Write<int32>("Sound Settings", "WaveDevice", m_nWaveDevice);
	conf.Write<uint32>("Sound Settings", "SoundSetup", m_MixerSettings.MixerFlags);
	conf.Write<bool>("Sound Settings", "ExclusiveMode", m_SoundDeviceExclusiveMode);
	conf.Write<bool>("Sound Settings", "BoostThreadPriority", m_SoundDeviceBoostThreadPriority);
	conf.Write<uint32>("Sound Settings", "Quality", m_MixerSettings.DSPMask);
	conf.Write<uint32>("Sound Settings", "SrcMode", m_ResamplerSettings.SrcMode);
	conf.Write<uint32>("Sound Settings", "Mixing_Rate", m_MixerSettings.gdwMixingFreq);
	conf.Write<uint32>("Sound Settings", "BitsPerSample", m_SampleFormat);
	conf.Write<uint32>("Sound Settings", "ChannelMode", m_MixerSettings.gnChannels);
	conf.Write<uint32>("Sound Settings", "Latency", m_LatencyMS);
	conf.Write<uint32>("Sound Settings", "UpdateInterval", m_UpdateIntervalMS);
	conf.Write<uint32>("Sound Settings", "PreAmp", m_MixerSettings.m_nPreAmp);
	conf.Write<int32>("Sound Settings", "StereoSeparation", m_MixerSettings.m_nStereoSeparation);
	conf.Write<int32>("Sound Settings", "MixChannels", m_MixerSettings.m_nMaxMixChannels);
	conf.Write<uint32>("Sound Settings", "XMMSModplugResamplerWFIRType", m_ResamplerSettings.gbWFIRType);
	conf.Write<int32>("Sound Settings", "ResamplerWFIRCutoff", static_cast<int>(m_ResamplerSettings.gdWFIRCutoff*100+0.5));
	conf.Remove("Sound Settings", "VolumeRampSamples");	// deprecated
	conf.Write<int32>("Sound Settings", "VolumeRampUpSamples", m_MixerSettings.glVolumeRampUpSamples);
	conf.Write<int32>("Sound Settings", "VolumeRampDownSamples", m_MixerSettings.glVolumeRampDownSamples);

	// MIDI Settings
	conf.Write<uint32>("MIDI Settings", "MidiSetup", m_dwMidiSetup);
	conf.Write<uint32>("MIDI Settings", "MidiDevice", m_nMidiDevice);
	conf.Write<uint32>("MIDI Settings", "AftertouchBehaviour", aftertouchBehaviour);
	conf.Write<uint16>("MIDI Settings", "MidiVelocityAmp", midiVelocityAmp);
	conf.Write<int32>("MIDI Settings", "MidiImportSpeed", midiImportSpeed);
	conf.Write<int32>("MIDI Settings", "MidiImportPatLen", midiImportPatternLen);
	conf.Write<std::string>("MIDI Settings", "IgnoredCCs", IgnoredCCsToString());

	conf.Write<uint32>("Pattern Editor", "PatternSetup", m_dwPatternSetup);
	conf.Write<uint32>("Pattern Editor", "RowSpacing", m_nRowHighlightMeasures);
	conf.Write<uint32>("Pattern Editor", "RowSpacing2", m_nRowHighlightBeats);
	conf.Write<uint32>("Pattern Editor", "LoopSong", gbLoopSong);
	conf.Write<uint32>("Pattern Editor", "Spacing", gnPatternSpacing);
	conf.Write<uint32>("Pattern Editor", "VU-Meters", gbPatternVUMeters);
	conf.Write<uint32>("Pattern Editor", "Plugin-Names", gbPatternPluginNames);
	conf.Write<uint32>("Pattern Editor", "Record", gbPatternRecord);
	conf.Write<uint32>("Pattern Editor", "AutoChordWaitTime", gnAutoChordWaitTime);
	conf.Write<uint32>("Pattern Editor", "RecordQuantize", recordQuantizeRows);

	conf.Write<uint32>("Pattern Editor", "NumClipboards", PatternClipboard::GetClipboardSize());

	conf.Write<bool>("Sample Editor", "MayNormalizeSamplesOnLoad", m_MayNormalizeSamplesOnLoad);
	
	// Write default paths
	const bool bConvertPaths = theApp.IsPortableMode();
	TCHAR szPath[_MAX_PATH] = "";
	for(size_t i = 0; i < NUM_DIRS; i++)
	{
		if(TrackerDirectories::Instance().m_szDirectoryToSettingsName[i][0] == 0)
			continue;

		_tcscpy(szPath, GetDefaultDirectory(static_cast<Directory>(i)));
		if(bConvertPaths)
		{
			theApp.AbsolutePathToRelative(szPath);
		}
		conf.Write<std::string>("Paths", TrackerDirectories::Instance().m_szDirectoryToSettingsName[i], szPath);

	}
	// Obsolete, since we always write to Keybindings.mkb now.
	// Older versions of OpenMPT 1.18+ will look for this file if this entry is missing, so removing this entry after having read it is kind of backwards compatible.
	conf.Remove("Paths", "Key_Config_File");

#ifndef NO_DSP
	conf.Write<int32>("Effects", "XBassDepth", m_DSPSettings.m_nXBassDepth);
	conf.Write<int32>("Effects", "XBassRange", m_DSPSettings.m_nXBassRange);
#endif
#ifndef NO_REVERB
	conf.Write<int32>("Effects", "ReverbDepth", m_ReverbSettings.m_nReverbDepth);
	conf.Write<int32>("Effects", "ReverbType", m_ReverbSettings.m_nReverbType);
#endif
#ifndef NO_DSP
	conf.Write<int32>("Effects", "ProLogicDepth", m_DSPSettings.m_nProLogicDepth);
	conf.Write<int32>("Effects", "ProLogicDelay", m_DSPSettings.m_nProLogicDelay);
#endif

#ifndef NO_EQ
	conf.Write<EQPreset>("Effects", "EQ_Settings", m_EqSettings);
	conf.Write<EQPreset>("Effects", "EQ_User1", CEQSetupDlg::gUserPresets[0]);
	conf.Write<EQPreset>("Effects", "EQ_User2", CEQSetupDlg::gUserPresets[1]);
	conf.Write<EQPreset>("Effects", "EQ_User3", CEQSetupDlg::gUserPresets[2]);
	conf.Write<EQPreset>("Effects", "EQ_User4", CEQSetupDlg::gUserPresets[3]);
#endif

	if(CMainFrame::m_pAutoSaver != nullptr)
	{
		conf.Write<int32>("AutoSave", "Enabled", CMainFrame::m_pAutoSaver->IsEnabled());
		conf.Write<int32>("AutoSave", "IntervalMinutes", CMainFrame::m_pAutoSaver->GetSaveInterval());
		conf.Write<int32>("AutoSave", "BackupHistory", CMainFrame::m_pAutoSaver->GetHistoryDepth());
		conf.Write<int32>("AutoSave", "UseOriginalPath", CMainFrame::m_pAutoSaver->GetUseOriginalPath());
		_tcscpy(szPath, CMainFrame::m_pAutoSaver->GetPath());
		if(bConvertPaths)
		{
			theApp.AbsolutePathToRelative(szPath);
		}
		conf.Write<std::string>("AutoSave", "Path", szPath);
		conf.Write<CString>("AutoSave", "FileNameTemplate", CMainFrame::m_pAutoSaver->GetFilenameTemplate());
	}

	SaveChords(Chords);

	// Save default macro configuration
	MIDIMacroConfig macros;
	theApp.GetDefaultMidiMacro(macros);
	for(int isfx = 0; isfx < 16; isfx++)
	{
		CHAR snam[8];
		wsprintf(snam, "SF%X", isfx);
		conf.Write<std::string>("Zxx Macros", snam, macros.szMidiSFXExt[isfx]);
	}
	for(int izxx = 0; izxx < 128; izxx++)
	{
		CHAR snam[8];
		wsprintf(snam, "Z%02X", izxx | 0x80);
		conf.Write<std::string>("Zxx Macros", snam, macros.szMidiZXXExt[izxx]);
	}

	conf.Write<std::string>("Misc", "DefaultModType", CSoundFile::GetModSpecifications(defaultModType).fileExtension);

	CMainFrame::GetMainFrame()->SaveBarState("Toolbars");
}


std::vector<uint32> TrackerSettings::GetSampleRates()
//---------------------------------------------------
{
	static const uint32 samplerates [] = {
		192000,
		176400,
		96000,
		88200,
		64000,
		48000,
		44100,
		40000,
		37800,
		33075,
		32000,
		24000,
		22050,
		20000,
		19800,
		16000
	};
	return std::vector<uint32>(samplerates, samplerates + CountOf(samplerates));
}


////////////////////////////////////////////////////////////////////////////////
// Chords

void TrackerSettings::LoadChords(MPTChords &chords)
//-------------------------------------------------
{	
	for(size_t i = 0; i < CountOf(chords); i++)
	{
		uint32 chord;
		if((chord = theApp.GetSettings().Read<int32>("Chords", szDefaultNoteNames[i], -1)) >= 0)
		{
			if((chord & 0xFFFFFFC0) || (!chords[i].notes[0]))
			{
				chords[i].key = (uint8)(chord & 0x3F);
				chords[i].notes[0] = (uint8)((chord >> 6) & 0x3F);
				chords[i].notes[1] = (uint8)((chord >> 12) & 0x3F);
				chords[i].notes[2] = (uint8)((chord >> 18) & 0x3F);
			}
		}
	}
}


void TrackerSettings::SaveChords(MPTChords &chords)
//-------------------------------------------------
{
	for(size_t i = 0; i < CountOf(chords); i++)
	{
		int32 s = (chords[i].key) | (chords[i].notes[0] << 6) | (chords[i].notes[1] << 12) | (chords[i].notes[2] << 18);
		theApp.GetSettings().Write<int32>("Chords", szDefaultNoteNames[i], s);
	}
}


std::string TrackerSettings::IgnoredCCsToString() const
//-----------------------------------------------------
{
	std::string cc;
	bool first = true;
	for(int i = 0; i < 128; i++)
	{
		if(midiIgnoreCCs[i])
		{
			if(!first)
			{
				cc += ",";
			}
			cc += Stringify(i);
			first = false;
		}
	}
	return cc;
}


void TrackerSettings::ParseIgnoredCCs(CString cc)
//-----------------------------------------------
{
	midiIgnoreCCs.reset();
	int curPos = 0;
	CString ccToken= cc.Tokenize(_T(", "), curPos);
	while(ccToken != _T(""))
	{
		int ccNumber = ConvertStrTo<int>(ccToken);
		if(ccNumber >= 0 && ccNumber <= 127)
			midiIgnoreCCs.set(ccNumber);
		ccToken = cc.Tokenize(_T(", "), curPos);
	};
}


void TrackerSettings::SetDefaultDirectory(const LPCTSTR szFilenameFrom, Directory dir, bool bStripFilename)
//---------------------------------------------------------------------------------------------------------
{
	TrackerDirectories::Instance().SetDefaultDirectory(szFilenameFrom, dir, bStripFilename);
}


void TrackerSettings::SetWorkingDirectory(const LPCTSTR szFilenameFrom, Directory dir, bool bStripFilename)
//---------------------------------------------------------------------------------------------------------
{
	TrackerDirectories::Instance().SetDefaultDirectory(szFilenameFrom, dir, bStripFilename);
}


LPCTSTR TrackerSettings::GetDefaultDirectory(Directory dir) const
//---------------------------------------------------------------
{
	return TrackerDirectories::Instance().GetDefaultDirectory(dir);
}


LPCTSTR TrackerSettings::GetWorkingDirectory(Directory dir) const
//---------------------------------------------------------------
{
	return TrackerDirectories::Instance().GetWorkingDirectory(dir);
}


// retrieve / set default directory from given string and store it our setup variables
void TrackerDirectories::SetDirectory(const LPCTSTR szFilenameFrom, Directory dir, TCHAR (&directories)[NUM_DIRS][_MAX_PATH], bool bStripFilename)
//------------------------------------------------------------------------------------------------------------------------------------------------
{
	TCHAR szPath[_MAX_PATH], szDir[_MAX_DIR];

	if(bStripFilename)
	{
		_tsplitpath(szFilenameFrom, szPath, szDir, 0, 0);
		_tcscat(szPath, szDir);
	}
	else
	{
		_tcscpy(szPath, szFilenameFrom);
	}

	TCHAR szOldDir[CountOf(directories[dir])]; // for comparison
	_tcscpy(szOldDir, directories[dir]);

	_tcscpy(directories[dir], szPath);

	// When updating default directory, also update the working directory.
	if(szPath[0] && directories == m_szDefaultDirectory)
	{
		if(_tcscmp(szOldDir, szPath) != 0) // update only if default directory has changed
			SetWorkingDirectory(szPath, dir);
	}
}

void TrackerDirectories::SetDefaultDirectory(const LPCTSTR szFilenameFrom, Directory dir, bool bStripFilename)
//------------------------------------------------------------------------------------------------------------
{
	SetDirectory(szFilenameFrom, dir, m_szDefaultDirectory, bStripFilename);
}


void TrackerDirectories::SetWorkingDirectory(const LPCTSTR szFilenameFrom, Directory dir, bool bStripFilename)
//------------------------------------------------------------------------------------------------------------
{
	SetDirectory(szFilenameFrom, dir, m_szWorkingDirectory, bStripFilename);
}


LPCTSTR TrackerDirectories::GetDefaultDirectory(Directory dir) const
//------------------------------------------------------------------
{
	return m_szDefaultDirectory[dir];
}


LPCTSTR TrackerDirectories::GetWorkingDirectory(Directory dir) const
//------------------------------------------------------------------
{
	return m_szWorkingDirectory[dir];
}



TrackerDirectories::TrackerDirectories()
//--------------------------------------
{
	// Directory Arrays (Default + Last)
	for(size_t i = 0; i < NUM_DIRS; i++)
	{
		if(i == DIR_TUNING) // Hack: Tuning folder is already set so don't reset it.
			continue;
		m_szDefaultDirectory[i][0] = '\0';
		m_szWorkingDirectory[i][0] = '\0';
	}
}


TrackerDirectories::~TrackerDirectories()
//---------------------------------------
{
	return;
}
