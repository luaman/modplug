/*
 * Vstplug.cpp
 * -----------
 * Purpose: Plugin handling / processing
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"
#include "Vstplug.h"
#include "VstPresets.h"
#ifdef MODPLUG_TRACKER
#include "Moddoc.h"
#include "Mainfrm.h"
#include "InputHandler.h"
#endif // MODPLUG_TRACKER
#include "../soundlib/Sndfile.h"
#include "AbstractVstEditor.h"
#include "VstEditor.h"
#include "DefaultVstEditor.h"
#include "../soundlib/MIDIEvents.h"
#include "MIDIMappingDialog.h"
#include "../common/StringFixer.h"
#include "../common/mptFileIO.h"
#include "../soundlib/FileReader.h"
#include "FileDialog.h"


OPENMPT_NAMESPACE_BEGIN


#ifndef NO_VST

//#define VST_LOG

VstIntPtr CVstPluginManager::VstCallback(AEffect *effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float /*opt*/)
//-----------------------------------------------------------------------------------------------------------------------------------
{
	#ifdef VST_LOG
	Log("VST plugin to host: Eff: 0x%.8X, Opcode = %d, Index = %d, Value = %d, PTR = %.8X, OPT = %.3f\n",(int)effect, opcode,index,value,(int)ptr,opt);
	#endif

	enum enmHostCanDo
	{
		HostDoNotKnow	= 0,
		HostCanDo		= 1,
		HostCanNotDo	= -1
	};

	CVstPlugin *pVstPlugin = nullptr;
	if(effect != nullptr)
	{
		pVstPlugin = FromVstPtr<CVstPlugin>(effect->resvd1);
	}

	switch(opcode)
	{
	// Called when plugin param is changed via gui
	case audioMasterAutomate:
		// Strum Acoustic GS-1 and Strum Electric GS-1 send audioMasterAutomate during effOpen (WTF #1),
		// but when sending back effCanBeAutomated, they just crash (WTF #2).
		// As a consequence, just generally forbid this action while the plugin is not fully initialized yet.
		if(pVstPlugin != nullptr && pVstPlugin->isInitialized && pVstPlugin->CanAutomateParameter(index))
		{
			// This parameter can be automated. Ugo Motion constantly sends automation callback events for parameters that cannot be automated...
			pVstPlugin->AutomateParameter((PlugParamIndex)index);
		}
		return 0;

	// Called when plugin asks for VST version supported by host
	case audioMasterVersion:
		return kVstVersion;

	// Returns the unique id of a plug that's currently loading
	// We don't support shell plugins currently, so we only support one effect ID as well.
	case audioMasterCurrentId:
		return (effect != nullptr) ? effect->uniqueID : 0;

	// Call application idle routine (this will call effEditIdle for all open editors too)
	case audioMasterIdle:
		OnIdle();
		return 0;

	// Inquire if an input or output is beeing connected; index enumerates input or output counting from zero,
	// value is 0 for input and != 0 otherwise. note: the return value is 0 for <true> such that older versions
	// will always return true.
	case audioMasterPinConnected:
		if (value)	//input:
			return (index < 2) ? 0 : 1;		//we only support up to 2 inputs. Remember: 0 means yes.
		else		//output:
			return (index < 2) ? 0 : 1;		//2 outputs max too

	//---from here VST 2.0 extension opcodes------------------------------------------------------

	// <value> is a filter which is currently ignored - DEPRECATED in VST 2.4
	case audioMasterWantMidi:
		return 1;

	// returns const VstTimeInfo* (or 0 if not supported)
	// <value> should contain a mask indicating which fields are required
	case audioMasterGetTime:
		MemsetZero(timeInfo);

		if(pVstPlugin)
		{
			timeInfo.sampleRate = pVstPlugin->m_nSampleRate;
			CSoundFile &sndFile = pVstPlugin->GetSoundFile();
			if(pVstPlugin->IsSongPlaying())
			{
				timeInfo.flags |= kVstTransportPlaying;
				if(pVstPlugin->GetSoundFile().m_SongFlags[SONG_PATTERNLOOP]) timeInfo.flags |= kVstTransportCycleActive;
				timeInfo.samplePos = sndFile.GetTotalSampleCount();
				if(sndFile.HasPositionChanged())
				{
					timeInfo.flags |= kVstTransportChanged;
					pVstPlugin->lastBarStartPos = -1.0;
				}
			} else
			{
				timeInfo.flags |= kVstTransportChanged; //just stopped.
				timeInfo.samplePos = 0;
				pVstPlugin->lastBarStartPos = -1.0;
			}
			if((value & kVstNanosValid))
			{
				timeInfo.flags |= kVstNanosValid;
				timeInfo.nanoSeconds = timeGetTime() * 1000000;
			}
			if((value & kVstPpqPosValid))
			{
				timeInfo.flags |= kVstPpqPosValid;
				if (timeInfo.flags & kVstTransportPlaying)
				{
					timeInfo.ppqPos = (timeInfo.samplePos / timeInfo.sampleRate) * (sndFile.GetCurrentBPM() / 60.0);
				} else
				{
					timeInfo.ppqPos = 0;
				}

				if((pVstPlugin->GetSoundFile().m_PlayState.m_nRow % pVstPlugin->GetSoundFile().m_PlayState.m_nCurrentRowsPerMeasure) == 0)
				{
					pVstPlugin->lastBarStartPos = std::floor(timeInfo.ppqPos);
				}
				if(pVstPlugin->lastBarStartPos >= 0)
				{
					timeInfo.barStartPos = pVstPlugin->lastBarStartPos;
					timeInfo.flags |= kVstBarsValid;
				}
			}
			if((value & kVstTempoValid))
			{
				timeInfo.tempo = sndFile.GetCurrentBPM();
				if (timeInfo.tempo)
				{
					timeInfo.flags |= kVstTempoValid;
				}
			}
			if((value & kVstTimeSigValid))
			{
				timeInfo.flags |= kVstTimeSigValid;

				// Time signature. numerator = rows per beats / rows pear measure (should sound somewhat logical to you).
				// the denominator is a bit more tricky, since it cannot be set explicitely. so we just assume quarters for now.
				timeInfo.timeSigNumerator = sndFile.m_PlayState.m_nCurrentRowsPerMeasure / std::max(sndFile.m_PlayState.m_nCurrentRowsPerBeat, ROWINDEX(1));
				timeInfo.timeSigDenominator = 4; //gcd(pSndFile->m_nCurrentRowsPerMeasure, pSndFile->m_nCurrentRowsPerBeat);
			}
		}
		return ToVstPtr(&timeInfo);

	// Receive MIDI events from plugin
	case audioMasterProcessEvents:
		if(pVstPlugin != nullptr && ptr != nullptr)
		{
			pVstPlugin->ReceiveVSTEvents(reinterpret_cast<VstEvents *>(ptr));
			return 1;
		}
		break;

	// DEPRECATED in VST 2.4
	case audioMasterSetTime:
		Log("VST plugin to host: Set Time\n");
		break;

	// returns tempo (in bpm * 10000) at sample frame location passed in <value> - DEPRECATED in VST 2.4
	case audioMasterTempoAt:
		//Screw it! Let's just return the tempo at this point in time (might be a bit wrong).
		if (pVstPlugin != nullptr)
		{
			return (VstInt32)(pVstPlugin->GetSoundFile().GetCurrentBPM() * 10000);
		}
		return (VstInt32)(125 * 10000);

	// parameters - DEPRECATED in VST 2.4
	case audioMasterGetNumAutomatableParameters:
		//Log("VST plugin to host: Get Num Automatable Parameters\n");
		if(pVstPlugin != nullptr)
		{
			return pVstPlugin->GetNumParameters();
		}
		break;

	// Apparently, this one is broken in VST SDK anyway. - DEPRECATED in VST 2.4
	case audioMasterGetParameterQuantization:
		Log("VST plugin to host: Audio Master Get Parameter Quantization\n");
		break;

	// numInputs and/or numOutputs has changed
	case audioMasterIOChanged:
		if(pVstPlugin != nullptr)
		{
			CriticalSection cs;
			return pVstPlugin->InitializeIOBuffers() ? 1 : 0;
		}
		break;

	// plug needs idle calls (outside its editor window) - DEPRECATED in VST 2.4
	case audioMasterNeedIdle:
		if(pVstPlugin != nullptr)
		{
			pVstPlugin->m_bNeedIdle = true;
		}

		return 1;

	// index: width, value: height
	case audioMasterSizeWindow:
		if(pVstPlugin != nullptr)
		{
			CAbstractVstEditor *pVstEditor = pVstPlugin->GetEditor();
			if (pVstEditor && pVstEditor->IsResizable())
			{
				pVstEditor->SetSize(index, static_cast<int>(value));
			}
		}
		Log("VST plugin to host: Size Window\n");
		return 1;

	case audioMasterGetSampleRate:
		if(pVstPlugin)
		{
			return pVstPlugin->m_nSampleRate;
		}

	case audioMasterGetBlockSize:
		return MIXBUFFERSIZE;

	case audioMasterGetInputLatency:
		Log("VST plugin to host: Get Input Latency\n");
		break;

	case audioMasterGetOutputLatency:
		if(pVstPlugin)
		{
			if(pVstPlugin->GetSoundFile().IsRenderingToDisc())
			{
				return 0;
			} else
			{
				return Util::Round<VstIntPtr>(pVstPlugin->GetSoundFile().m_TimingInfo.OutputLatency * pVstPlugin->m_nSampleRate);
			}
		}
		break;

	// input pin in <value> (-1: first to come), returns cEffect* - DEPRECATED in VST 2.4
	case audioMasterGetPreviousPlug:
		if(pVstPlugin != nullptr)
		{
			std::vector<CVstPlugin *> list;
			if(pVstPlugin->GetInputPlugList(list) != 0)
			{
				// We don't assign plugins to pins...
				return ToVstPtr(&list[0]->m_Effect);
			}
		}
		break;

	// output pin in <value> (-1: first to come), returns cEffect* - DEPRECATED in VST 2.4
	case audioMasterGetNextPlug:
		if(pVstPlugin != nullptr)
		{
			std::vector<CVstPlugin *> list;
			if(pVstPlugin->GetOutputPlugList(list) != 0)
			{
				// We don't assign plugins to pins...
				return ToVstPtr(&list[0]->m_Effect);
			}
		}
		break;

	// realtime info
	// returns: 0: not supported, 1: replace, 2: accumulate - DEPRECATED in VST 2.4 (replace is default)
	case audioMasterWillReplaceOrAccumulate:
		return 1; //we replace.

	case audioMasterGetCurrentProcessLevel:
		if(pVstPlugin != nullptr && pVstPlugin->GetSoundFile().IsRenderingToDisc())
			return kVstProcessLevelOffline;
		else
			return kVstProcessLevelRealtime;
		break;

	// returns 0: not supported, 1: off, 2:read, 3:write, 4:read/write
	case audioMasterGetAutomationState:
		// Not entirely sure what this means. We can write automation TO the plug.
		// Is that "read" in this context?
		//Log("VST plugin to host: Get Automation State\n");
		return kVstAutomationReadWrite;

	case audioMasterOfflineStart:
		Log("VST plugin to host: Offlinestart\n");
		break;

	case audioMasterOfflineRead:
		Log("VST plugin to host: Offlineread\n");
		break;

	case audioMasterOfflineWrite:
		Log("VST plugin to host: Offlinewrite\n");
		break;

	case audioMasterOfflineGetCurrentPass:
		Log("VST plugin to host: OfflineGetcurrentpass\n");
		break;

	case audioMasterOfflineGetCurrentMetaPass:
		Log("VST plugin to host: OfflineGetCurrentMetapass\n");
		break;

	// for variable i/o, sample rate in <opt> - DEPRECATED in VST 2.4
	case audioMasterSetOutputSampleRate:
		Log("VST plugin to host: Set Output Sample Rate\n");
		break;

	// result in ret - DEPRECATED in VST 2.4
	case audioMasterGetOutputSpeakerArrangement:
		Log("VST plugin to host: Get Output Speaker Arrangement\n");
		break;

	case audioMasterGetVendorString:
		strcpy((char *) ptr, s_szHostVendorString);
		return 1;

	case audioMasterGetProductString:
		strcpy((char *) ptr, s_szHostProductString);
		return 1;

	case audioMasterGetVendorVersion:
		return s_nHostVendorVersion;

	case audioMasterVendorSpecific:
		return 0;

	// void* in <ptr>, format not defined yet - DEPRECATED in VST 2.4
	case audioMasterSetIcon:
		Log("VST plugin to host: Set Icon\n");
		break;

	// string in ptr, see below
	case audioMasterCanDo:
		//Other possible Can Do strings are:
		//"receiveVstTimeInfo",
		//"asyncProcessing",
		//"offline",
		//"supportShell"
		//"shellCategory"
		//"editFile"
		//"startStopProcess"
		//"sendVstMidiEventFlagIsRealtime"

		if(!strcmp((char*)ptr,"sendVstEvents")
			|| !strcmp((char*)ptr,"sendVstMidiEvent")
			|| !strcmp((char*)ptr,"sendVstTimeInfo")
			|| !strcmp((char*)ptr,"receiveVstEvents")
			|| !strcmp((char*)ptr,"receiveVstMidiEvent")
			|| !strcmp((char*)ptr,"supplyIdle")
			|| !strcmp((char*)ptr,"sizeWindow")
			|| !strcmp((char*)ptr,"openFileSelector")
			|| !strcmp((char*)ptr,"closeFileSelector")
			|| !strcmp((char*)ptr,"acceptIOChanges")
			|| !strcmp((char*)ptr,"reportConnectionChanges"))
		{
			return HostCanDo;
		} else
		{
			return HostCanNotDo;
		}

	case audioMasterGetLanguage:
		return kVstLangEnglish;

	// returns platform specific ptr - DEPRECATED in VST 2.4
	case audioMasterOpenWindow:
		Log("VST plugin to host: Open Window\n");
		break;

	// close window, platform specific handle in <ptr> - DEPRECATED in VST 2.4
	case audioMasterCloseWindow:
		Log("VST plugin to host: Close Window\n");
		break;

	// get plug directory, FSSpec on MAC, else char*
	case audioMasterGetDirectory:
		//Log("VST plugin to host: Get Directory\n");
		// Need to allocate space for path only, but I guess noone relies on this anyway.
		//return ToVstPtr(pVstPlugin->GetPluginFactory().szDllPath);
		//return ToVstPtr(TrackerDirectories::Instance().GetDefaultDirectory(DIR_PLUGINS));
		break;

	// something has changed, update 'multi-fx' display
	case audioMasterUpdateDisplay:
		if(pVstPlugin != nullptr)
		{
			// Note to self for testing: Electri-Q sends opcode. Korg M1 sends this when switching between Combi and Multi mode to update the preset names.
			CAbstractVstEditor *pVstEditor = pVstPlugin->GetEditor();
			if(pVstEditor && ::IsWindow(pVstEditor->m_hWnd))
			{
				pVstEditor->UpdateDisplay();
			}
		}
		return 0;

	//---from here VST 2.1 extension opcodes------------------------------------------------------

	// begin of automation session (when mouse down), parameter index in <index>
	case audioMasterBeginEdit:
		Log("VST plugin to host: Begin Edit\n");
		break;

	// end of automation session (when mouse up),     parameter index in <index>
	case audioMasterEndEdit:
		Log("VST plugin to host: End Edit\n");
		break;

	// open a fileselector window with VstFileSelect* in <ptr>
	case audioMasterOpenFileSelector:

	//---from here VST 2.2 extension opcodes------------------------------------------------------

	// close a fileselector operation with VstFileSelect* in <ptr>: Must be always called after an open !
	case audioMasterCloseFileSelector:
		if(pVstPlugin != nullptr)
		{
			return VstFileSelector(opcode == audioMasterCloseFileSelector, static_cast<VstFileSelect *>(ptr), pVstPlugin);
		}

	// open an editor for audio (defined by XML text in ptr) - DEPRECATED in VST 2.4
	case audioMasterEditFile:
		Log("VST plugin to host: Edit File\n");
		break;

	// get the native path of currently loading bank or project
	// (called from writeChunk) void* in <ptr> (char[2048], or sizeof(FSSpec)) - DEPRECATED in VST 2.4
	case audioMasterGetChunkFile:
#ifdef MODPLUG_TRACKER
		if(pVstPlugin && pVstPlugin->GetModDoc())
		{
			strcpy(ptr, pVstPlugin->GetModDoc()->GetPathNameMpt().ToLocale().c_str());
			return 1;
		}
#endif
		Log("VST plugin to host: Get Chunk File\n");
		break;

	//---from here VST 2.3 extension opcodes------------------------------------------------------

	// result a VstSpeakerArrangement in ret - DEPRECATED in VST 2.4
	case audioMasterGetInputSpeakerArrangement:
		Log("VST plugin to host: Get Input Speaker Arrangement\n");
		break;

	}

	// Unknown codes:

	return 0;
}


