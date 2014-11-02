/*
 * vstplug.h
 * ---------
 * Purpose: Plugin handling (loading and processing plugins)
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#pragma once

#ifndef NO_VST
	#define VST_FORCE_DEPRECATED 0
	#include <pluginterfaces/vst2.x/aeffectx.h>			// VST
#endif

#include "../soundlib/Snd_defs.h"
#include "../soundlib/plugins/PluginMixBuffer.h"
#include "../soundlib/plugins/PlugInterface.h"

OPENMPT_NAMESPACE_BEGIN

//#define kBuzzMagic	'Buzz'
#define kDmoMagic	'DXMO'


class CVstPluginManager;
class CVstPlugin;
#ifdef MODPLUG_TRACKER
class CModDoc;
#endif // MODPLUG_TRACKER
class CSoundFile;


struct VSTPluginLib
{
public:
	enum PluginCategory
	{
		// Same plugin categories as defined in VST SDK
		catUnknown = 0,
		catEffect,			// Simple Effect
		catSynth,			// VST Instrument (Synths, samplers,...)
		catAnalysis,		// Scope, Tuner, ...
		catMastering,		// Dynamics, ...
		catSpacializer,		// Panners, ...
		catRoomFx,			// Delays and Reverbs
		catSurroundFx,		// Dedicated surround processor
		catRestoration,		// Denoiser, ...
		catOfflineProcess,	// Offline Process
		catShell,			// Plug-in is container of other plug-ins
		catGenerator,		// Tone Generator, ...
		// Custom categories
		catDMO,				// DirectX media object plugin

		numCategories,
	};

public:
	CVstPlugin *pPluginsList;		// Pointer to first plugin instance (this instance carries pointers to other instances)
	mpt::PathString libraryName;	// Display name
	mpt::PathString dllPath;		// Full path name
	VstInt32 pluginId1;				// Plugin type (kEffectMagic, kDmoMagic)
	VstInt32 pluginId2;				// Plugin unique ID
	PluginCategory category;
	bool isInstrument;
	bool useBridge, shareBridgeInstance;
protected:
	mutable uint8 dllBits;

public:
	VSTPluginLib(const mpt::PathString &dllPath, const mpt::PathString &libraryName)
		: pPluginsList(nullptr),
		libraryName(libraryName), dllPath(dllPath),
		pluginId1(0), pluginId2(0),
		category(catUnknown),
		isInstrument(false), useBridge(false), shareBridgeInstance(false),
		dllBits(0)
	{
	}

	// Check whether a plugin can be hosted inside OpenMPT or requires bridging
	uint8 GetDllBits(bool fromCache = true) const;
	bool IsNative(bool fromCache = true) const { return GetDllBits(fromCache) == sizeof(void *) * CHAR_BIT; }

	void WriteToCache() const;

	uint32 EncodeCacheFlags() const
	{
		// Format: 00000000.00000000.DDDDDDSB.CCCCCCCI
		return (isInstrument ? 1 : 0)
			| (category << 1)
			| (useBridge ? 0x100 : 0)
			| (shareBridgeInstance ? 0x200 : 0)
			| ((dllBits / 8) << 10);
	}

	void DecodeCacheFlags(uint32 flags)
	{
		category = static_cast<PluginCategory>((flags & 0xFF) >> 1);
		if(category >= numCategories)
		{
			category = catUnknown;
		}
		if(flags & 1)
		{
			isInstrument = true;
			category = catSynth;
		}
		if(flags & 0x100)
		{
			useBridge = true;
		}
		if(flags & 0x200)
		{
			shareBridgeInstance = true;
		}
		dllBits = ((flags >> 10) & 0x3F) * 8;
	}
};


OPENMPT_NAMESPACE_END
#ifndef NO_VST
#include "../soundlib/plugins/PluginEventQueue.h"
#include "../soundlib/Mixer.h"
#endif // NO_VST
OPENMPT_NAMESPACE_BEGIN


//=================================
class CVstPlugin: public IMixPlugin
//=================================
{
	friend class CAbstractVstEditor;
	friend class CVstPluginManager;
#ifndef NO_VST
protected:

	enum
	{
		// Number of MIDI events that can be sent to a plugin at once (the internal queue is not affected by this number, it can hold any number of events)
		vstNumProcessEvents = 256,

		// Pitch wheel constants
		vstPitchBendShift	= 12,		// Use lowest 12 bits for fractional part and vibrato flag => 16.11 fixed point precision
		vstPitchBendMask	= (~1),
		vstVibratoFlag		= 1,
	};

	struct VSTInstrChannel
	{
		int32  midiPitchBendPos;		// Current Pitch Wheel position, in 16.11 fixed point format. Lowest bit is used for indicating that vibrato was applied. Vibrato offset itself is not stored in this value.
		uint16 currentProgram;
		uint16 currentBank;
		uint8  noteOnMap[128][MAX_CHANNELS];

		void ResetProgram() { currentProgram = 0; currentBank = 0; }
	};

	CVstPlugin *m_pNext, *m_pPrev;
	HINSTANCE m_hLibrary;
	VSTPluginLib &m_Factory;
	SNDMIXPLUGIN *m_pMixStruct;
	AEffect &m_Effect;
	void (*m_pProcessFP)(AEffect*, float**, float**, VstInt32); //Function pointer to AEffect processReplacing if supported, else process.
	CAbstractVstEditor *m_pEditor;
	CSoundFile &m_SndFile;

	uint32 m_nSampleRate;
	SNDMIXPLUGINSTATE m_MixState;

	double lastBarStartPos;
	float m_fGain;
	PLUGINDEX m_nSlot;
	bool m_bSongPlaying;
	bool m_bPlugResumed;
	bool m_bIsVst2;
	bool m_bIsInstrument;
	bool isInitialized;

	VSTInstrChannel m_MidiCh[16];						// MIDI channel state
	PluginMixBuffer<float, MIXBUFFERSIZE> mixBuffer;	// Float buffers (input and output) for plugins
	mixsample_t m_MixBuffer[MIXBUFFERSIZE * 2 + 2];		// Stereo interleaved
	PluginEventQueue<vstNumProcessEvents> vstEvents;	// MIDI events that should be sent to the plugin

public:
	bool m_bNeedIdle; //rewbs.VSTCompliance
	bool m_bRecordAutomation;
	bool m_bPassKeypressesToPlug;
	bool m_bRecordMIDIOut;
	const bool isBridged;		// True if our built-in plugin bridge is being used.

public:
	CVstPlugin(HINSTANCE hLibrary, VSTPluginLib &factory, SNDMIXPLUGIN &mixPlugin, AEffect &effect, CSoundFile &sndFile);
	virtual ~CVstPlugin();
	void Initialize();

public:
	VSTPluginLib &GetPluginFactory() const { return m_Factory; }
	CVstPlugin *GetNextInstance() const { return m_pNext; }
	bool HasEditor();
	VstInt32 GetNumPrograms();
	PlugParamIndex GetNumParameters();
	VstInt32 GetCurrentProgram();
	VstInt32 GetNumProgramCategories();
	CString GetFormattedProgramName(VstInt32 index);
	bool LoadProgram(mpt::PathString fileName = mpt::PathString());
	bool SaveProgram();
	VstInt32 GetUID() const;
	VstInt32 GetVersion() const;
	// Check if programs should be stored as chunks or parameters
	bool ProgramsAreChunks() const { return (m_Effect.flags & effFlagsProgramChunks) != 0; }
	bool GetParams(float* param, VstInt32 min, VstInt32 max);
	void RandomizeParams(int amount);
	// If true, the plugin produces an output even if silence is being fed into it.
	bool ShouldProcessSilence() { return isInstrument() || ((m_Effect.flags & effFlagsNoSoundInStop) == 0 && Dispatch(effGetTailSize, 0, 0, nullptr, 0.0f) != 1); }
	void ResetSilence() { m_MixState.ResetSilence(); }
#ifdef MODPLUG_TRACKER
	forceinline CModDoc *GetModDoc();
	forceinline const CModDoc *GetModDoc() const;
#endif // MODPLUG_TRACKER
	inline CSoundFile &GetSoundFile() { return m_SndFile; }
	inline const CSoundFile &GetSoundFile() const { return m_SndFile; }
	PLUGINDEX FindSlot();
	void SetSlot(PLUGINDEX slot);
	PLUGINDEX GetSlot();
	void UpdateMixStructPtr(SNDMIXPLUGIN *);

	void SetEditorPos(int32 x, int32 y) { m_pMixStruct->editorX= x; m_pMixStruct->editorY = y; }
	void GetEditorPos(int32 &x, int32 &y) const { x = m_pMixStruct->editorX; y = m_pMixStruct->editorY; }

	void SetCurrentProgram(VstInt32 nIndex);
	PlugParamValue GetParameter(PlugParamIndex nIndex);
	void SetParameter(PlugParamIndex nIndex, PlugParamValue fValue);

	CString GetFormattedParamName(PlugParamIndex param);
	CString GetFormattedParamValue(PlugParamIndex param);
	CString GetParamName(PlugParamIndex param) { return GetParamPropertyString(param, effGetParamName); };
	CString GetParamLabel(PlugParamIndex param) { return GetParamPropertyString(param, effGetParamLabel); };
	CString GetParamDisplay(PlugParamIndex param) { return GetParamPropertyString(param, effGetParamDisplay); };

	VstIntPtr Dispatch(VstInt32 opCode, VstInt32 index, VstIntPtr value, void *ptr, float opt);
	void ToggleEditor();
	void GetPluginType(LPSTR pszType);
	BOOL GetDefaultEffectName(LPSTR pszName);
	CAbstractVstEditor *GetEditor() { return m_pEditor; }
	const CAbstractVstEditor *GetEditor() const { return m_pEditor; }

	void Bypass(bool bypass = true);
	bool IsBypassed() const { return m_pMixStruct->IsBypassed(); };

	bool isInstrument();
	bool CanRecieveMidiEvents();

	size_t GetOutputPlugList(std::vector<CVstPlugin *> &list);
	size_t GetInputPlugList(std::vector<CVstPlugin *> &list);
	size_t GetInputInstrumentList(std::vector<INSTRUMENTINDEX> &list);
	size_t GetInputChannelList(std::vector<CHANNELINDEX> &list);

public:
	void Release();
	void SaveAllParameters();
	void RestoreAllParameters(long nProg=-1);
	void RecalculateGain();
	void Process(float *pOutL, float *pOutR, size_t nSamples);
	float RenderSilence(size_t numSamples);
	bool MidiSend(uint32 dwMidiCode);
	bool MidiSysexSend(const char *message, uint32 length);
	void MidiCC(uint8 nMidiCh, MIDIEvents::MidiCC nController, uint8 nParam, CHANNELINDEX trackChannel);
	void MidiPitchBend(uint8 nMidiCh, int32 increment, int8 pwd);
	void MidiVibrato(uint8 nMidiCh, int32 depth, int8 pwd);
	void MidiCommand(uint8 nMidiCh, uint8 nMidiProg, uint16 wMidiBank, uint16 note, uint16 vol, CHANNELINDEX trackChannel);
	void HardAllNotesOff();
	bool isPlaying(UINT note, UINT midiChn, UINT trackerChn);
	void NotifySongPlaying(bool playing);
	bool IsSongPlaying() const { return m_bSongPlaying; }
	bool IsResumed() {return m_bPlugResumed;}
	void Resume();
	void Suspend();
	void SetDryRatio(UINT param);
	void AutomateParameter(PlugParamIndex param);

	// Check whether a VST parameter can be automated
	bool CanAutomateParameter(PlugParamIndex index);

	void SetZxxParameter(UINT nParam, UINT nValue);
	UINT GetZxxParameter(UINT nParam); //rewbs.smoothVST

protected:
	void MidiPitchBend(uint8 nMidiCh, int32 pitchBendPos);
	// Converts a 14-bit MIDI pitch bend position to our internal pitch bend position representation
	static int32 EncodePitchBendParam(int32 position) { return (position << vstPitchBendShift); }
	// Converts the internal pitch bend position to a 14-bit MIDI pitch bend position
	static int16 DecodePitchBendParam(int32 position) { return static_cast<int16>(position >> vstPitchBendShift); }
	// Apply Pitch Wheel Depth (PWD) to some MIDI pitch bend value.
	static inline void ApplyPitchWheelDepth(int32 &value, int8 pwd);

	bool GetProgramNameIndexed(VstInt32 index, VstIntPtr category, char *text);	//rewbs.VSTpresets

	// Helper function for retreiving parameter name / label / display
	CString GetParamPropertyString(VstInt32 param, VstInt32 opcode);

	// Set up input / output buffers.
	bool InitializeIOBuffers();

	// Process incoming and outgoing VST events.
	void ProcessVSTEvents();
	void ReceiveVSTEvents(const VstEvents *events);

	void ProcessMixOps(float *pOutL, float *pOutR, float *leftPlugOutput, float *rightPlugOutput, size_t nSamples);

	void ReportPlugException(std::wstring text) const;

#else // case: NO_VST
public:
	PlugParamIndex GetNumParameters() { return 0; }
	void ToggleEditor() {}
	bool HasEditor() { return false; }
	void GetPluginType(LPSTR) {}
	VstInt32 GetNumPrograms() { return 0; }
	VstInt32 GetCurrentProgram() { return 0; }
	bool GetProgramNameIndexed(long, long, char*) { return false; }
	CString GetFormattedProgramName(VstInt32) { return ""; }
	void SetParameter(PlugParamIndex, PlugParamValue) {}
	VstInt32 GetUID() const { return 0; }
	VstInt32 GetVersion() const { return 0; }
	bool ShouldProcessSilence() { return false; }
	void ResetSilence() { }

	bool CanAutomateParameter(PlugParamIndex) { return false; }

	CString GetFormattedParamName(PlugParamIndex) { return ""; };
	CString GetFormattedParamValue(PlugParamIndex){ return ""; };
	CString GetParamName(PlugParamIndex) { return ""; };
	CString GetParamLabel(PlugParamIndex) { return ""; };
	CString GetParamDisplay(PlugParamIndex) { return ""; };

	PlugParamValue GetParameter(PlugParamIndex) { return 0; }
	bool LoadProgram(mpt::PathString = mpt::PathString()) { return false; }
	bool SaveProgram() { return false; }
	void SetCurrentProgram(VstInt32) {}
	void SetSlot(PLUGINDEX) {}
	void UpdateMixStructPtr(void*) {}
	void Bypass(bool = true) { }
	bool IsBypassed() const { return false; }
	bool IsSongPlaying() const { return false; }

	void SetEditorPos(int32, int32) { }
	void GetEditorPos(int32 &x, int32 &y) const { x = y = int32_min; }

#endif // NO_VST
};


//=====================
class CVstPluginManager
//=====================
{
#ifndef NO_VST
protected:
	std::vector<VSTPluginLib *> pluginList;

public:
	CVstPluginManager();
	~CVstPluginManager();

	typedef std::vector<VSTPluginLib *>::iterator iterator;
	typedef std::vector<VSTPluginLib *>::const_iterator const_iterator;

	iterator begin() { return pluginList.begin(); }
	const_iterator begin() const { return pluginList.begin(); }
	iterator end() { return pluginList.end(); }
	const_iterator end() const { return pluginList.end(); }
	void reserve(size_t num) { pluginList.reserve(num); }

	bool IsValidPlugin(const VSTPluginLib *pLib) const;
	VSTPluginLib *AddPlugin(const mpt::PathString &dllPath, bool fromCache = true, const bool checkFileExistence = false, std::wstring* const errStr = nullptr);
	bool RemovePlugin(VSTPluginLib *);
	bool CreateMixPlugin(SNDMIXPLUGIN &, CSoundFile &);
	void OnIdle();
	static void ReportPlugException(LPCSTR format,...);
	static void ReportPlugException(const std::wstring &msg);
	static void ReportPlugException(const std::string &msg);

protected:
	void EnumerateDirectXDMOs();
	AEffect *LoadPlugin(const VSTPluginLib &plugin, HINSTANCE &library, bool forceBridge);

public:
	static VstIntPtr VSTCALLBACK MasterCallBack(AEffect *effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float opt);

protected:
	VstIntPtr VstCallback(AEffect *effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float opt);
	VstIntPtr VstFileSelector(bool destructor, VstFileSelect *fileSel, const CVstPlugin *plugin);
	static bool CreateMixPluginProc(SNDMIXPLUGIN &, CSoundFile &);
	VstTimeInfo timeInfo;

public:
	static char s_szHostProductString[64];
	static char s_szHostVendorString[64];
	static VstIntPtr s_nHostVendorVersion;

#else // NO_VST
public:
	VSTPluginLib *AddPlugin(const mpt::PathString &, bool = true, const bool = false, std::wstring* const = nullptr) { return 0; }

	const VSTPluginLib **begin() const { return nullptr; }
	const VSTPluginLib **end() const { return nullptr; }
	void reserve(size_t) { }

	void OnIdle() {}
#endif // NO_VST
};


OPENMPT_NAMESPACE_END
