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


std::string SettingsModTypeToString(MODTYPE modtype)
//--------------------------------------------------
{
	return CSoundFile::GetModSpecifications(modtype).fileExtension;
}

MODTYPE SettingsStringToModType(const std::string &str)
//-----------------------------------------------------
{
	return CModSpecifications::ExtensionToType(str);
}


static ResamplingMode GetDefaultResamplerMode()
//---------------------------------------------
{
	ResamplingMode result = CResamplerSettings().SrcMode;
#ifdef ENABLE_ASM
	// rough heuristic to select less cpu consuming defaults for old CPUs
	if(GetProcSupport() & PROCSUPPORT_SSE)
	{
		result = SRCMODE_POLYPHASE;
	} else if(GetProcSupport() & PROCSUPPORT_MMX)
	{
		result = SRCMODE_SPLINE;
	} else
	{
		result = SRCMODE_LINEAR;
	}
#else
	// just use a sane default
	result = CResamplerSettings().SrcMode;
#endif
	return result;
}


static uint32 GetDefaultPatternSetup()
//------------------------------------
{
	return PATTERN_PLAYNEWNOTE | PATTERN_EFFECTHILIGHT
		| PATTERN_SMALLFONT | PATTERN_CENTERROW | PATTERN_DRAGNDROPEDIT
		| PATTERN_FLATBUTTONS | PATTERN_NOEXTRALOUD | PATTERN_2NDHIGHLIGHT
		| PATTERN_STDHIGHLIGHT | PATTERN_SHOWPREVIOUS | PATTERN_CONTSCROLL
		| PATTERN_SYNCMUTE | PATTERN_AUTODELAY | PATTERN_NOTEFADE
		| PATTERN_LARGECOMMENTS | PATTERN_SHOWDEFAULTVOLUME;
}


static uint32 GetDefaultUndoBufferSize()
//--------------------------------------
{
	MEMORYSTATUS gMemStatus;
	MemsetZero(gMemStatus);
	GlobalMemoryStatus(&gMemStatus);
	// Allow allocations of at least 16MB
	if(gMemStatus.dwTotalPhys < 16*1024*1024)
		gMemStatus.dwTotalPhys = 16*1024*1024;
	return std::max<uint32>(gMemStatus.dwTotalPhys / 10, 1 << 20);;
}