// Helper function for file selection dialog stuff.
VstIntPtr CVstPluginManager::VstFileSelector(bool destructor, VstFileSelect *fileSel, const CVstPlugin *plugin)
//-------------------------------------------------------------------------------------------------------------
{
	if(fileSel == nullptr)
	{
		return 0;
	}

	if(!destructor)
	{
		fileSel->nbReturnPath = 0;
		fileSel->reserved = 0;

		if(fileSel->command != kVstDirectorySelect)
		{
			// Plugin wants to load or save a file.
			std::string extensions, workingDir;
			for(VstInt32 i = 0; i < fileSel->nbFileTypes; i++)
			{
				VstFileType *pType = &(fileSel->fileTypes[i]);
				extensions += pType->name;
				extensions += "|";
#if (defined(WIN32) || (defined(WINDOWS) && WINDOWS == 1))
				extensions += "*.";
				extensions += pType->dosType;
#elif defined(MAC) && MAC == 1
				extensions += "*";
				extensions += pType->macType;
#elif defined(UNIX) && UNIX == 1
				extensions += "*.";
				extensions += pType->unixType;
#else
#error Platform-specific code missing
#endif
				extensions += "|";
			}
			extensions += "|";

			if(fileSel->initialPath != nullptr)
			{
				workingDir = fileSel->initialPath;
			} else
			{
				// Plugins are probably looking for presets...?
				//workingDir = TrackerDirectories::Instance().GetWorkingDirectory(DIR_PLUGINPRESETS);
			}

			FileDialog dlg = OpenFileDialog();
			if(fileSel->command == kVstFileSave)
			{
				dlg = SaveFileDialog();
			} else if(fileSel->command == kVstMultipleFilesLoad)
			{
				dlg = OpenFileDialog().AllowMultiSelect();
			}
			dlg.ExtensionFilter(extensions)
				.WorkingDirectory(mpt::PathString::FromLocale(workingDir));
			if(!dlg.Show(plugin->GetEditor()))
			{
				return 0;
			}

			if(fileSel->command == kVstMultipleFilesLoad)
			{
				// Multiple paths
				const FileDialog::PathList &files = dlg.GetFilenames();
				fileSel->nbReturnPath = files.size();
				fileSel->returnMultiplePaths = new (std::nothrow) char *[fileSel->nbReturnPath];
				for(size_t i = 0; i < files.size(); i++)
				{
					char *fname = new (std::nothrow) char[files[i].ToLocale().length() + 1];
					strcpy(fname, files[i].ToLocale().c_str());
					fileSel->returnMultiplePaths[i] = fname;
				}
				return 1;
			} else
			{
				// Single path

				// VOPM doesn't initialize required information properly (it doesn't memset the struct to 0)...
				if(CCONST('V', 'O', 'P', 'M') == plugin->GetUID())
				{
					fileSel->sizeReturnPath = _MAX_PATH;
				}

				if(fileSel->returnPath == nullptr || fileSel->sizeReturnPath == 0)
				{

					// Provide some memory for the return path.
					fileSel->sizeReturnPath = dlg.GetFirstFile().ToLocale().length() + 1;
					fileSel->returnPath = new (std::nothrow) char[fileSel->sizeReturnPath];
					if(fileSel->returnPath == nullptr)
					{
						return 0;
					}
					fileSel->returnPath[fileSel->sizeReturnPath - 1] = '\0';
					fileSel->reserved = 1;
				} else
				{
					fileSel->reserved = 0;
				}
				strncpy(fileSel->returnPath, dlg.GetFirstFile().ToLocale().c_str(), fileSel->sizeReturnPath - 1);
				fileSel->nbReturnPath = 1;
				fileSel->returnMultiplePaths = nullptr;
			}
			return 1;

		} else
		{
			// Plugin wants a directory
			BrowseForFolder dlg(mpt::PathString::FromLocale(fileSel->initialPath != nullptr ? fileSel->initialPath : ""), fileSel->title != nullptr ? fileSel->title : "");
			if(dlg.Show(plugin->GetEditor()))
			{
				const std::string dir = dlg.GetDirectory().ToLocale();
				if(CCONST('V', 'S', 'T', 'r') == plugin->GetUID() && fileSel->returnPath != nullptr && fileSel->sizeReturnPath == 0)
				{
					// old versions of reViSiT (which still relied on the host's file selection code) seem to be dodgy.
					// They report a path size of 0, but when using an own buffer, they will crash.
					// So we'll just assume that reViSiT can handle long enough (_MAX_PATH) paths here.
					fileSel->sizeReturnPath = dir.length() + 1;
					fileSel->returnPath[fileSel->sizeReturnPath - 1] = '\0';
				}
				if(fileSel->returnPath == nullptr || fileSel->sizeReturnPath == 0)
				{
					// Provide some memory for the return path.
					fileSel->sizeReturnPath = mpt::saturate_cast<VstInt32>(dir.length() + 1);
					fileSel->returnPath = new char[fileSel->sizeReturnPath];
					if(fileSel->returnPath == nullptr)
					{
						return 0;
					}
					fileSel->returnPath[fileSel->sizeReturnPath - 1] = '\0';
					fileSel->reserved = 1;
				} else
				{
					fileSel->reserved = 0;
				}
				strncpy(fileSel->returnPath, dir.c_str(), fileSel->sizeReturnPath - 1);
				fileSel->nbReturnPath = 1;
				return 1;
			} else
			{
				return 0;
			}
		}
	} else
	{
		// Close file selector - delete allocated strings.
		if(fileSel->command == kVstMultipleFilesLoad && fileSel->returnMultiplePaths != nullptr)
		{
			for(VstInt32 i = 0; i < fileSel->nbReturnPath; i++)
			{
				if(fileSel->returnMultiplePaths[i] != nullptr)
				{
					delete[] fileSel->returnMultiplePaths[i];
				}
			}
			delete[] fileSel->returnMultiplePaths;
			fileSel->returnMultiplePaths = nullptr;
		} else
		{
			if(fileSel->reserved == 1 && fileSel->returnPath != nullptr)
			{
				delete[] fileSel->returnPath;
				fileSel->returnPath = nullptr;
			}
		}
		return 1;
	}
}


//////////////////////////////////////////////////////////////////////////////
//
// CVstPlugin
//

CVstPlugin::CVstPlugin(HMODULE hLibrary, VSTPluginLib &factory, SNDMIXPLUGIN &mixStruct, AEffect &effect, CSoundFile &sndFile)
	: m_SndFile(sndFile), m_Factory(factory), m_Effect(effect), isBridged(!memcmp(&effect.resvd2, "OMPT", 4))
//-----------------------------------------------------------------------------------------------------------
{
	m_hLibrary = hLibrary;
	m_pPrev = nullptr;
	m_pNext = nullptr;
	m_pMixStruct = &mixStruct;
	m_pEditor = nullptr;
	m_pProcessFP = nullptr;

	m_MixState.dwFlags = 0;
	m_MixState.nVolDecayL = 0;
	m_MixState.nVolDecayR = 0;
	m_MixState.pMixBuffer = (mixsample_t *)((((intptr_t)m_MixBuffer) + 7) & ~7);
	m_MixState.pOutBufferL = mixBuffer.GetInputBuffer(0);
	m_MixState.pOutBufferR = mixBuffer.GetInputBuffer(1);

	m_bSongPlaying = false;
	m_bPlugResumed = false;
	m_nSampleRate = uint32_max;
	isInitialized = false;

	MemsetZero(m_MidiCh);
	for(int ch = 0; ch < 16; ch++)
	{
		m_MidiCh[ch].midiPitchBendPos = EncodePitchBendParam(MIDIEvents::pitchBendCentre); // centre pitch bend on all channels
		m_MidiCh[ch].ResetProgram();
	}

	// Update Mix structure
	m_pMixStruct->pMixState = &m_MixState;
	// Open plugin and initialize data structures
	Initialize();
	// Now we should be ready to go
	m_pMixStruct->pMixPlugin = this;

	// Insert ourselves in the beginning of the list
	m_pNext = m_Factory.pPluginsList;
	if(m_Factory.pPluginsList)
	{
		m_Factory.pPluginsList->m_pPrev = this;
	}
	m_Factory.pPluginsList = this;

	isInitialized = true;
}


void CVstPlugin::Initialize()
//---------------------------
{
	m_bNeedIdle = false;
	m_bRecordAutomation = false;
	m_bPassKeypressesToPlug = false;
	m_bRecordMIDIOut = false;

	// Store a pointer so we can get the CVstPlugin object from the basic VST effect object.
	m_Effect.resvd1 = ToVstPtr(this);
	m_nSlot = FindSlot();
	m_nSampleRate = m_SndFile.GetSampleRate();

	Dispatch(effOpen, 0, 0, nullptr, 0.0f);
	// VST 2.0 plugins return 2 here, VST 2.4 plugins return 2400... Great!
	m_bIsVst2 = Dispatch(effGetVstVersion, 0,0, nullptr, 0.0f) >= 2;
	if (m_bIsVst2)
	{
		// Set VST speaker in/out setup to Stereo. Required for some plugins (e.g. Voxengo SPAN 2)
		// All this might get more interesting when adding sidechaining support...
		VstSpeakerArrangement sa;
		MemsetZero(sa);
		sa.numChannels = 2;
		sa.type = kSpeakerArrStereo;
		for(int i = 0; i < CountOf(sa.speakers); i++)
		{
			sa.speakers[i].azimuth = 0.0f;
			sa.speakers[i].elevation = 0.0f;
			sa.speakers[i].radius = 0.0f;
			sa.speakers[i].reserved = 0.0f;
			// For now, only left and right speaker are used.
			switch(i)
			{
			case 0:
				sa.speakers[i].type = kSpeakerL;
				vst_strncpy(sa.speakers[i].name, "Left", kVstMaxNameLen - 1);
				break;
			case 1:
				sa.speakers[i].type = kSpeakerR;
				vst_strncpy(sa.speakers[i].name, "Right", kVstMaxNameLen - 1);
				break;
			default:
				sa.speakers[i].type = kSpeakerUndefined;
				break;
			}
		}
		// For now, input setup = output setup.
		Dispatch(effSetSpeakerArrangement, 0, ToVstPtr(&sa), &sa, 0.0f);

		// Dummy pin properties collection.
		// We don't use them but some plugs might do inits in here.
		VstPinProperties tempPinProperties;
		Dispatch(effGetInputProperties, 0, 0, &tempPinProperties, 0);
		Dispatch(effGetOutputProperties, 0, 0, &tempPinProperties, 0);

		Dispatch(effConnectInput, 0, 1, nullptr, 0.0f);
		if (m_Effect.numInputs > 1) Dispatch(effConnectInput, 1, 1, nullptr, 0.0f);
		Dispatch(effConnectOutput, 0, 1, nullptr, 0.0f);
		if (m_Effect.numOutputs > 1) Dispatch(effConnectOutput, 1, 1, nullptr, 0.0f);
		//rewbs.VSTCompliance: disable all inputs and outputs beyond stereo left and right:
		for (int i=2; i<m_Effect.numInputs; i++)
			Dispatch(effConnectInput, i, 0, nullptr, 0.0f);
		for (int i=2; i<m_Effect.numOutputs; i++)
			Dispatch(effConnectOutput, i, 0, nullptr, 0.0f);
		//end rewbs.VSTCompliance

	}

	Dispatch(effSetSampleRate, 0, 0, nullptr, static_cast<float>(m_nSampleRate));
	Dispatch(effSetBlockSize, 0, MIXBUFFERSIZE, nullptr, 0.0f);
	if(m_Effect.numPrograms > 0)
	{
		Dispatch(effBeginSetProgram, 0, 0, nullptr, 0);
		Dispatch(effSetProgram, 0, 0, nullptr, 0);
		Dispatch(effEndSetProgram, 0, 0, nullptr, 0);
	}

	InitializeIOBuffers();

	Dispatch(effSetProcessPrecision, 0, kVstProcessPrecision32, nullptr, 0.0f);

#ifdef VST_LOG
	Log("%s: vst ver %d.0, flags=%04X, %d programs, %d parameters\n",
		m_Factory.libraryName, (m_bIsVst2) ? 2 : 1, m_Effect.flags,
		m_Effect.numPrograms, m_Effect.numParams);
#endif

	m_bIsInstrument = isInstrument();
	RecalculateGain();
	m_pProcessFP = (m_Effect.flags & effFlagsCanReplacing) ? m_Effect.processReplacing : m_Effect.process;

	// issue samplerate again here, cos some plugs like it before the block size, other like it right at the end.
	Dispatch(effSetSampleRate, 0, 0, nullptr, static_cast<float>(m_nSampleRate));

	// Korg Wavestation GUI won't work until plugin was resumed at least once.
	// On the other hand, some other plugins (notably Synthedit plugins like Superwave P8 2.3 or Rez 3.0) don't like this
	// and won't load their stored plugin data instantly, so only do this for the troublesome plugins...
	// Also apply this fix for Korg's M1 plugin, as this will fixes older versions of said plugin, newer versions don't require the fix.
	// EZDrummer won't load its samples until playback has started.
	if(GetUID() == CCONST('K', 'L', 'W', 'V')			// Wavestation
		|| GetUID() == CCONST('K', 'L', 'M', '1')		// M1
		|| GetUID() == CCONST('d', 'f', 'h', 'e'))		// EZDrummer
	{
		Resume();
		Suspend();
	}
}


bool CVstPlugin::InitializeIOBuffers()
//------------------------------------
{
	// Input pointer array size must be >= 2 for now - the input buffer assignment might write to non allocated mem. otherwise
	// In case of a bridged plugin, the AEffect struct has been updated before calling this opcode, so we don't have to worry about it being up-to-date.
	bool result = mixBuffer.Initialize(std::max<size_t>(m_Effect.numInputs, 2), m_Effect.numOutputs);
	m_MixState.pOutBufferL = mixBuffer.GetInputBuffer(0);
	m_MixState.pOutBufferR = mixBuffer.GetInputBuffer(1);

	return result;
}


CVstPlugin::~CVstPlugin()
//-----------------------
{
	CriticalSection cs;

	if (m_pEditor)
	{
		if (m_pEditor->m_hWnd) m_pEditor->OnClose();
		if ((volatile void *)m_pEditor) delete m_pEditor;
		m_pEditor = nullptr;
	}
	if (m_bIsVst2)
	{
		Dispatch(effConnectInput, 0, 0, nullptr, 0);
		if (m_Effect.numInputs > 1) Dispatch(effConnectInput, 1, 0, nullptr, 0);
		Dispatch(effConnectOutput, 0, 0, nullptr, 0);
		if (m_Effect.numOutputs > 1) Dispatch(effConnectOutput, 1, 0, nullptr, 0);
	}
	Suspend();
	isInitialized = false;

	// First thing to do, if we don't want to hang in a loop
	if (m_Factory.pPluginsList == this) m_Factory.pPluginsList = m_pNext;
	if (m_pMixStruct)
	{
		m_pMixStruct->pMixPlugin = nullptr;
		m_pMixStruct->pMixState = nullptr;
		m_pMixStruct = nullptr;
	}
	if (m_pNext) m_pNext->m_pPrev = m_pPrev;
	if (m_pPrev) m_pPrev->m_pNext = m_pNext;
	m_pPrev = nullptr;
	m_pNext = nullptr;

	Dispatch(effClose, 0, 0, nullptr, 0);
	if(m_hLibrary)
	{
		FreeLibrary(m_hLibrary);
		m_hLibrary = nullptr;
	}

}