TrackerSettings::TrackerSettings(SettingsContainer &conf)
//-------------------------------------------------------
	: conf(conf)
	// Version
	, RegVersion(conf, "Settings", "Version", 0)
	, IniVersion(conf, "Version", "Version", "")
	, gcsPreviousVersion(GetStoredVersion(IniVersion, RegVersion))
	, gcsInstallGUID(conf, "Version", "InstallGUID", "")
	// Window
	, WindowMaximized_DEPRECATED(conf, "Window", "Maximized", false)
	// Display
	, m_ShowSplashScreen(conf, "Display", "ShowSplashScreen", true)
	, gbMdiMaximize(conf, SettingPath("Display", "MDIMaximize", "Window", "MDIMaximize"), true)
	, glTreeSplitRatio(conf, SettingPath("Display", "MDITreeRatio", "Window", "MDITreeRatio"), 128)
	, glTreeWindowWidth(conf, SettingPath("Display", "MDITreeWidth", "Window", "MDITreeWidth"), 160)
	, glGeneralWindowHeight(conf, SettingPath("Display", "MDIGeneralHeight", "Window", "MDIGeneralHeight"), 178)
	, glPatternWindowHeight(conf, SettingPath("Display", "MDIPatternHeight", "Window", "MDIPatternHeight"), 152)
	, glSampleWindowHeight(conf, SettingPath("Display", "MDISampleHeight", "Window", "MDISampleHeight"), 188)
	, glInstrumentWindowHeight(conf, SettingPath("Display", "MDIInstrumentHeight", "Window", "MDIInstrumentHeight"), 300)
	, glCommentsWindowHeight(conf, SettingPath("Display", "MDICommentsHeight", "Window", "MDICommentsHeight"), 288)
	, glGraphWindowHeight(conf, SettingPath("Display", "MDIGraphHeight", "Window", "MDIGraphHeight"), 288)
	, gnPlugWindowX(conf, SettingPath("Display", "PlugSelectWindowX", "", "PlugSelectWindowX"), 243)
	, gnPlugWindowY(conf, SettingPath("Display", "PlugSelectWindowY", "", "PlugSelectWindowY"), 273)
	, gnPlugWindowWidth(conf, SettingPath("Display", "PlugSelectWindowWidth", "", "PlugSelectWindowWidth"), 370)
	, gnPlugWindowHeight(conf, SettingPath("Display", "PlugSelectWindowHeight", "", "PlugSelectWindowHeight"), 332)
	, gnPlugWindowLast(conf, SettingPath("Display", "PlugSelectWindowLast", "", "PlugSelectWindowLast"), 0)
	, gnMsgBoxVisiblityFlags(conf, "Display", "MDIGraphHeight", uint32_max)
	, VuMeterUpdateInterval(conf, "Display", "VuMeterUpdateInterval", 15)
	// Misc
	, gbShowHackControls(conf, "Misc", "ShowHackControls", false)
	, defaultModType(conf, "Misc", "DefaultModType", MOD_TYPE_IT)
	, DefaultPlugVolumeHandling(conf, "Misc", "DefaultPlugVolumeHandling", PLUGIN_VOLUMEHANDLING_IGNORE)
	, autoApplySmoothFT2Ramping(conf, "Misc", "SmoothFT2Ramping", false)
	// Sound Settings
	, m_MorePortaudio(conf, "Sound Settings", "MorePortaudio", false)
	, m_nWaveDevice(conf, "Sound Settings", "WaveDevice", SNDDEV_BUILD_ID(0, SNDDEV_WAVEOUT))
	, m_BufferLength_DEPRECATED(conf, SettingPath("Sound Settings", "BufferLength", "", "BufferLength"), 50)
	, m_LatencyMS(conf, "Sound Settings", "Latency", SNDDEV_DEFAULT_LATENCY_MS)
	, m_UpdateIntervalMS(conf, "Sound Settings", "UpdateInterval", SNDDEV_DEFAULT_UPDATEINTERVAL_MS)
	, m_SampleFormat(conf, SettingPath("Sound Settings", "BitsPerSample", "", "BitsPerSample"), SampleFormatInt16)
	, m_SoundDeviceExclusiveMode(conf, "Sound Settings", "ExclusiveMode", false)
	, m_SoundDeviceBoostThreadPriority(conf, "Sound Settings", "BoostThreadPriority", true)
	, MixerMaxChannels(conf, SettingPath("Sound Settings", "MixChannels", "", "MixChannels"), MixerSettings().m_nMaxMixChannels)
	, MixerDSPMask(conf, SettingPath("Sound Settings", "Quality", "", "Quality"), MixerSettings().DSPMask)
	, MixerFlags(conf, SettingPath("Sound Settings", "SoundSetup", "", "SoundSetup"), MixerSettings().MixerFlags)
	, MixerSamplerate(conf, SettingPath("Sound Settings", "Mixing_Rate", "", "Mixing_Rate"), MixerSettings().gdwMixingFreq)
	, MixerOutputChannels(conf, SettingPath("Sound Settings", "ChannelMode", "", "ChannelMode"), MixerSettings().gnChannels)
	, MixerPreAmp(conf, SettingPath("Sound Settings", "PreAmp", "", "PreAmp"), MixerSettings().m_nPreAmp)
	, MixerStereoSeparation(conf, SettingPath("Sound Settings", "StereoSeparation", "", "StereoSeparation"), MixerSettings().m_nStereoSeparation)
	, MixerVolumeRampUpSamples(conf, SettingPath("Sound Settings", "VolumeRampUpSamples", "", "BitsPerSample"), MixerSettings().glVolumeRampUpSamples)
	, MixerVolumeRampDownSamples(conf, SettingPath("Sound Settings", "VolumeRampDownSamples", "", "BitsPerSample"), MixerSettings().glVolumeRampDownSamples)
	, MixerVolumeRampSamples_DEPRECATED(conf, SettingPath("Sound Settings", "VolumeRampSamples", "", "VolumeRampSamples"), 42)
	, ResamplerMode(conf, SettingPath("Sound Settings", "SrcMode", "", "SrcMode"), GetDefaultResamplerMode())
	, ResamplerSubMode(conf, SettingPath("Sound Settings", "XMMSModplugResamplerWFIRType", "", "XMMSModplugResamplerWFIRType"), CResamplerSettings().gbWFIRType)
	, ResamplerCutoffPercent(conf, SettingPath("Sound Settings", "ResamplerWFIRCutoff", "", "ResamplerWFIRCutoff"), Util::Round<int32>(CResamplerSettings().gdWFIRCutoff * 100.0))
	// MIDI Settings
	, m_nMidiDevice(conf, SettingPath("MIDI Settings", "MidiDevice", "", "MidiDevice"), 0)
	, m_dwMidiSetup(conf, SettingPath("MIDI Settings", "MidiSetup", "", "MidiSetup"), MIDISETUP_RECORDVELOCITY | MIDISETUP_RECORDNOTEOFF | MIDISETUP_TRANSPOSEKEYBOARD | MIDISETUP_MIDITOPLUG)
	, aftertouchBehaviour(conf, "MIDI Settings", "AftertouchBehaviour", atDoNotRecord)
	, midiVelocityAmp(conf, "MIDI Settings", "MidiVelocityAmp", 100)
	, midiIgnoreCCs(conf, "MIDI Settings", "IgnoredCCs", std::bitset<128>())
	, midiImportSpeed(conf, SettingPath("MIDI Settings", "MidiImportSpeed", "", "MidiImportSpeed"), 3)
	, midiImportPatternLen(conf, SettingPath("MIDI Settings", "MidiImportPatLen", "", "MidiImportPatLen"), 128)
	// Pattern Editor
	, gbLoopSong(conf, SettingPath("Pattern Editor", "LoopSong", "", "LoopSong"), true)
	, gnPatternSpacing(conf, "Pattern Editor", "Spacing", 0)
	, gbPatternVUMeters(conf, "Pattern Editor", "VU-Meters", false)
	, gbPatternPluginNames(conf, "Pattern Editor", "Plugin-Names", true)
	, gbPatternRecord(conf, "Pattern Editor", "Record", true)
	, m_dwPatternSetup(conf, SettingPath("Pattern Editor", "PatternSetup", "", "PatternSetup"), GetDefaultPatternSetup())
	, m_nRowHighlightMeasures(conf, SettingPath("Pattern Editor", "RowSpacing", "", "RowSpacing"), 16)
	, m_nRowHighlightBeats(conf, SettingPath("Pattern Editor", "RowSpacing2", "", "RowSpacing2"), 4)
	, recordQuantizeRows(conf, "Pattern Editor", "RecordQuantize", 0)
	, gnAutoChordWaitTime(conf, SettingPath("Pattern Editor", "AutoChordWaitTime", "", "AutoChordWaitTime"), 60)
	, orderlistMargins(conf, "Pattern Editor", "DefaultSequenceMargins", 0)
	, rowDisplayOffset(conf, "Pattern Editor", "RowDisplayOffset", 0)
	// Sample Editor
	, m_SampleUndoMaxBufferMB(conf, "Sample Editor", "UndoBufferSize", GetDefaultUndoBufferSize() >> 20)
	, m_MayNormalizeSamplesOnLoad(conf, "Sample Editor", "MayNormalizeSamplesOnLoad", true)
{
	// Effects
#ifndef NO_DSP
	m_DSPSettings.m_nXBassDepth = conf.Read<int32>(SettingPath("Effects", "XBassDepth", "", "XBassDepth"), m_DSPSettings.m_nXBassDepth);
	m_DSPSettings.m_nXBassRange = conf.Read<int32>(SettingPath("Effects", "XBassRange", "", "XBassRange"), m_DSPSettings.m_nXBassRange);
#endif
#ifndef NO_REVERB
	m_ReverbSettings.m_nReverbDepth = conf.Read<int32>(SettingPath("Effects", "ReverbDepth", "", "ReverbDepth"), m_ReverbSettings.m_nReverbDepth);
	m_ReverbSettings.m_nReverbType = conf.Read<int32>(SettingPath("Effects", "ReverbType", "", "ReverbType"), m_ReverbSettings.m_nReverbType);
#endif
#ifndef NO_DSP
	m_DSPSettings.m_nProLogicDepth = conf.Read<int32>(SettingPath("Effects", "ProLogicDepth", "", "ProLogicDepth"), m_DSPSettings.m_nProLogicDepth);
	m_DSPSettings.m_nProLogicDelay = conf.Read<int32>(SettingPath("Effects", "ProLogicDelay", "", "ProLogicDelay"), m_DSPSettings.m_nProLogicDelay);
#endif
#ifndef NO_EQ
	m_EqSettings = conf.Read<EQPreset>(SettingPath("Effects", "EQ_Settings", "", "EQ_Settings"), CEQSetupDlg::gEQPresets[0]);
	CEQSetupDlg::gUserPresets[0] = conf.Read<EQPreset>(SettingPath("Effects", "EQ_User1", "", "EQ_User1"), CEQSetupDlg::gUserPresets[0]);
	CEQSetupDlg::gUserPresets[1] = conf.Read<EQPreset>(SettingPath("Effects", "EQ_User2", "", "EQ_User2"), CEQSetupDlg::gUserPresets[1]);
	CEQSetupDlg::gUserPresets[2] = conf.Read<EQPreset>(SettingPath("Effects", "EQ_User3", "", "EQ_User3"), CEQSetupDlg::gUserPresets[2]);
	CEQSetupDlg::gUserPresets[3] = conf.Read<EQPreset>(SettingPath("Effects", "EQ_User4", "", "EQ_User4"), CEQSetupDlg::gUserPresets[3]);
#endif
	// Display (Colors)
	GetDefaultColourScheme(rgbCustomColors);
	for(int ncol = 0; ncol < MAX_MODCOLORS; ncol++)
	{
		const std::string colorName = mpt::String::Format("Color%02d", ncol);
		rgbCustomColors[ncol] = conf.Read<uint32>(SettingPath("Display", colorName, "Window", colorName), rgbCustomColors[ncol]);
	}
	// AutoSave
	CMainFrame::m_pAutoSaver->SetEnabled(conf.Read<bool>(SettingPath("AutoSave", "Enabled", "", "AutoSave_Enabled"), CMainFrame::m_pAutoSaver->IsEnabled()));
	CMainFrame::m_pAutoSaver->SetSaveInterval(conf.Read<int32>(SettingPath("AutoSave", "IntervalMinutes", "", "AutoSave_IntervalMinutes"), CMainFrame::m_pAutoSaver->GetSaveInterval()));
	CMainFrame::m_pAutoSaver->SetHistoryDepth(conf.Read<int32>(SettingPath("AutoSave", "BackupHistory", "", "AutoSave_BackupHistory"), CMainFrame::m_pAutoSaver->GetHistoryDepth()));
	CMainFrame::m_pAutoSaver->SetUseOriginalPath(conf.Read<bool>(SettingPath("AutoSave", "UseOriginalPath", "", "AutoSave_UseOriginalPath"), CMainFrame::m_pAutoSaver->GetUseOriginalPath()));
	CMainFrame::m_pAutoSaver->SetPath(theApp.RelativePathToAbsolute(conf.Read<CString>(SettingPath("AutoSave", "Path", "", "AutoSave_Path"), CMainFrame::m_pAutoSaver->GetPath())));
	CMainFrame::m_pAutoSaver->SetFilenameTemplate(conf.Read<CString>(SettingPath("AutoSave", "FileNameTemplate", "", "AutoSave_FileNameTemplate"), CMainFrame::m_pAutoSaver->GetFilenameTemplate()));


	// init old and messy stuff:

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


	// load old and messy stuff:

	LoadSettings();


	// Fixups:
	// -------

	const MptVersion::VersionNum storedVersion = gcsPreviousVersion;

	// Version
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

	// Window
	if(storedVersion < MAKE_VERSION_NUMERIC(1,17,02,40))
	{
		if(WindowMaximized_DEPRECATED)
		{
			theApp.m_nCmdShow = SW_SHOWMAXIMIZED;
		}
	}
	conf.Remove(WindowMaximized_DEPRECATED.GetPath());

	// Sound Settings
	if(storedVersion < MAKE_VERSION_NUMERIC(1,21,01,26))
	{
		if(m_BufferLength_DEPRECATED != 0)
		{
			if(m_BufferLength_DEPRECATED < 1) m_BufferLength_DEPRECATED = 1; // 1ms
			if(m_BufferLength_DEPRECATED > 1000) m_BufferLength_DEPRECATED = 1000; // 1sec
			if(SNDDEV_GET_TYPE(m_nWaveDevice) == SNDDEV_ASIO)
			{
				m_LatencyMS = m_BufferLength_DEPRECATED;
				m_UpdateIntervalMS = m_BufferLength_DEPRECATED / 8;
			} else
			{
				m_LatencyMS = m_BufferLength_DEPRECATED * 3;
				m_UpdateIntervalMS = m_BufferLength_DEPRECATED / 8;
			}
		}
		conf.Remove(m_BufferLength_DEPRECATED.GetPath());
	}
	if(storedVersion < MAKE_VERSION_NUMERIC(1,21,01,26))
	{
		MixerFlags = MixerFlags & ~OLD_SOUNDSETUP_REVERSESTEREO;
	}
	if(storedVersion < MAKE_VERSION_NUMERIC(1,22,01,03))
	{
		m_SoundDeviceExclusiveMode = ((MixerFlags & OLD_SOUNDSETUP_SECONDARY) == 0);
		MixerFlags = MixerFlags & ~OLD_SOUNDSETUP_SECONDARY;
	}
	if(storedVersion < MAKE_VERSION_NUMERIC(1,22,01,03))
	{
		m_SoundDeviceBoostThreadPriority = ((MixerFlags & OLD_SOUNDSETUP_NOBOOSTTHREADPRIORITY) == 0);
		MixerFlags = MixerFlags & ~OLD_SOUNDSETUP_NOBOOSTTHREADPRIORITY;
	}
	if(storedVersion < MAKE_VERSION_NUMERIC(1,20,00,22))
	{
		MixerVolumeRampUpSamples = MixerVolumeRampSamples_DEPRECATED.Get();
		MixerVolumeRampDownSamples = MixerVolumeRampSamples_DEPRECATED.Get();
		conf.Remove(MixerVolumeRampSamples_DEPRECATED.GetPath());
	}
#ifndef NO_ASIO
	CASIODevice::baseChannel = conf.Read<int32>("Sound Settings", "ASIOBaseChannel", CASIODevice::baseChannel);
#endif // NO_ASIO

	// Misc
	if(defaultModType == MOD_TYPE_NONE)
	{
		defaultModType = MOD_TYPE_IT;
	}

	// MIDI Settings
	if((m_dwMidiSetup & 0x40) != 0 && storedVersion < MAKE_VERSION_NUMERIC(1,20,00,86))
	{
		// This flag used to be "amplify MIDI Note Velocity" - with a fixed amplification factor of 2.
		midiVelocityAmp = 200;
		m_dwMidiSetup = m_dwMidiSetup & ~0x40;
	}

	// Pattern Editor
	if(storedVersion < MAKE_VERSION_NUMERIC(1,17,02,50))
	{
		m_dwPatternSetup = m_dwPatternSetup | PATTERN_NOTEFADE;
	}
	if(storedVersion < MAKE_VERSION_NUMERIC(1,17,03,01))
	{
		m_dwPatternSetup = m_dwPatternSetup | PATTERN_RESETCHANNELS;
	}
	if(storedVersion < MAKE_VERSION_NUMERIC(1,19,00,07))
	{
		m_dwPatternSetup = m_dwPatternSetup & ~0x800;					// this was previously deprecated and is now used for something else
	}
	if(storedVersion < MAKE_VERSION_NUMERIC(1,20,00,04))
	{
		m_dwPatternSetup = m_dwPatternSetup & ~0x200000;				// dito
	}
	if(storedVersion < MAKE_VERSION_NUMERIC(1,20,00,07))
	{
		m_dwPatternSetup = m_dwPatternSetup & ~0x400000;				// dito
	}
	if(storedVersion < MAKE_VERSION_NUMERIC(1,20,00,39))
	{
		m_dwPatternSetup = m_dwPatternSetup & ~0x10000000;			// dito
	}

	// Effects
	FixupEQ(&m_EqSettings);
	FixupEQ(&CEQSetupDlg::gUserPresets[0]);
	FixupEQ(&CEQSetupDlg::gUserPresets[1]);
	FixupEQ(&CEQSetupDlg::gUserPresets[2]);
	FixupEQ(&CEQSetupDlg::gUserPresets[3]);

	// Last fixup: update config version
	IniVersion = MptVersion::str;
	conf.Remove("Settings", "Version");

	// Write updated settings
	conf.Flush();

}