void CVstPlugin::Release()
//------------------------
{
	try
	{
		delete this;
	} catch (...)
	{
		ReportPlugException(L"Exception while destroying plugin!");
	}
}


#ifdef MODPLUG_TRACKER
CModDoc *CVstPlugin::GetModDoc() { return m_SndFile.GetpModDoc(); }
const CModDoc *CVstPlugin::GetModDoc() const { return m_SndFile.GetpModDoc(); }
#endif // MODPLUG_TRACKER


void CVstPlugin::GetPluginType(LPSTR pszType)
//-------------------------------------------
{
	pszType[0] = 0;
	if (m_Effect.numInputs < 1) strcpy(pszType, "No input"); else
	if (m_Effect.numInputs == 1) strcpy(pszType, "Mono-In"); else
	strcpy(pszType, "Stereo-In");
	strcat(pszType, ", ");
	if (m_Effect.numOutputs < 1) strcat(pszType, "No output"); else
	if (m_Effect.numInputs == 1) strcat(pszType, "Mono-Out"); else
	strcat(pszType, "Stereo-Out");
}


bool CVstPlugin::HasEditor()
//--------------------------
{
	return (m_Effect.flags & effFlagsHasEditor) != 0;
}


VstInt32 CVstPlugin::GetNumPrograms()
//-----------------------------------
{
	return std::max(m_Effect.numPrograms, VstInt32(0));
}


PlugParamIndex CVstPlugin::GetNumParameters()
//-------------------------------------------
{
	return std::max(m_Effect.numParams, VstInt32(0));
}


// Check whether a VST parameter can be automated
bool CVstPlugin::CanAutomateParameter(PlugParamIndex index)
//---------------------------------------------------------
{
	return (Dispatch(effCanBeAutomated, index, 0, nullptr, 0.0f) != 0);
}


VstInt32 CVstPlugin::GetUID() const
//---------------------------------
{
	return m_Effect.uniqueID;
}


VstInt32 CVstPlugin::GetVersion() const
//-------------------------------------
{
	return m_Effect.version;
}


bool CVstPlugin::GetParams(float *param, VstInt32 min, VstInt32 max)
//------------------------------------------------------------------
{
	LimitMax(max, m_Effect.numParams);

	for(VstInt32 p = min; p < max; p++)
		param[p - min] = GetParameter(p);

	return true;
}


void CVstPlugin::RandomizeParams(int amount)
//------------------------------------------
{
	PlugParamValue factor = PlugParamValue(amount) / 100.0f;
	for(PlugParamIndex p = 0; p < m_Effect.numParams; p++)
	{
		PlugParamValue val = GetParameter(p);
		val += (2.0f * PlugParamValue(rand()) / PlugParamValue(RAND_MAX) - 1.0f) * factor;
		Limit(val, 0.0f, 1.0f);
		SetParameter(p, val);
	}
}


bool CVstPlugin::SaveProgram()
//----------------------------
{
	mpt::PathString defaultDir = TrackerDirectories::Instance().GetWorkingDirectory(DIR_PLUGINPRESETS);
	bool useDefaultDir = !defaultDir.empty();
	if(!useDefaultDir)
	{
		defaultDir = m_Factory.dllPath.GetPath();
	}

	char rawname[MAX(kVstMaxProgNameLen + 1, 256)] = "";	// kVstMaxProgNameLen is 24...
	Dispatch(effGetProgramName, 0, 0, rawname, 0);
	SanitizeFilename(rawname);
	mpt::String::SetNullTerminator(rawname);

	FileDialog dlg = SaveFileDialog()
		.DefaultExtension("fxb")
		.DefaultFilename(rawname)
		.ExtensionFilter("VST Plugin Programs (*.fxp)|*.fxp|"
			"VST Plugin Banks (*.fxb)|*.fxb||")
		.WorkingDirectory(defaultDir);
	if(!dlg.Show(m_pEditor)) return false;

	if(useDefaultDir)
	{
		TrackerDirectories::Instance().SetWorkingDirectory(dlg.GetWorkingDirectory(), DIR_PLUGINPRESETS);
	}

	bool bank = (dlg.GetExtension() == MPT_PATHSTRING("fxb"));

	mpt::fstream f(dlg.GetFirstFile(), std::ios::out | std::ios::trunc | std::ios::binary);
	if(f.good() && VSTPresets::SaveFile(f, *this, bank))
	{
		return true;
	} else
	{
		Reporting::Error("Error saving preset.", m_pEditor);
		return false;
	}

}


bool CVstPlugin::LoadProgram(mpt::PathString fileName)
//----------------------------------------------------
{
	mpt::PathString defaultDir = TrackerDirectories::Instance().GetWorkingDirectory(DIR_PLUGINPRESETS);
	bool useDefaultDir = !defaultDir.empty();
	if(!useDefaultDir)
	{
		defaultDir = m_Factory.dllPath.GetPath();
	}

	if(fileName.empty())
	{
		FileDialog dlg = OpenFileDialog()
			.DefaultExtension("fxp")
			.ExtensionFilter("VST Plugin Programs and Banks (*.fxp,*.fxb)|*.fxp;*.fxb|"
			"VST Plugin Programs (*.fxp)|*.fxp|"
			"VST Plugin Banks (*.fxb)|*.fxb|"
			"All Files|*.*||")
			.WorkingDirectory(defaultDir);
		if(!dlg.Show(m_pEditor)) return false;

		if(useDefaultDir)
		{
			TrackerDirectories::Instance().SetWorkingDirectory(dlg.GetWorkingDirectory(), DIR_PLUGINPRESETS);
		}
		fileName = dlg.GetFirstFile();
	}

	const char *errorStr = nullptr;
	InputFile f(fileName);
	if(f.IsValid())
	{
		FileReader file = GetFileReader(f);
		errorStr = VSTPresets::GetErrorMessage(VSTPresets::LoadFile(file, *this));
	} else
	{
		errorStr = "Can't open file.";
	}

	if(errorStr == nullptr)
	{
#ifndef MODPLUG_TRACKER
		if(GetModDoc() != nullptr && GetSoundFile().GetModSpecifications().supportsPlugins)
		{
			GetModDoc()->SetModified();
		}
#endif // MODPLUG_TRACKER
		return true;
	} else
	{
		Reporting::Error(errorStr, m_pEditor);
		return false;
	}
}


VstIntPtr CVstPlugin::Dispatch(VstInt32 opCode, VstInt32 index, VstIntPtr value, void *ptr, float opt)
//----------------------------------------------------------------------------------------------------
{
	VstIntPtr result = 0;

	try
	{
		if(m_Effect.dispatcher != nullptr)
		{
			#ifdef VST_LOG
			Log("About to Dispatch(%d) (Plugin=\"%s\"), index: %d, value: %d, value: %h, value: %f!\n", opCode, m_Factory.libraryName, index, value, ptr, opt);
			#endif
			result = m_Effect.dispatcher(&m_Effect, opCode, index, value, ptr, opt);
		}
	} catch (...)
	{
		ReportPlugException(mpt::String::PrintW(L"Exception in Dispatch(%1)!", opCode));
	}

	return result;
}


VstInt32 CVstPlugin::GetCurrentProgram()
//--------------------------------------
{
	if(m_Effect.numPrograms > 0)
	{
		return Dispatch(effGetProgram, 0, 0, nullptr, 0);
	}
	return 0;
}


bool CVstPlugin::GetProgramNameIndexed(VstInt32 index, VstIntPtr category, char *text)
//------------------------------------------------------------------------------------
{
	if(m_Effect.numPrograms > 0)
	{
		return (Dispatch(effGetProgramNameIndexed, index, category, text, 0) == 1);
	}
	return false;
}


CString CVstPlugin::GetFormattedProgramName(VstInt32 index)
//---------------------------------------------------------
{
	char rawname[MAX(kVstMaxProgNameLen + 1, 256)];	// kVstMaxProgNameLen is 24...
	if(!GetProgramNameIndexed(index, -1, rawname))
	{
		// Fallback: Try to get current program name.
		strcpy(rawname, "");
		VstInt32 curProg = GetCurrentProgram();
		if(index != curProg)
		{
			SetCurrentProgram(index);
		}
		Dispatch(effGetProgramName, 0, 0, rawname, 0);
		if(index != curProg)
		{
			SetCurrentProgram(curProg);
		}
	}
	mpt::String::SetNullTerminator(rawname);

	// Let's start counting at 1 for the program name (as most MIDI hardware / software does)
	index++;

	CString formattedName;
	if((unsigned char)rawname[0] < ' ')
	{
		formattedName.Format("%02d - Program %d", index, index);
	}
	else
	{
		formattedName.Format("%02d - %s", index, rawname);
	}

	return formattedName;
}


void CVstPlugin::SetCurrentProgram(VstInt32 nIndex)
//-------------------------------------------------
{
	if(m_Effect.numPrograms > 0)
	{
		if(nIndex < m_Effect.numPrograms)
		{
			Dispatch(effBeginSetProgram, 0, 0, nullptr, 0);
			Dispatch(effSetProgram, 0, nIndex, nullptr, 0);
			Dispatch(effEndSetProgram, 0, 0, nullptr, 0);
		}
	}
}


PlugParamValue CVstPlugin::GetParameter(PlugParamIndex nIndex)
//------------------------------------------------------------
{
	float fResult = 0;
	if(nIndex < m_Effect.numParams && m_Effect.getParameter != nullptr)
	{
		try
		{
			fResult = m_Effect.getParameter(&m_Effect, nIndex);
		} catch (...)
		{
			//CVstPluginManager::ReportPlugException("Exception in getParameter (Plugin=\"%s\")!\n", m_Factory.szLibraryName);
		}
	}
	//rewbs.VSTcompliance
	if (fResult<0.0f)
		return 0.0f;
	else if (fResult>1.0f)
		return 1.0f;
	else
	//end rewbs.VSTcompliance
		return fResult;
}


void CVstPlugin::SetParameter(PlugParamIndex nIndex, PlugParamValue fValue)
//-------------------------------------------------------------------------
{
	try
	{
		if(nIndex < m_Effect.numParams && m_Effect.setParameter)
		{
			if ((fValue >= 0.0f) && (fValue <= 1.0f))
				m_Effect.setParameter(&m_Effect, nIndex, fValue);
		}
		ResetSilence();
	} catch (...)
	{
		ReportPlugException(mpt::String::PrintW(L"Exception in SetParameter(%1, %2)!", nIndex, fValue));
	}
}


// Helper function for retreiving parameter name / label / display
CString CVstPlugin::GetParamPropertyString(VstInt32 param, VstInt32 opcode)
//-------------------------------------------------------------------------
{
	char s[MAX(kVstMaxParamStrLen + 1, 64)]; // Increased to 64 bytes since 32 bytes doesn't seem to suffice for all plugs. Kind of ridiculous if you consider that kVstMaxParamStrLen = 8...
	s[0] = '\0';

	if(m_Effect.numParams > 0 && param < m_Effect.numParams)
	{
		Dispatch(opcode, param, 0, s, 0);
		mpt::String::SetNullTerminator(s);
	}
	return CString(s);
}


CString CVstPlugin::GetFormattedParamName(PlugParamIndex param)
//-------------------------------------------------------------
{
	CString paramName;

	VstParameterProperties properties;
	MemsetZero(properties.label);
	if(Dispatch(effGetParameterProperties, param, 0, &properties, 0.0f) == 1)
	{
		mpt::String::SetNullTerminator(properties.label);
		paramName = properties.label;
	} else
	{
		paramName = GetParamName(param);
	}

	CString name;
	if(paramName.IsEmpty())
	{
		name.Format("%02d: Parameter %02d", param, param);
	} else
	{
		name.Format("%02d: %s", param, paramName);
	}
	return name;
}


// Get a parameter's current value, represented by the plugin.
CString CVstPlugin::GetFormattedParamValue(PlugParamIndex param)
//--------------------------------------------------------------
{

	CString paramDisplay = GetParamDisplay(param);
	CString paramUnits = GetParamLabel(param);
	paramDisplay.Trim();
	paramUnits.Trim();
	paramDisplay += " " + paramUnits;

	return paramDisplay;
}


BOOL CVstPlugin::GetDefaultEffectName(LPSTR pszName)
//--------------------------------------------------
{
	pszName[0] = 0;
	if(m_bIsVst2)
	{
		Dispatch(effGetEffectName, 0, 0, pszName, 0);
		return TRUE;
	}
	return FALSE;
}


void CVstPlugin::Resume()
//-----------------------
{
	const uint32 sampleRate = m_SndFile.GetSampleRate();

	try
	{
		//reset some stuff
		m_MixState.nVolDecayL = 0;
		m_MixState.nVolDecayR = 0;
		if(m_bPlugResumed)
		{
			Dispatch(effStopProcess, 0, 0, nullptr, 0.0f);
			Dispatch(effMainsChanged, 0, 0, nullptr, 0.0f);	// calls plugin's suspend
		}
		if (sampleRate != m_nSampleRate)
		{
			m_nSampleRate = sampleRate;
			Dispatch(effSetSampleRate, 0, 0, nullptr, static_cast<float>(m_nSampleRate));
		}
		Dispatch(effSetBlockSize, 0, MIXBUFFERSIZE, nullptr, 0.0f);
		//start off some stuff
		Dispatch(effMainsChanged, 0, 1, nullptr, 0.0f);	// calls plugin's resume
		Dispatch(effStartProcess, 0, 0, nullptr, 0.0f);
		m_bPlugResumed = true;
	} catch (...)
	{
		ReportPlugException(L"Exception in Resume()!");
	}
}


void CVstPlugin::Suspend()
//------------------------
{
	if(m_bPlugResumed)
	{
		try
		{
			Dispatch(effStopProcess, 0, 0, nullptr, 0.0f);
			Dispatch(effMainsChanged, 0, 0, nullptr, 0.0f); // calls plugin's suspend (theoretically, plugins should clean their buffers here, but oh well, the number of plugins which don't do this is surprisingly high.)
			m_bPlugResumed = false;
		} catch (...)
		{
			ReportPlugException(L"Exception in Suspend()!");
		}
	}
}


// Send events to plugin. Returns true if there are events left to be processed.
void CVstPlugin::ProcessVSTEvents()
//---------------------------------
{
	// Process VST events
	if(m_Effect.dispatcher != nullptr && vstEvents.Finalise() > 0)
	{
		try
		{
			m_Effect.dispatcher(&m_Effect, effProcessEvents, 0, 0, &vstEvents, 0);
			ResetSilence();
		} catch (...)
		{
			ReportPlugException(mpt::String::PrintW(L"Exception in ProcessVSTEvents(numEvents:%1)!",
				vstEvents.GetNumEvents()));
		}
	}
}


// Receive events from plugin and send them to the next plugin in the chain.
void CVstPlugin::ReceiveVSTEvents(const VstEvents *events)
//--------------------------------------------------------
{
	if(m_pMixStruct == nullptr)
	{
		return;
	}

	ResetSilence();

	// I think we should only route events to plugins that are explicitely specified as output plugins of the current plugin.
	// This should probably use GetOutputPlugList here if we ever get to support multiple output plugins.
	PLUGINDEX receiver = m_pMixStruct->GetOutputPlugin();

	if(receiver != PLUGINDEX_INVALID)
	{
		SNDMIXPLUGIN &mixPlug = m_SndFile.m_MixPlugins[receiver];
		CVstPlugin *vstPlugin = dynamic_cast<CVstPlugin *>(mixPlug.pMixPlugin);
		if(vstPlugin != nullptr)
		{
			// Add all events to the plugin's queue.
			for(VstInt32 i = 0; i < events->numEvents; i++)
			{
				vstPlugin->vstEvents.Enqueue(events->events[i]);
			}
		}
	}

#ifdef MODPLUG_TRACKER
	if(m_bRecordMIDIOut)
	{
		// Spam MIDI data to all views
		for(VstInt32 i = 0; i < events->numEvents; i++)
		{
			if(events->events[i]->type == kVstMidiType)
			{
				VstMidiEvent *event = reinterpret_cast<VstMidiEvent *>(events->events[i]);
				::PostMessage(CMainFrame::GetMainFrame()->GetMidiRecordWnd(), WM_MOD_MIDIMSG, *reinterpret_cast<uint32 *>(event->midiData), reinterpret_cast<LPARAM>(this));
			}
		}
	}
#endif // MODPLUG_TRACKER
}


void CVstPlugin::RecalculateGain()
//--------------------------------
{
	float gain = 0.1f * static_cast<float>(m_pMixStruct ? m_pMixStruct->GetGain() : 10);
	if(gain < 0.1f) gain = 1.0f;

	if(m_bIsInstrument)
	{
		gain /= m_SndFile.GetPlayConfig().getVSTiAttenuation();
		gain = static_cast<float>(gain * (m_SndFile.m_nVSTiVolume / m_SndFile.GetPlayConfig().getNormalVSTiVol()));
	}
	m_fGain = gain;
}


void CVstPlugin::SetDryRatio(UINT param)
//--------------------------------------
{
	param = MIN(param, 127);
	m_pMixStruct->fDryRatio = static_cast<float>(1.0-(static_cast<double>(param)/127.0));
}


// Render some silence and return maximum level returned by the plugin.
float CVstPlugin::RenderSilence(size_t numSamples)
//------------------------------------------------
{
	// The JUCE framework doesn't like processing while being suspended.
	const bool wasSuspended = !IsResumed();
	if(wasSuspended)
	{
		Resume();
	}

	float out[2][MIXBUFFERSIZE]; // scratch buffers
	float maxVal = 0.0f;
	mixBuffer.ClearInputBuffers(MIXBUFFERSIZE);

	while(numSamples > 0)
	{
		size_t renderSamples = numSamples;
		LimitMax(renderSamples, CountOf(out[0]));
		MemsetZero(out);

		Process(out[0], out[1], renderSamples);
		for(size_t i = 0; i < renderSamples; i++)
		{
			maxVal = std::max(maxVal, fabs(out[0][i]));
			maxVal = std::max(maxVal, fabs(out[1][i]));
		}

		numSamples -= renderSamples;
	}

	if(wasSuspended)
	{
		Suspend();
	}

	return maxVal;
}


void CVstPlugin::Process(float *pOutL, float *pOutR, size_t nSamples)
//-------------------------------------------------------------------
{
	ProcessVSTEvents();

	// If the plug is found & ok, continue
	if(m_pProcessFP != nullptr && (mixBuffer.GetInputBufferArray()) && mixBuffer.GetOutputBufferArray() && m_pMixStruct != nullptr)
	{

		//RecalculateGain();

		// Merge stereo input before sending to the plug if the plug can only handle one input.
		if (m_Effect.numInputs == 1)
		{
			for (size_t i = 0; i < nSamples; i++)
			{
				m_MixState.pOutBufferL[i] = 0.5f * m_MixState.pOutBufferL[i] + 0.5f * m_MixState.pOutBufferR[i];
			}
		}

		float **outputBuffers = mixBuffer.GetOutputBufferArray();
		if(!isBridged)
		{
			mixBuffer.ClearOutputBuffers(nSamples);
		}

		// Do the VST processing magic
		try
		{
			ASSERT(nSamples <= MIXBUFFERSIZE);
			m_pProcessFP(&m_Effect, mixBuffer.GetInputBufferArray(), outputBuffers, nSamples);
		} catch (...)
		{
			Bypass();
			const wchar_t *processMethod = (m_Effect.flags & effFlagsCanReplacing) ? L"processReplacing" : L"process";
			ReportPlugException(mpt::String::PrintW(L"The plugin threw an exception in %1. It has automatically been set to \"Bypass\".", processMethod));
		}

		ASSERT(outputBuffers != nullptr);

		// Mix outputs of multi-output VSTs:
		if(m_Effect.numOutputs > 2)
		{
			// first, mix extra outputs on a stereo basis
			VstInt32 numOutputs = m_Effect.numOutputs;
			// so if nOuts is not even, let process the last output later
			if((numOutputs % 2u) == 1) numOutputs--;

			// mix extra stereo outputs
			for(VstInt32 iOut = 2; iOut < numOutputs; iOut++)
			{
				for(size_t i = 0; i < nSamples; i++)
				{
					outputBuffers[iOut % 2u][i] += outputBuffers[iOut][i]; // assumed stereo.
				}
			}

			// if m_Effect.numOutputs is odd, mix half the signal of last output to each channel
			if(numOutputs != m_Effect.numOutputs)
			{
				// trick : if we are here, numOutputs = m_Effect.numOutputs - 1 !!!
				for(size_t i = 0; i < nSamples; i++)
				{
					float v = 0.5f * outputBuffers[numOutputs][i];
					outputBuffers[0][i] += v;
					outputBuffers[1][i] += v;
				}
			}
		}

		if(m_Effect.numOutputs != 0)
		{
			ProcessMixOps(pOutL, pOutR, outputBuffers[0], outputBuffers[m_Effect.numOutputs > 1 ? 1 : 0], nSamples);
		}

		// If dry mix is ticked, we add the unprocessed buffer,
		// except if this is an instrument since this it has already been done:
		if(m_pMixStruct->IsWetMix() && !m_bIsInstrument)
		{
			for(size_t i = 0; i < nSamples; i++)
			{
				pOutL[i] += m_MixState.pOutBufferL[i];
				pOutR[i] += m_MixState.pOutBufferR[i];
			}
		}
	}

	vstEvents.Clear();
}