DWORD TrackerSettings::GetSoundDeviceFlags() const
//------------------------------------------------
{
	return (m_SoundDeviceExclusiveMode ? SNDDEV_OPTIONS_EXCLUSIVE : 0)
		| (m_SoundDeviceBoostThreadPriority ? SNDDEV_OPTIONS_BOOSTTHREADPRIORITY : 0);
}

void TrackerSettings::SetSoundDeviceFlags(DWORD flags)
//----------------------------------------------------
{
	m_SoundDeviceExclusiveMode = (flags & SNDDEV_OPTIONS_EXCLUSIVE) ? true : false;
	m_SoundDeviceBoostThreadPriority = (flags & SNDDEV_OPTIONS_BOOSTTHREADPRIORITY) ? true : false;
}


MixerSettings TrackerSettings::GetMixerSettings() const
//-----------------------------------------------------
{
	MixerSettings settings;
	settings.m_nMaxMixChannels = MixerMaxChannels;
	settings.DSPMask = MixerDSPMask;
	settings.MixerFlags = MixerFlags;
	settings.gdwMixingFreq = MixerSamplerate;
	settings.gnChannels = MixerOutputChannels;
	settings.m_nPreAmp = MixerPreAmp;
	settings.m_nStereoSeparation = MixerStereoSeparation;
	settings.glVolumeRampUpSamples = MixerVolumeRampUpSamples;
	settings.glVolumeRampDownSamples = MixerVolumeRampDownSamples;
	return settings;
}