void CVstPlugin::ProcessMixOps(float *pOutL, float *pOutR, float *leftPlugOutput, float *rightPlugOutput, size_t nSamples)
//------------------------------------------------------------------------------------------------------------------------
{
/*	float *leftPlugOutput;
	float *rightPlugOutput;

	if(m_Effect.numOutputs == 1)
	{
		// If there was just the one plugin output we copy it into our 2 outputs
		leftPlugOutput = rightPlugOutput = mixBuffer.GetOutputBuffer(0);
	} else if(m_Effect.numOutputs > 1)
	{
		// Otherwise we actually only cater for two outputs max (outputs > 2 have been mixed together already).
		leftPlugOutput = mixBuffer.GetOutputBuffer(0);
		rightPlugOutput = mixBuffer.GetOutputBuffer(1);
	} else
	{
		return;
	}*/

	// -> mixop == 0 : normal processing
	// -> mixop == 1 : MIX += DRY - WET * wetRatio
	// -> mixop == 2 : MIX += WET - DRY * dryRatio
	// -> mixop == 3 : MIX -= WET - DRY * wetRatio
	// -> mixop == 4 : MIX -= middle - WET * wetRatio + middle - DRY
	// -> mixop == 5 : MIX_L += wetRatio * (WET_L - DRY_L) + dryRatio * (DRY_R - WET_R)
	//                 MIX_R += dryRatio * (WET_L - DRY_L) + wetRatio * (DRY_R - WET_R)

	int mixop;
	if(m_bIsInstrument || m_pMixStruct == nullptr)
	{
		// Force normal mix mode for instruments
		mixop = 0;
	} else
	{
		mixop = m_pMixStruct->GetMixMode();
	}

	float wetRatio = 1 - m_pMixStruct->fDryRatio;
	float dryRatio = m_bIsInstrument ? 1 : m_pMixStruct->fDryRatio; // Always mix full dry if this is an instrument

	// Wet / Dry range expansion [0,1] -> [-1,1]
	if(m_Effect.numInputs > 0 && m_pMixStruct->IsExpandedMix())
	{
		wetRatio = 2.0f * wetRatio - 1.0f;
		dryRatio = -wetRatio;
	}

	wetRatio *= m_fGain;
	dryRatio *= m_fGain;

	// Mix operation
	switch(mixop)
	{

	// Default mix
	case 0:
		for(size_t i = 0; i < nSamples; i++)
		{
			//rewbs.wetratio - added the factors. [20040123]
			pOutL[i] += leftPlugOutput[i] * wetRatio + m_MixState.pOutBufferL[i] * dryRatio;
			pOutR[i] += rightPlugOutput[i] * wetRatio + m_MixState.pOutBufferR[i] * dryRatio;
		}
		break;

	// Wet subtract
	case 1:
		for(size_t i = 0; i < nSamples; i++)
		{
			pOutL[i] += m_MixState.pOutBufferL[i] - leftPlugOutput[i] * wetRatio;
			pOutR[i] += m_MixState.pOutBufferR[i] - rightPlugOutput[i] * wetRatio;
		}
		break;

	// Dry subtract
	case 2:
		for(size_t i = 0; i < nSamples; i++)
		{
			pOutL[i] += leftPlugOutput[i] - m_MixState.pOutBufferL[i] * dryRatio;
			pOutR[i] += rightPlugOutput[i] - m_MixState.pOutBufferR[i] * dryRatio;
		}
		break;

	// Mix subtract
	case 3:
		for(size_t i = 0; i < nSamples; i++)
		{
			pOutL[i] -= leftPlugOutput[i] - m_MixState.pOutBufferL[i] * wetRatio;
			pOutR[i] -= rightPlugOutput[i] - m_MixState.pOutBufferR[i] * wetRatio;
		}
		break;

	// Middle subtract
	case 4:
		for(size_t i = 0; i < nSamples; i++)
		{
			float middle = (pOutL[i] + m_MixState.pOutBufferL[i] + pOutR[i] + m_MixState.pOutBufferR[i]) / 2.0f;
			pOutL[i] -= middle - leftPlugOutput[i] * wetRatio + middle - m_MixState.pOutBufferL[i];
			pOutR[i] -= middle - rightPlugOutput[i] * wetRatio + middle - m_MixState.pOutBufferR[i];
		}
		break;

	// Left / Right balance
	case 5:
		if(m_pMixStruct->IsExpandedMix())
		{
			wetRatio /= 2.0f;
			dryRatio /= 2.0f;
		}

		for(size_t i = 0; i < nSamples; i++)
		{
			pOutL[i] += wetRatio * (leftPlugOutput[i] - m_MixState.pOutBufferL[i]) + dryRatio * (m_MixState.pOutBufferR[i] - rightPlugOutput[i]);
			pOutR[i] += dryRatio * (leftPlugOutput[i] - m_MixState.pOutBufferL[i]) + wetRatio * (m_MixState.pOutBufferR[i] - rightPlugOutput[i]);
		}
		break;
	}
}


bool CVstPlugin::MidiSend(uint32 dwMidiCode)
//------------------------------------------
{
	// Note-Offs go at the start of the queue.
	bool insertAtFront = (MIDIEvents::GetTypeFromEvent(dwMidiCode) == MIDIEvents::evNoteOff);

	VstMidiEvent event;
	event.type = kVstMidiType;
	event.byteSize = sizeof(event);
	event.deltaFrames = 0;
	event.flags = 0;
	event.noteLength = 0;
	event.noteOffset = 0;
	event.detune = 0;
	event.noteOffVelocity = 0;
	event.reserved1 = 0;
	event.reserved2 = 0;
	memcpy(event.midiData, &dwMidiCode, 4);

	#ifdef VST_LOG
		Log("Sending Midi %02X.%02X.%02X\n", event.midiData[0]&0xff, event.midiData[1]&0xff, event.midiData[2]&0xff);
	#endif

	ResetSilence();
	return vstEvents.Enqueue(reinterpret_cast<VstEvent *>(&event), insertAtFront);
}


bool CVstPlugin::MidiSysexSend(const char *message, uint32 length)
//----------------------------------------------------------------
{
	VstMidiSysexEvent event;
	event.type = kVstSysExType;
	event.byteSize = sizeof(event);
	event.deltaFrames = 0;
	event.flags = 0;
	event.dumpBytes = length;
	event.resvd1 = 0;
	event.sysexDump = const_cast<char *>(message);	// We will make our own copy in VstEventQueue::Enqueue
	event.resvd2 = 0;

	ResetSilence();
	return vstEvents.Enqueue(reinterpret_cast<VstEvent *>(&event));
}


void CVstPlugin::HardAllNotesOff()
//--------------------------------
{
	float out[2][SCRATCH_BUFFER_SIZE]; // scratch buffers

	// The JUCE framework doesn't like processing while being suspended.
	const bool wasSuspended = !IsResumed();
	if(wasSuspended)
	{
		Resume();
	}

	for(uint8 mc = 0; mc < CountOf(m_MidiCh); mc++)		//all midi chans
	{
		VSTInstrChannel &channel = m_MidiCh[mc];
		channel.ResetProgram();

		MidiPitchBend(mc, EncodePitchBendParam(MIDIEvents::pitchBendCentre));		// centre pitch bend
		if(GetUID() != CCONST('K', 'L', 'W', 'V'))
		{
			// Korg Wavestation doesn't seem to like this CC, it can introduce ghost notes or
			// prevent new notes from being played.
			MidiSend(MIDIEvents::CC(MIDIEvents::MIDICC_AllControllersOff, mc, 0));		// reset all controllers
		}
		MidiSend(MIDIEvents::CC(MIDIEvents::MIDICC_AllNotesOff, mc, 0));			// all notes off
		MidiSend(MIDIEvents::CC(MIDIEvents::MIDICC_AllSoundOff, mc, 0));			// all sounds off

		for(size_t i = 0; i < CountOf(channel.noteOnMap); i++)	//all notes
		{
			for(CHANNELINDEX c = 0; c < CountOf(channel.noteOnMap[i]); c++)
			{
				while(channel.noteOnMap[i][c])
				{
					MidiSend(MIDIEvents::NoteOff(mc, static_cast<uint8>(i), 0));
					channel.noteOnMap[i][c]--;
				}
			}
		}
	}
	// let plug process events
	while(vstEvents.GetNumQueuedEvents() > 0)
	{
		Process(out[0], out[1], SCRATCH_BUFFER_SIZE);
	}

	if(wasSuspended)
	{
		Suspend();
	}
}


void CVstPlugin::MidiCC(uint8 nMidiCh, MIDIEvents::MidiCC nController, uint8 nParam, CHANNELINDEX /*trackChannel*/)
//-----------------------------------------------------------------------------------------------------------------
{
	//Error checking
	LimitMax(nController, MIDIEvents::MIDICC_end);
	LimitMax(nParam, uint8(127));

	if(m_SndFile.GetModFlag(MSF_MIDICC_BUGEMULATION))
		MidiSend(MIDIEvents::Event(MIDIEvents::evControllerChange, nMidiCh, nParam, static_cast<uint8>(nController)));	// param and controller are swapped (old broken implementation)
	else
		MidiSend(MIDIEvents::CC(nController, nMidiCh, nParam));
}


void CVstPlugin::ApplyPitchWheelDepth(int32 &value, int8 pwd)
//-----------------------------------------------------------
{
	if(pwd != 0)
	{
		value = (value * ((MIDIEvents::pitchBendMax - MIDIEvents::pitchBendCentre + 1) / 64)) / pwd;
	} else
	{
		value = 0;
	}
}


// Bend MIDI pitch for given MIDI channel using fine tracker param (one unit = 1/64th of a note step)
void CVstPlugin::MidiPitchBend(uint8 nMidiCh, int32 increment, int8 pwd)
//----------------------------------------------------------------------
{
	if(m_SndFile.GetModFlag(MSF_OLD_MIDI_PITCHBENDS))
	{
		// OpenMPT Legacy: Old pitch slides never were really accurate, but setting the PWD to 13 in plugins would give the closest results.
		increment = (increment * 0x800 * 13) / (0xFF * pwd);
		increment = EncodePitchBendParam(increment);
	} else
	{
		increment = EncodePitchBendParam(increment);
		ApplyPitchWheelDepth(increment, pwd);
	}

	int32 newPitchBendPos = (increment + m_MidiCh[nMidiCh].midiPitchBendPos) & vstPitchBendMask;
	Limit(newPitchBendPos, EncodePitchBendParam(MIDIEvents::pitchBendMin), EncodePitchBendParam(MIDIEvents::pitchBendMax));

	MidiPitchBend(nMidiCh, newPitchBendPos);
}


// Set MIDI pitch for given MIDI channel using fixed point pitch bend value (converted back to 0-16383 MIDI range)
void CVstPlugin::MidiPitchBend(uint8 nMidiCh, int32 newPitchBendPos)
//------------------------------------------------------------------
{
	ASSERT(EncodePitchBendParam(MIDIEvents::pitchBendMin) <= newPitchBendPos && newPitchBendPos <= EncodePitchBendParam(MIDIEvents::pitchBendMax));
	m_MidiCh[nMidiCh].midiPitchBendPos = newPitchBendPos;
	MidiSend(MIDIEvents::PitchBend(nMidiCh, DecodePitchBendParam(newPitchBendPos)));
}


// Apply vibrato effect through pitch wheel commands on a given MIDI channel.
void CVstPlugin::MidiVibrato(uint8 nMidiCh, int32 depth, int8 pwd)
//----------------------------------------------------------------
{
	depth = EncodePitchBendParam(depth);
	if(depth != 0 || (m_MidiCh[nMidiCh].midiPitchBendPos & vstVibratoFlag))
	{
		ApplyPitchWheelDepth(depth, pwd);

		// Temporarily add vibrato offset to current pitch
		int32 newPitchBendPos = (depth + m_MidiCh[nMidiCh].midiPitchBendPos) & vstPitchBendMask;
		Limit(newPitchBendPos, EncodePitchBendParam(MIDIEvents::pitchBendMin), EncodePitchBendParam(MIDIEvents::pitchBendMax));

		MidiSend(MIDIEvents::PitchBend(nMidiCh, DecodePitchBendParam(newPitchBendPos)));
	}

	// Update vibrato status
	if(depth != 0)
	{
		m_MidiCh[nMidiCh].midiPitchBendPos |= vstVibratoFlag;
	} else
	{
		m_MidiCh[nMidiCh].midiPitchBendPos &= ~vstVibratoFlag;
	}
}


//rewbs.introVST - many changes to MidiCommand, still to be refined.
void CVstPlugin::MidiCommand(uint8 nMidiCh, uint8 nMidiProg, uint16 wMidiBank, uint16 note, uint16 vol, CHANNELINDEX trackChannel)
//--------------------------------------------------------------------------------------------------------------------------------
{
	VSTInstrChannel &channel = m_MidiCh[nMidiCh];

	bool bankChanged = (channel.currentBank != --wMidiBank) && (wMidiBank < 0x4000);
	bool progChanged = (channel.currentProgram != --nMidiProg) && (nMidiProg < 0x80);
	//get vol in [0,128[
	uint8 volume = static_cast<uint8>(std::min(vol / 2, 127));

	// Bank change
	if(wMidiBank < 0x4000 && bankChanged)
	{
		uint8 high = static_cast<uint8>(wMidiBank >> 7);
		uint8 low = static_cast<uint8>(wMidiBank & 0x7F);

		if((channel.currentBank >> 7) != high)
		{
			// High byte changed
			MidiSend(MIDIEvents::CC(MIDIEvents::MIDICC_BankSelect_Coarse, nMidiCh, high));
		}
		// Low byte
		//GetSoundFile()->ProcessMIDIMacro(trackChannel, false, GetSoundFile()->m_MidiCfg.szMidiGlb[MIDIOUT_BANKSEL], 0);
		MidiSend(MIDIEvents::CC(MIDIEvents::MIDICC_BankSelect_Fine, nMidiCh, low));

		channel.currentBank = wMidiBank;
	}

	// Program change
	// According to the MIDI specs, a bank change alone doesn't have to change the active program - it will only change the bank of subsequent program changes.
	// Thus we send program changes also if only the bank has changed.
	if(nMidiProg < 0x80 && (progChanged || bankChanged))
	{
		channel.currentProgram = nMidiProg;
		//GetSoundFile()->ProcessMIDIMacro(trackChannel, false, GetSoundFile()->m_MidiCfg.szMidiGlb[MIDIOUT_PROGRAM], 0);
		MidiSend(MIDIEvents::ProgramChange(nMidiCh, nMidiProg));
	}


	// Specific Note Off
	if(note > NOTE_MAX_SPECIAL)
	{
		uint8 i = static_cast<uint8>(note - NOTE_MAX_SPECIAL - NOTE_MIN);
		if(channel.noteOnMap[i][trackChannel])
		{
			channel.noteOnMap[i][trackChannel]--;
			MidiSend(MIDIEvents::NoteOff(nMidiCh, i, 0));
		}
	}

	// "Hard core" All Sounds Off on this midi and tracker channel
	// This one doesn't check the note mask - just one note off per note.
	// Also less likely to cause a VST event buffer overflow.
	else if(note == NOTE_NOTECUT)	// ^^
	{
		MidiSend(MIDIEvents::CC(MIDIEvents::MIDICC_AllNotesOff, nMidiCh, 0));
		MidiSend(MIDIEvents::CC(MIDIEvents::MIDICC_AllSoundOff, nMidiCh, 0));

		// Turn off all notes
		for(uint8 i = 0; i < CountOf(channel.noteOnMap); i++)
		{
			channel.noteOnMap[i][trackChannel] = 0;
			MidiSend(MIDIEvents::NoteOff(nMidiCh, i, volume));
		}

	}

	// All "active" notes off on this midi and tracker channel
	// using note mask.
	else if(note == NOTE_KEYOFF || note == NOTE_FADE) // ==, ~~
	{
		for(uint8 i = 0; i < CountOf(channel.noteOnMap); i++)
		{
			// Some VSTis need a note off for each instance of a note on, e.g. fabfilter.
			while(channel.noteOnMap[i][trackChannel])
			{
				MidiSend(MIDIEvents::NoteOff(nMidiCh, i, volume));
				channel.noteOnMap[i][trackChannel]--;
			}
		}
	}

	// Note On
	else if(ModCommand::IsNote(static_cast<ModCommand::NOTE>(note)))
	{
		note -= NOTE_MIN;

		// Reset pitch bend on each new note, tracker style.
		// This is done if the pitch wheel has been moved or there was a vibrato on the previous row (in which case the "vstVibratoFlag" bit of the pitch bend memory is set)
		if(m_MidiCh[nMidiCh].midiPitchBendPos != EncodePitchBendParam(MIDIEvents::pitchBendCentre))
		{
			MidiPitchBend(nMidiCh, EncodePitchBendParam(MIDIEvents::pitchBendCentre));
		}

		// count instances of active notes.
		// This is to send a note off for each instance of a note, for plugs like Fabfilter.
		// Problem: if a note dies out naturally and we never send a note off, this counter
		// will block at max until note off. Is this a problem?
		// Safe to assume we won't need more than 16 note offs max on a given note?
		if(channel.noteOnMap[note][trackChannel] < 17)
			channel.noteOnMap[note][trackChannel]++;

		MidiSend(MIDIEvents::NoteOn(nMidiCh, static_cast<uint8>(note), volume));
	}
}


bool CVstPlugin::isPlaying(UINT note, UINT midiChn, UINT trackerChn)
//------------------------------------------------------------------
{
	note -= NOTE_MIN;
	return (m_MidiCh[midiChn].noteOnMap[note][trackerChn] != 0);
}


void CVstPlugin::SetZxxParameter(UINT nParam, UINT nValue)
//--------------------------------------------------------
{
	PlugParamValue fValue = (PlugParamValue)nValue / 127.0f;
	SetParameter(nParam, fValue);
}

//rewbs.smoothVST
UINT CVstPlugin::GetZxxParameter(UINT nParam)
//-------------------------------------------
{
	return (UINT) (GetParameter(nParam) * 127.0f + 0.5f);
}
//end rewbs.smoothVST


// Automate a parameter from the plugin GUI (both custom and default plugin GUI)
void CVstPlugin::AutomateParameter(PlugParamIndex param)
//------------------------------------------------------
{
#ifdef MODPLUG_TRACKER
	CModDoc* pModDoc = GetModDoc();
	if(pModDoc == nullptr)
	{
		return;
	}

	if (m_bRecordAutomation)
	{
		// Record parameter change
		pModDoc->RecordParamChange(GetSlot(), param);
	}

	CAbstractVstEditor *pVstEditor = GetEditor();

	if(pVstEditor && pVstEditor->m_hWnd)
	{
		// Mark track modified if GUI is open and format supports plugins
		if(pModDoc->GetrSoundFile().GetModSpecifications().supportsPlugins)
		{
			CMainFrame::GetMainFrame()->ThreadSafeSetModified(pModDoc);
		}

		// TODO: Could be used to update general tab in real time, but causes flickers in treeview
		// Better idea: add an update hint just for plugin params?
		//pModDoc->UpdateAllViews(nullptr, HINT_MIXPLUGINS, nullptr);

		if (CMainFrame::GetInputHandler()->ShiftPressed())
		{
			// Shift pressed -> Open MIDI mapping dialog
			CMainFrame::GetInputHandler()->SetModifierMask(0); // Make sure that the dialog will open only once.

			CMIDIMappingDialog dlg(pVstEditor, pModDoc->GetrSoundFile());
			dlg.m_Setting.SetParamIndex(param);
			dlg.m_Setting.SetPlugIndex(GetSlot() + 1);
			dlg.DoModal();
		}

		// Learn macro
		int macroToLearn = pVstEditor->GetLearnMacro();
		if (macroToLearn > -1)
		{
			pModDoc->LearnMacro(macroToLearn, param);
			pVstEditor->SetLearnMacro(-1);
		}
	}
#endif // MODPLUG_TRACKER
}