void TrackerSettings::SetMixerSettings(const MixerSettings &settings)
//-------------------------------------------------------------------
{
	MixerMaxChannels = settings.m_nMaxMixChannels;
	MixerDSPMask = settings.DSPMask;
	MixerFlags = settings.MixerFlags;
	MixerSamplerate = settings.gdwMixingFreq;
	MixerOutputChannels = settings.gnChannels;
	MixerPreAmp = settings.m_nPreAmp;
	MixerStereoSeparation = settings.m_nStereoSeparation;
	MixerVolumeRampUpSamples = settings.glVolumeRampUpSamples;
	MixerVolumeRampDownSamples = settings.glVolumeRampDownSamples;
}


CResamplerSettings TrackerSettings::GetResamplerSettings() const
//--------------------------------------------------------------
{
	CResamplerSettings settings;
	settings.SrcMode = ResamplerMode;
	settings.gbWFIRType = ResamplerSubMode;
	settings.gdWFIRCutoff = ResamplerCutoffPercent * 0.01;
	return settings;
}

void TrackerSettings::SetResamplerSettings(const CResamplerSettings &settings)
//----------------------------------------------------------------------------
{
	ResamplerMode = settings.SrcMode;
	ResamplerSubMode = settings.gbWFIRType;
	ResamplerCutoffPercent = Util::Round<int32>(settings.gdWFIRCutoff * 100.0);
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

	CString storedVersion = IniVersion.Get().c_str();

	// If version number stored in INI is 1.17.02.40 or later, always load setting from INI file.
	// If it isn't, try loading from Registry first, then from the INI file.
	if (storedVersion >= "1.17.02.40" || !LoadRegistrySettings())
	{
		LoadINISettings();
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


void TrackerSettings::LoadINISettings()
//-------------------------------------
{

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

}


void TrackerSettings::FixupEQ(EQPreset *pEqSettings)
//--------------------------------------------------
{
	for(UINT i=0; i<MAX_EQ_BANDS; i++)
	{
		if(pEqSettings->Gains[i] > 32)
			pEqSettings->Gains[i] = 16;
		if((pEqSettings->Freqs[i] < 100) || (pEqSettings->Freqs[i] > 10000))
			pEqSettings->Freqs[i] = CEQSetupDlg::gEQPresets[0].Freqs[i];
	}
	mpt::String::SetNullTerminator(pEqSettings->szName);
}


bool TrackerSettings::LoadRegistrySettings()
//------------------------------------------
{

	HKEY key;

	if(RegOpenKeyEx(HKEY_CURRENT_USER, "Software\\Olivier Lapicque\\ModPlug Tracker", 0, KEY_READ, &key) == ERROR_SUCCESS)
	{
		DWORD dwREG_SZ = REG_SZ;
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
		RegCloseKey(key);
	} else
	{
		return false;
	}

	return true;
}


void TrackerSettings::SaveSettings()
//----------------------------------
{

	WINDOWPLACEMENT wpl;
	wpl.length = sizeof(WINDOWPLACEMENT);
	CMainFrame::GetMainFrame()->GetWindowPlacement(&wpl);
	conf.Write<WINDOWPLACEMENT>("Display", "WindowPlacement", wpl);

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

	conf.Write<uint32>("Pattern Editor", "NumClipboards", PatternClipboard::GetClipboardSize());

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


	// Effects
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

	// Display (Colors)
	for(int ncol = 0; ncol < MAX_MODCOLORS; ncol++)
	{
		conf.Write<uint32>("Display", mpt::String::Format("Color%02d", ncol), rgbCustomColors[ncol]);
	}

	// AutoSave
	conf.Write<bool>("AutoSave", "Enabled", CMainFrame::m_pAutoSaver->IsEnabled());
	conf.Write<int32>("AutoSave", "IntervalMinutes", CMainFrame::m_pAutoSaver->GetSaveInterval());
	conf.Write<int32>("AutoSave", "BackupHistory", CMainFrame::m_pAutoSaver->GetHistoryDepth());
	conf.Write<bool>("AutoSave", "UseOriginalPath", CMainFrame::m_pAutoSaver->GetUseOriginalPath());
	conf.Write<CString>("AutoSave", "Path", theApp.AbsolutePathToRelative(CMainFrame::m_pAutoSaver->GetPath()));
	conf.Write<CString>("AutoSave", "FileNameTemplate", CMainFrame::m_pAutoSaver->GetFilenameTemplate());


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
		if((chord = conf.Read<int32>("Chords", szDefaultNoteNames[i], -1)) >= 0)
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
		conf.Write<int32>("Chords", szDefaultNoteNames[i], s);
	}
}


std::string IgnoredCCsToString(const std::bitset<128> &midiIgnoreCCs)
//-------------------------------------------------------------------
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


std::bitset<128> StringToIgnoredCCs(const std::string &in)
//--------------------------------------------------------
{
	CString cc = in.c_str();
	std::bitset<128> midiIgnoreCCs;
	midiIgnoreCCs.reset();
	int curPos = 0;
	CString ccToken = cc.Tokenize(_T(", "), curPos);
	while(ccToken != _T(""))
	{
		int ccNumber = ConvertStrTo<int>(ccToken);
		if(ccNumber >= 0 && ccNumber <= 127)
			midiIgnoreCCs.set(ccNumber);
		ccToken = cc.Tokenize(_T(", "), curPos);
	}
	return midiIgnoreCCs;
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