void CVstPlugin::SaveAllParameters()
//----------------------------------
{
	if (m_pMixStruct)
	{
		m_pMixStruct->defaultProgram = -1;

		if(ProgramsAreChunks() && Dispatch(effIdentify, 0,0, nullptr, 0.0f) == 'NvEf')
		{
			void *p = nullptr;

			// Try to get whole bank
			uint32 nByteSize = mpt::saturate_cast<uint32>(Dispatch(effGetChunk, 0,0, &p, 0));

			if (!p)
			{
				nByteSize = mpt::saturate_cast<uint32>(Dispatch(effGetChunk, 1,0, &p, 0)); 	// Getting bank failed, try to get just preset
			} else
			{
				m_pMixStruct->defaultProgram = GetCurrentProgram();	//we managed to get the bank, now we need to remember which program we're on.
			}
			if (p != nullptr)
			{
				LimitMax(nByteSize, Util::MaxValueOfType(nByteSize) - 4);
				if ((m_pMixStruct->pPluginData) && (m_pMixStruct->nPluginDataSize >= nByteSize + 4))
				{
					m_pMixStruct->nPluginDataSize = nByteSize + 4;
				} else
				{
					delete[] m_pMixStruct->pPluginData;
					m_pMixStruct->nPluginDataSize = 0;
					m_pMixStruct->pPluginData = new char[nByteSize + 4];
					if (m_pMixStruct->pPluginData)
					{
						m_pMixStruct->nPluginDataSize = nByteSize + 4;
					}
				}
				if (m_pMixStruct->pPluginData)
				{
					*(uint32 *)m_pMixStruct->pPluginData = 'NvEf';
					memcpy(m_pMixStruct->pPluginData + 4, p, nByteSize);
					return;
				}
			}
		}
		// This plug doesn't support chunks: save parameters
		PlugParamIndex nParams = (m_Effect.numParams > 0) ? m_Effect.numParams : 0;
		UINT nLen = nParams * sizeof(float);
		if (!nLen) return;
		nLen += 4;
		if ((m_pMixStruct->pPluginData) && (m_pMixStruct->nPluginDataSize >= nLen))
		{
			m_pMixStruct->nPluginDataSize = nLen;
		} else
		{
			if (m_pMixStruct->pPluginData) delete[] m_pMixStruct->pPluginData;
			m_pMixStruct->nPluginDataSize = 0;
			m_pMixStruct->pPluginData = new char[nLen];
			if (m_pMixStruct->pPluginData)
			{
				m_pMixStruct->nPluginDataSize = nLen;
			}
		}
		if (m_pMixStruct->pPluginData)
		{
			float *p = (float *)m_pMixStruct->pPluginData;
			*(ULONG *)p = 0;
			p++;
			for(PlugParamIndex i = 0; i < nParams; i++)
			{
				p[i] = GetParameter(i);
			}
		}
	}
	return;
}


void CVstPlugin::RestoreAllParameters(long nProgram)
//--------------------------------------------------
{
	if(m_pMixStruct != nullptr && m_pMixStruct->pPluginData != nullptr && m_pMixStruct->nPluginDataSize >= 4)
	{
		UINT nParams = (m_Effect.numParams > 0) ? m_Effect.numParams : 0;
		UINT nLen = nParams * sizeof(float);
		ULONG nType = *(ULONG *)m_pMixStruct->pPluginData;

		if ((Dispatch(effIdentify, 0, 0, nullptr, 0) == 'NvEf') && (nType == 'NvEf'))
		{
			if ((nProgram>=0) && (nProgram < m_Effect.numPrograms))
			{
				// Bank
				Dispatch(effSetChunk, 0, m_pMixStruct->nPluginDataSize - 4, m_pMixStruct->pPluginData + 4, 0);
				SetCurrentProgram(nProgram);
			} else
			{
				// Program
				Dispatch(effBeginSetProgram, 0, 0, nullptr, 0.0f);
				Dispatch(effSetChunk, 1, m_pMixStruct->nPluginDataSize - 4, m_pMixStruct->pPluginData + 4, 0);
				Dispatch(effEndSetProgram, 0, 0, nullptr, 0.0f);
			}

		} else
		{
			Dispatch(effBeginSetProgram, 0, 0, nullptr, 0.0f);
			float *p = (float *)m_pMixStruct->pPluginData;
			if (m_pMixStruct->nPluginDataSize >= nLen + 4) p++;
			if (m_pMixStruct->nPluginDataSize >= nLen)
			{
				for (UINT i = 0; i < nParams; i++)
				{
					SetParameter(i, p[i]);
				}
			}
			Dispatch(effEndSetProgram, 0, 0, nullptr, 0.0f);
		}
	}
}


void CVstPlugin::ToggleEditor()
//-----------------------------
{
	try
	{
		if ((m_pEditor) && (!m_pEditor->m_hWnd))
		{
			delete m_pEditor;
			m_pEditor = nullptr;
		}
		if (m_pEditor)
		{
			if (m_pEditor->m_hWnd) m_pEditor->DoClose();
			if ((volatile void *)m_pEditor) delete m_pEditor;
			m_pEditor = nullptr;
		} else
		{
			if (HasEditor())
				m_pEditor =  new COwnerVstEditor(*this);
			else
				m_pEditor = new CDefaultVstEditor(*this);

			if (m_pEditor)
				m_pEditor->OpenEditor(CMainFrame::GetMainFrame());
		}
	} catch (...)
	{
		ReportPlugException(L"Exception in ToggleEditor()");
	}
}


void CVstPlugin::Bypass(bool bypass)
//----------------------------------
{
	m_pMixStruct->Info.SetBypass(bypass);

	Dispatch(effSetBypass, bypass ? 1 : 0, 0, nullptr, 0.0f);

#ifdef MODPLUG_TRACKER
	if(m_SndFile.GetpModDoc())
		m_SndFile.GetpModDoc()->UpdateAllViews(nullptr, HINT_MIXPLUGINS, nullptr);
#endif // MODPLUG_TRACKER
}


void CVstPlugin::NotifySongPlaying(bool playing)
//----------------------------------------------
{
	m_bSongPlaying = playing;
}


PLUGINDEX CVstPlugin::FindSlot()
//------------------------------
{
	PLUGINDEX slot = 0;
	while(m_pMixStruct != &(m_SndFile.m_MixPlugins[slot]) && slot < MAX_MIXPLUGINS - 1)
	{
		slot++;
	}
	return slot;
}


void CVstPlugin::SetSlot(PLUGINDEX slot)
//--------------------------------------
{
	m_nSlot = slot;
}


PLUGINDEX CVstPlugin::GetSlot()
//-----------------------------
{
	return m_nSlot;
}


void CVstPlugin::UpdateMixStructPtr(SNDMIXPLUGIN *p)
//--------------------------------------------------
{
	m_pMixStruct = p;
}


bool CVstPlugin::isInstrument()
//-----------------------------
{
	return ((m_Effect.flags & effFlagsIsSynth) || (!m_Effect.numInputs));
}


bool CVstPlugin::CanRecieveMidiEvents()
//-------------------------------------
{
	return (CVstPlugin::Dispatch(effCanDo, 0, 0, "receiveVstMidiEvent", 0.0f) != 0);
}


// Get list of plugins to which output is sent. A nullptr indicates master output.
size_t CVstPlugin::GetOutputPlugList(std::vector<CVstPlugin *> &list)
//-------------------------------------------------------------------
{
	// At the moment we know there will only be 1 output.
	// Returning nullptr means plugin outputs directly to master.
	list.clear();

	CVstPlugin *outputPlug = nullptr;
	if(!m_pMixStruct->IsOutputToMaster())
	{
		PLUGINDEX nOutput = m_pMixStruct->GetOutputPlugin();
		if(nOutput > m_nSlot && nOutput != PLUGINDEX_INVALID)
		{
			outputPlug = dynamic_cast<CVstPlugin *>(m_SndFile.m_MixPlugins[nOutput].pMixPlugin);
		}
	}
	list.push_back(outputPlug);

	return 1;
}


// Get a list of plugins that send data to this plugin.
size_t CVstPlugin::GetInputPlugList(std::vector<CVstPlugin *> &list)
//------------------------------------------------------------------
{
	std::vector<CVstPlugin *> candidatePlugOutputs;
	list.clear();

	for(PLUGINDEX plug = 0; plug < MAX_MIXPLUGINS; plug++)
	{
		CVstPlugin *candidatePlug = dynamic_cast<CVstPlugin *>(m_SndFile.m_MixPlugins[plug].pMixPlugin);
		if(candidatePlug)
		{
			candidatePlug->GetOutputPlugList(candidatePlugOutputs);

			for(std::vector<CVstPlugin *>::iterator iter = candidatePlugOutputs.begin(); iter != candidatePlugOutputs.end(); iter++)
			{
				if(*iter == this)
				{
					list.push_back(candidatePlug);
					break;
				}
			}
		}
	}

	return list.size();
}


// Get a list of instruments that send data to this plugin.
size_t CVstPlugin::GetInputInstrumentList(std::vector<INSTRUMENTINDEX> &list)
//---------------------------------------------------------------------------
{
	list.clear();
	const PLUGINDEX nThisMixPlug = m_nSlot + 1;		//m_nSlot is position in mixplug array.

	for(INSTRUMENTINDEX ins = 0; ins <= m_SndFile.GetNumInstruments(); ins++)
	{
		if(m_SndFile.Instruments[ins] != nullptr && m_SndFile.Instruments[ins]->nMixPlug == nThisMixPlug)
		{
			list.push_back(ins);
		}
	}

	return list.size();
}


size_t CVstPlugin::GetInputChannelList(std::vector<CHANNELINDEX> &list)
//---------------------------------------------------------------------
{
	list.clear();

	PLUGINDEX nThisMixPlug = m_nSlot + 1;		//m_nSlot is position in mixplug array.
	const CHANNELINDEX chnCount = m_SndFile.GetNumChannels();
	for(CHANNELINDEX nChn=0; nChn<chnCount; nChn++)
	{
		if(m_SndFile.ChnSettings[nChn].nMixPlugin == nThisMixPlug)
		{
			list.push_back(nChn);
		}
	}

	return list.size();

}


void CVstPlugin::ReportPlugException(std::wstring text) const
//-----------------------------------------------------------
{
	text += L" (Plugin=" + m_Factory.libraryName.ToWide() + L")";
	CVstPluginManager::ReportPlugException(text);
}


#endif // NO_VST


std::string SNDMIXPLUGIN::GetParamName(PlugParamIndex index) const
//------------------------------------------------------------
{
	CVstPlugin *vstPlug = dynamic_cast<CVstPlugin *>(pMixPlugin);
	if(vstPlug != nullptr)
	{
		return vstPlug->GetParamName(index).GetString();
	} else
	{
		return std::string();
	}
}


OPENMPT_NAMESPACE_END
