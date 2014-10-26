/*
 * ModDoc.cpp
 * ----------
 * Purpose: Module document handling in OpenMPT.
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"
#include "mptrack.h"
#include "mainfrm.h"
#include "InputHandler.h"
#include "moddoc.h"
#include "childfrm.h"
#include "mpdlgs.h"
#include "dlg_misc.h"
#include "Dlsbank.h"
#include "mod2wave.h"
#include "StreamEncoderFLAC.h"
#include "StreamEncoderMP3.h"
#include "StreamEncoderOpus.h"
#include "StreamEncoderVorbis.h"
#include "StreamEncoderWAV.h"
#include "mod2midi.h"
#include "vstplug.h"
#include "../common/version.h"
#include "modsmp_ctrl.h"
#include "CleanupSong.h"
#include "../common/StringFixer.h"
#include "../common/mptFileIO.h"
#include "../soundlib/FileReader.h"
#include <shlwapi.h>
#include "FileDialog.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


OPENMPT_NAMESPACE_BEGIN


const TCHAR FileFilterMOD[]	= _T("ProTracker Modules (*.mod)|*.mod||");
const TCHAR FileFilterXM[]	= _T("FastTracker Modules (*.xm)|*.xm||");
const TCHAR FileFilterS3M[] = _T("ScreamTracker Modules (*.s3m)|*.s3m||");
const TCHAR FileFilterIT[]	= _T("Impulse Tracker Modules (*.it)|*.it||");
const TCHAR FileFilterMPT[] = _T("OpenMPT Modules (*.mptm)|*.mptm||");
const TCHAR FileFilterNone[] = _T("");

const TCHAR* ModTypeToFilter(const CSoundFile& sndFile)
//-----------------------------------------------------
{
	const MODTYPE modtype = sndFile.GetType();
	switch(modtype)
	{
		case MOD_TYPE_MOD: return FileFilterMOD;
		case MOD_TYPE_XM: return FileFilterXM;
		case MOD_TYPE_S3M: return FileFilterS3M;
		case MOD_TYPE_IT: return FileFilterIT;
		case MOD_TYPE_MPT: return FileFilterMPT;
		default: return FileFilterNone;
	}
}

/////////////////////////////////////////////////////////////////////////////
// CModDoc

IMPLEMENT_SERIAL(CModDoc, CDocument, 0 /* schema number*/ )

BEGIN_MESSAGE_MAP(CModDoc, CDocument)
	//{{AFX_MSG_MAP(CModDoc)
	ON_COMMAND(ID_FILE_SAVEASTEMPLATE,	OnSaveTemplateModule)
	ON_COMMAND(ID_FILE_SAVEASWAVE,		OnFileWaveConvert)
	ON_COMMAND(ID_FILE_SAVEASMP3,		OnFileMP3Convert)
	ON_COMMAND(ID_FILE_SAVEMIDI,		OnFileMidiConvert)
	ON_COMMAND(ID_FILE_SAVECOMPAT,		OnFileCompatibilitySave)
	ON_COMMAND(ID_FILE_APPENDMODULE,	OnAppendModule)
	ON_COMMAND(ID_PLAYER_PLAY,			OnPlayerPlay)
	ON_COMMAND(ID_PLAYER_PAUSE,			OnPlayerPause)
	ON_COMMAND(ID_PLAYER_STOP,			OnPlayerStop)
	ON_COMMAND(ID_PLAYER_PLAYFROMSTART,	OnPlayerPlayFromStart)
	ON_COMMAND(ID_VIEW_GLOBALS,			OnEditGlobals)
	ON_COMMAND(ID_VIEW_PATTERNS,		OnEditPatterns)
	ON_COMMAND(ID_VIEW_SAMPLES,			OnEditSamples)
	ON_COMMAND(ID_VIEW_INSTRUMENTS,		OnEditInstruments)
	ON_COMMAND(ID_VIEW_COMMENTS,		OnEditComments)
	ON_COMMAND(ID_VIEW_GRAPH,			OnEditGraph) //rewbs.graph
	ON_COMMAND(ID_VIEW_EDITHISTORY,		OnViewEditHistory)
	ON_COMMAND(ID_VIEW_MPTHACKS,		OnViewMPTHacks)
	ON_COMMAND(ID_INSERT_PATTERN,		OnInsertPattern)
	ON_COMMAND(ID_INSERT_SAMPLE,		OnInsertSample)
	ON_COMMAND(ID_INSERT_INSTRUMENT,	OnInsertInstrument)
	ON_COMMAND(ID_EDIT_CLEANUP,			OnShowCleanup)

	ON_COMMAND(ID_ESTIMATESONGLENGTH,	OnEstimateSongLength)
	ON_COMMAND(ID_APPROX_BPM,			OnApproximateBPM)
	ON_COMMAND(ID_PATTERN_PLAY,			OnPatternPlay)				//rewbs.patPlayAllViews
	ON_COMMAND(ID_PATTERN_PLAYNOLOOP,	OnPatternPlayNoLoop)		//rewbs.patPlayAllViews
	ON_COMMAND(ID_PATTERN_RESTART,		OnPatternRestart)			//rewbs.patPlayAllViews
	ON_UPDATE_COMMAND_UI(ID_INSERT_INSTRUMENT,		OnUpdateXMITMPTOnly)
	ON_UPDATE_COMMAND_UI(ID_VIEW_INSTRUMENTS,		OnUpdateXMITMPTOnly)
	//ON_UPDATE_COMMAND_UI(ID_VIEW_COMMENTS,			OnUpdateXMITMPTOnly)
	ON_UPDATE_COMMAND_UI(ID_VIEW_MIDIMAPPING,		OnUpdateHasMIDIMappings)
	ON_UPDATE_COMMAND_UI(ID_VIEW_EDITHISTORY,		OnUpdateITMPTOnly)
	ON_UPDATE_COMMAND_UI(ID_FILE_SAVECOMPAT,		OnUpdateCompatExportableOnly)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


////////////////////////////////////////////////////////////////////////////
// CModDoc diagnostics

#ifdef _DEBUG
void CModDoc::AssertValid() const
{
	CDocument::AssertValid();
}

void CModDoc::Dump(CDumpContext& dc) const
{
	CDocument::Dump(dc);
}
#endif //_DEBUG


/////////////////////////////////////////////////////////////////////////////
// CModDoc construction/destruction

CModDoc::CModDoc() : m_LogMode(LogModeInstantReporting), m_PatternUndo(*this), m_SampleUndo(*this)
//------------------------------------------------------------------------------------------------
{
	m_bHasValidPath = false;
	m_hWndFollow = NULL;

	m_notifyType = Notification::Default;
	m_notifyItem = 0;

	// Set the creation date of this file (or the load time if we're loading an existing file)
	time(&m_creationTime);

#ifdef _DEBUG
	ModChannel *p = m_SndFile.m_PlayState.Chn;
	if (((DWORD)p) & 7) Log("ModChannel struct is not aligned (0x%08X)\n", p);
#endif
// Fix: save pattern scrollbar position when switching to other tab
	m_szOldPatternScrollbarsPos = CSize(-10,-10);
// -> CODE#0015
// -> DESC="channels management dlg"
	ReinitRecordState();
// -! NEW_FEATURE#0015
	m_ShowSavedialog = false;

	CMainFrame::UpdateAudioParameters(m_SndFile, true);
}


CModDoc::~CModDoc()
//-----------------
{
	ClearLog();
}


void CModDoc::SetModifiedFlag(BOOL bModified)
//-------------------------------------------
{
	BOOL bChanged = (bModified != IsModified());
	CDocument::SetModifiedFlag(bModified);
	if (bChanged) UpdateFrameCounts();
}


BOOL CModDoc::OnNewDocument()
//---------------------------
{
	if (!CDocument::OnNewDocument()) return FALSE;

	m_SndFile.Create(FileReader(), CSoundFile::loadCompleteModule, this);
	m_SndFile.ChangeModTypeTo(CTrackApp::GetDefaultDocType());

	theApp.GetDefaultMidiMacro(m_SndFile.m_MidiCfg);
	m_SndFile.m_SongFlags.set(SONG_LINEARSLIDES & m_SndFile.GetModSpecifications().songFlags);
	if(!m_SndFile.m_MidiCfg.IsMacroDefaultSetupUsed())
	{
		m_SndFile.m_SongFlags.set(SONG_EMBEDMIDICFG & m_SndFile.GetModSpecifications().songFlags);
	}


	ReinitRecordState();
	InitializeMod();
	SetModifiedFlag(FALSE);
	return TRUE;
}


BOOL CModDoc::OnOpenDocument(const mpt::PathString &filename)
//-----------------------------------------------------------
{
	BOOL bModified = TRUE;

	ScopedLogCapturer logcapturer(*this);

	if(filename.empty()) return OnNewDocument();

	BeginWaitCursor();

	InputFile f(filename);
	if(f.IsValid())
	{
		FileReader file = GetFileReader(f);
		ASSERT(GetPathNameMpt() == mpt::PathString());
		SetPathName(filename, FALSE);	// Path is not set yet, but ITP loader needs this for relative paths.
		m_SndFile.Create(file, CSoundFile::loadCompleteModule, this);
	}

	EndWaitCursor();

	logcapturer.ShowLog(std::wstring()
		+ L"File: " + filename.ToWide() + L"\n"
		+ L"Last saved with: " + mpt::ToWide(mpt::CharsetLocale, m_SndFile.madeWithTracker) + L", you are using OpenMPT " + mpt::ToWide(mpt::CharsetUTF8, MptVersion::str) + L"\n"
		+ L"\n"
		);

	if ((m_SndFile.m_nType == MOD_TYPE_NONE) || (!m_SndFile.GetNumChannels())) return FALSE;
	// Midi Import
	if (m_SndFile.m_nType == MOD_TYPE_MID)
	{
		CDLSBank *pCachedBank = NULL, *pEmbeddedBank = NULL;
		mpt::PathString szCachedBankFile = MPT_PATHSTRING("");

		if (CDLSBank::IsDLSBank(filename))
		{
			pEmbeddedBank = new CDLSBank();
			pEmbeddedBank->Open(filename);
		}
		m_SndFile.ChangeModTypeTo(MOD_TYPE_IT);
		BeginWaitCursor();
		MIDILIBSTRUCT &midiLib = CTrackApp::GetMidiLibrary();
		// Scan Instruments
		for (INSTRUMENTINDEX nIns = 1; nIns <= m_SndFile.m_nInstruments; nIns++) if (m_SndFile.Instruments[nIns])
		{
			mpt::PathString pszMidiMapName;
			ModInstrument *pIns = m_SndFile.Instruments[nIns];
			UINT nMidiCode;
			BOOL bEmbedded = FALSE;

			if (pIns->nMidiChannel == 10)
				nMidiCode = 0x80 | (pIns->nMidiDrumKey & 0x7F);
			else
				nMidiCode = pIns->nMidiProgram & 0x7F;
			pszMidiMapName = midiLib.MidiMap[nMidiCode];
			if (pEmbeddedBank)
			{
				UINT nDlsIns = 0, nDrumRgn = 0;
				UINT nProgram = pIns->nMidiProgram;
				UINT dwKey = (nMidiCode < 128) ? 0xFF : (nMidiCode & 0x7F);
				if ((pEmbeddedBank->FindInstrument(	(nMidiCode >= 128),
													(pIns->wMidiBank & 0x3FFF),
													nProgram, dwKey, &nDlsIns))
				 || (pEmbeddedBank->FindInstrument(	(nMidiCode >= 128),	0xFFFF,
													(nMidiCode >= 128) ? 0xFF : nProgram,
													dwKey, &nDlsIns)))
				{
					if (dwKey < 0x80) nDrumRgn = pEmbeddedBank->GetRegionFromKey(nDlsIns, dwKey);
					if (pEmbeddedBank->ExtractInstrument(m_SndFile, nIns, nDlsIns, nDrumRgn))
					{
						pIns = m_SndFile.Instruments[nIns]; // Reset pIns because ExtractInstrument may delete the previous value.
						if ((dwKey >= 24) && (dwKey < 100))
						{
							mpt::String::CopyN(pIns->name, szMidiPercussionNames[dwKey - 24]);
						}
						bEmbedded = TRUE;
					}
					else
						pIns = m_SndFile.Instruments[nIns]; // Reset pIns because ExtractInstrument may delete the previous value.
				}
			}
			if((!pszMidiMapName.empty()) && (!bEmbedded))
			{
				// Load From DLS Bank
				if (CDLSBank::IsDLSBank(pszMidiMapName))
				{
					CDLSBank *pDLSBank = NULL;
					
					if ((pCachedBank) && (!mpt::PathString::CompareNoCase(szCachedBankFile, pszMidiMapName)))
					{
						pDLSBank = pCachedBank;
					} else
					{
						if (pCachedBank) delete pCachedBank;
						pCachedBank = new CDLSBank;
						szCachedBankFile = pszMidiMapName;
						if (pCachedBank->Open(pszMidiMapName)) pDLSBank = pCachedBank;
					}
					if (pDLSBank)
					{
						UINT nDlsIns = 0, nDrumRgn = 0;
						UINT nProgram = pIns->nMidiProgram;
						UINT dwKey = (nMidiCode < 128) ? 0xFF : (nMidiCode & 0x7F);
						if ((pDLSBank->FindInstrument(	(nMidiCode >= 128),
														(pIns->wMidiBank & 0x3FFF),
														nProgram, dwKey, &nDlsIns))
						 || (pDLSBank->FindInstrument(	(nMidiCode >= 128), 0xFFFF,
														(nMidiCode >= 128) ? 0xFF : nProgram,
														dwKey, &nDlsIns)))
						{
							if (dwKey < 0x80) nDrumRgn = pDLSBank->GetRegionFromKey(nDlsIns, dwKey);
							pDLSBank->ExtractInstrument(m_SndFile, nIns, nDlsIns, nDrumRgn);
							pIns = m_SndFile.Instruments[nIns]; // Reset pIns because ExtractInstrument may delete the previous value.
							if ((dwKey >= 24) && (dwKey < 24+61))
							{
								mpt::String::CopyN(pIns->name, szMidiPercussionNames[dwKey-24]);
							}
						}
					}
				} else
				{
					// Load from Instrument or Sample file
					InputFile f;

					if(f.Open(pszMidiMapName))
					{
						FileReader file = GetFileReader(f);
						if(file.IsValid())
						{
							m_SndFile.ReadInstrumentFromFile(nIns, file, false);
							mpt::PathString szName, szExt;
							pszMidiMapName.SplitPath(nullptr, nullptr, &szName, &szExt);
							szName += szExt;
							pIns = m_SndFile.Instruments[nIns];
							if (!pIns->filename[0]) mpt::String::Copy(pIns->filename, szName.ToLocale().c_str());
							if (!pIns->name[0])
							{
								if (nMidiCode < 128)
								{
									mpt::String::CopyN(pIns->name, szMidiProgramNames[nMidiCode]);
								} else
								{
									UINT nKey = nMidiCode & 0x7F;
									if (nKey >= 24)
										mpt::String::CopyN(pIns->name, szMidiPercussionNames[nKey - 24]);
								}
							}
						}
					}
				}
			}
		}
		if (pCachedBank) delete pCachedBank;
		if (pEmbeddedBank) delete pEmbeddedBank;
		EndWaitCursor();
	}
	// Convert to MOD/S3M/XM/IT
	switch(m_SndFile.GetType())
	{
	case MOD_TYPE_MOD:
	case MOD_TYPE_S3M:
	case MOD_TYPE_XM:
	case MOD_TYPE_IT:
	case MOD_TYPE_MPT:
		bModified = FALSE;
		break;
	default:
		m_SndFile.ChangeModTypeTo(m_SndFile.GetBestSaveFormat());
	}

// -> CODE#0015
// -> DESC="channels management dlg"
	ReinitRecordState();
// -! NEW_FEATURE#0015

	// Show warning if file was made with more recent version of OpenMPT except
	if(MptVersion::RemoveBuildNumber(m_SndFile.m_dwLastSavedWithVersion) > MptVersion::num)
	{
		char s[256];
		wsprintf(s, "Warning: this song was last saved with a more recent version of OpenMPT.\r\nSong saved with: v%s. Current version: v%s.\r\n", 
			MptVersion::ToStr(m_SndFile.m_dwLastSavedWithVersion).c_str(),
			MptVersion::str);
		Reporting::Notification(s);
	}

	SetModifiedFlag(FALSE); // (bModified);
	m_bHasValidPath=true;

	return TRUE;
}


BOOL CModDoc::OnSaveDocument(const mpt::PathString &filename, const bool bTemplateFile)
//-------------------------------------------------------------------------------------
{
	ScopedLogCapturer logcapturer(*this);
	static int greccount = 0;
	BOOL bOk = FALSE;
	m_SndFile.m_dwLastSavedWithVersion = MptVersion::num;
	if(filename.empty()) 
		return FALSE;
	MODTYPE type = m_SndFile.GetType(); // CModSpecifications::ExtensionToType(fext);

	if (type == MOD_TYPE_NONE && !greccount)
	{
		greccount++;
		bOk = DoSave(mpt::PathString(), TRUE);
		greccount--;
		return bOk;
	}
	BeginWaitCursor();
	FixNullStrings();
	switch(type)
	{
	case MOD_TYPE_MOD:	bOk = m_SndFile.SaveMod(filename); break;
	case MOD_TYPE_S3M:	bOk = m_SndFile.SaveS3M(filename); break;
	case MOD_TYPE_XM:	bOk = m_SndFile.SaveXM(filename); break;
	case MOD_TYPE_IT:	bOk = m_SndFile.SaveIT(filename); break;
	case MOD_TYPE_MPT:	bOk = m_SndFile.SaveIT(filename); break;
	}
	EndWaitCursor();
	if (bOk)
	{
		if (!bTemplateFile)
		{
			// Set new path for this file, unless we are saving a template, in which case we want to keep the old file path.
			SetPathName(filename);
		}
		logcapturer.ShowLog(true);
		if (bTemplateFile)
		{
			// Update template menu.
			CMainFrame* const pMainFrame = CMainFrame::GetMainFrame();
			if (pMainFrame)
				pMainFrame->CreateTemplateModulesMenu();
		}
	} else
	{
		ErrorBox(IDS_ERR_SAVESONG, CMainFrame::GetMainFrame());
	}
	return bOk;
}


BOOL CModDoc::SaveModified()
//--------------------------
{
	BOOL result = CDocument::SaveModified();
	if(result && m_SndFile.GetType() == MOD_TYPE_MPT)
	{
		std::wstring prompt = L"The following external samples have been modified:\n";
		bool modified = false;
		for(SAMPLEINDEX i = 1; i <= m_SndFile.GetNumSamples(); i++)
		{
			if(m_SndFile.GetSample(i).uFlags.test_all(SMP_KEEPONDISK | SMP_MODIFIED))
			{
				modified = true;
				prompt += mpt::wfmt::dec0<2>(i) + L": " + m_SndFile.GetSamplePath(i).ToWide() + L"\n";
			}
		}

		ConfirmAnswer ans = cnfYes;
		if(modified && (ans = Reporting::Confirm(prompt + L"Do you want to save them?", L"External Samples", true)) == cnfYes)
		{
			for(SAMPLEINDEX i = 1; i <= m_SndFile.GetNumSamples(); i++)
			{
				if(m_SndFile.GetSample(i).uFlags.test_all(SMP_KEEPONDISK | SMP_MODIFIED))
				{
					SaveSample(i);
				}
			}
		} else if(ans == cnfCancel)
		{
			result = FALSE;
		}
	}

	return result;
}


bool CModDoc::SaveSample(SAMPLEINDEX smp)
//---------------------------------------
{
	bool success = false;
	if(smp > 0 && smp <= GetNumSamples())
	{
		const mpt::PathString filename = m_SndFile.GetSamplePath(smp);
		if(!filename.empty())
		{
			const mpt::PathString dotExt = filename.GetFileExt();
			const bool wav = !mpt::PathString::CompareNoCase(dotExt, MPT_PATHSTRING(".wav"));
			const bool flac  = !mpt::PathString::CompareNoCase(dotExt, MPT_PATHSTRING(".flac"));

			if(flac || (!wav && TrackerSettings::Instance().m_defaultSampleFormat != dfWAV))
				success = m_SndFile.SaveFLACSample(smp, filename);
			else
				success = m_SndFile.SaveWAVSample(smp, filename);

			if(success)
				m_SndFile.GetSample(smp).uFlags.reset(SMP_MODIFIED);
			else
				Reporting::Error(L"Unable to save sample:\n" + filename.ToWide());
		}
	}
	return success;
}


void CModDoc::OnCloseDocument()
//-----------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	if (pMainFrm) pMainFrm->OnDocumentClosed(this);
	CDocument::OnCloseDocument();
}


void CModDoc::DeleteContents()
//----------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	if (pMainFrm) pMainFrm->StopMod(this);
	m_SndFile.Destroy();
// -> CODE#0015
// -> DESC="channels management dlg"
	ReinitRecordState();
// -! NEW_FEATURE#0015
}


#ifndef UNICODE
BOOL CModDoc::DoFileSave()
//------------------------
{
	// Completely replaces MFC implementation.
	DWORD dwAttrib = GetFileAttributesW(mpt::PathString::TunnelOutofCString(m_strPathName).AsNative().c_str());
	if(dwAttrib & FILE_ATTRIBUTE_READONLY)
	{
		if(!DoSave(mpt::PathString()))
		{
			return FALSE;
		}
	} else
	{
		if(!DoSave(mpt::PathString::TunnelOutofCString(m_strPathName)))
		{
			return FALSE;
		}
	}
	return TRUE;
}
#endif


BOOL CModDoc::DoSave(const mpt::PathString &filename, BOOL)
//---------------------------------------------------------
{
	const mpt::PathString docFileName = GetPathNameMpt();
	const std::string defaultExtension = m_SndFile.GetModSpecifications().fileExtension;
	
	switch(m_SndFile.GetBestSaveFormat())
	{
	case MOD_TYPE_MOD:
		MsgBoxHidable(ModSaveHint);
		break;
	case MOD_TYPE_S3M:
		break;
	case MOD_TYPE_XM:
		MsgBoxHidable(XMCompatibilityExportTip);
		break;
	case MOD_TYPE_IT:
		MsgBoxHidable(ItCompatibilityExportTip);
		break;
	case MOD_TYPE_MPT:
		break;
	default:	
		ErrorBox(IDS_ERR_SAVESONG, CMainFrame::GetMainFrame());
		return FALSE;
	}

	mpt::PathString ext = MPT_PATHSTRING(".") + mpt::PathString::FromUTF8(defaultExtension);

	mpt::PathString saveFileName;

	if(filename.empty() || m_ShowSavedialog)
	{
		mpt::PathString drive = docFileName.GetDrive();
		mpt::PathString dir = docFileName.GetDir();
		mpt::PathString fileName = docFileName.GetFileName();
		if(fileName.empty())
		{
			fileName = mpt::PathString::FromCStringSilent(GetTitle()).SanitizeComponent();
		}
		mpt::PathString defaultSaveName = drive + dir + fileName + ext;
		
		FileDialog dlg = SaveFileDialog()
			.DefaultExtension(defaultExtension)
			.DefaultFilename(defaultSaveName)
			.ExtensionFilter(ModTypeToFilter(m_SndFile))
			.WorkingDirectory(TrackerDirectories::Instance().GetWorkingDirectory(DIR_MODS));
		if(!dlg.Show()) return FALSE;

		TrackerDirectories::Instance().SetWorkingDirectory(dlg.GetWorkingDirectory(), DIR_MODS);

		saveFileName = dlg.GetFirstFile();
	} else
	{
		saveFileName = filename.ReplaceExt(ext);
	}

	// Do we need to create a backup file ?
	if((TrackerSettings::Instance().m_dwPatternSetup & PATTERN_CREATEBACKUP)
		&& (IsModified()) && (!mpt::PathString::CompareNoCase(saveFileName, docFileName)))
	{
		if(PathFileExistsW(saveFileName.AsNative().c_str()))
		{
			mpt::PathString backupFileName = saveFileName.ReplaceExt(MPT_PATHSTRING(".bak"));
			if(PathFileExistsW(backupFileName.AsNative().c_str()))
			{
				DeleteFileW(backupFileName.AsNative().c_str());
			}
			MoveFileW(saveFileName.AsNative().c_str(), backupFileName.AsNative().c_str());
		}
	}
	if(OnSaveDocument(saveFileName))
	{
		SetModified(FALSE);
		m_bHasValidPath=true;
		m_ShowSavedialog = false;
		CMainFrame::GetMainFrame()->UpdateTree(this, HINT_MODGENERAL); // Update treeview (e.g. filename might have changed).
		return TRUE;
	} else
	{
		return FALSE;
	}
}


void CModDoc::OnAppendModule()
//----------------------------
{
	FileDialog::PathList files;
	CTrackApp::OpenModulesDialog(files);

	CSoundFile source;
	ScopedLogCapturer logcapture(*this, "Append Failures");

	for(size_t counter = 0; counter < files.size(); counter++)
	{
		InputFile f;
		if(f.Open(files[counter]) && source.Create(GetFileReader(f), CSoundFile::loadCompleteModule))
		{
			AppendModule(source);
			source.Destroy();
			SetModified();
		} else
		{
			AddToLog("Unable to open source file!");
		}
	}
	UpdateAllViews(nullptr, HINT_MODTYPE | HINT_MODSEQUENCE);
}


BOOL CModDoc::InitializeMod()
//---------------------------
{
	// New module ?
	if (!m_SndFile.m_nChannels)
	{
		switch(GetModType())
		{
		case MOD_TYPE_MOD:
			m_SndFile.m_nChannels = 4;
			break;
		case MOD_TYPE_S3M:
			m_SndFile.m_nChannels = 16;
			break;
		default:
			m_SndFile.m_nChannels = 32;
			break;
		}

		// Refresh mix levels now that the correct mod type has been set
		m_SndFile.SetMixLevels(m_SndFile.GetModSpecifications().defaultMixLevels);
		// ...and the order length
		m_SndFile.Order.resize(MIN(ModSequenceSet::s_nCacheSize, m_SndFile.GetModSpecifications().ordersMax));

		if (m_SndFile.Order[0] >= m_SndFile.Patterns.Size())
			m_SndFile.Order[0] = 0;

		if (!m_SndFile.Patterns[0])
		{
			m_SndFile.Patterns.Insert(0, 64);
		}

		MemsetZero(m_SndFile.m_szNames);
		//m_SndFile.SetTitle("untitled");

		m_SndFile.m_PlayState.m_nMusicTempo = m_SndFile.m_nDefaultTempo = 125;
		m_SndFile.m_PlayState.m_nMusicSpeed = m_SndFile.m_nDefaultSpeed = 6;

		// Set up levels
		//if(m_SndFile.m_nMixLevels == mixLevels_original || m_SndFile.m_nMixLevels == mixLevels_compatible)
		{
			m_SndFile.m_PlayState.m_nGlobalVolume = m_SndFile.m_nDefaultGlobalVolume = MAX_GLOBAL_VOLUME;
			m_SndFile.m_nSamplePreAmp = m_SndFile.m_nVSTiVolume = 48;
		}
		/*else
		{
			m_SndFile.m_nGlobalVolume = m_SndFile.m_nDefaultGlobalVolume = MAX_GLOBAL_VOLUME / 2;
			m_SndFile.m_nSamplePreAmp = m_SndFile.m_nVSTiVolume = 128;
		}*/

		if(m_SndFile.GetType() & (MOD_TYPE_IT | MOD_TYPE_XM))
		{
			m_SndFile.SetModFlag(MSF_COMPATIBLE_PLAY, true);
		}

		for (CHANNELINDEX nChn = 0; nChn < MAX_BASECHANNELS; nChn++)
		{
			m_SndFile.ChnSettings[nChn].dwFlags.reset();
			m_SndFile.ChnSettings[nChn].nVolume = 64;
			m_SndFile.ChnSettings[nChn].nPan = 128;
			m_SndFile.m_PlayState.Chn[nChn].nGlobalVol = 64;
		}
		// Setup LRRL panning scheme for MODs
		m_SndFile.SetupMODPanning();
	}
	if (!m_SndFile.m_nSamples)
	{
		strcpy(m_SndFile.m_szNames[1], "untitled");
		m_SndFile.m_nSamples = (GetModType() == MOD_TYPE_MOD) ? 31 : 1;

		ctrlSmp::ResetSamples(m_SndFile, ctrlSmp::SmpResetInit);

		m_SndFile.GetSample(1).Initialize(m_SndFile.GetType());

		if ((!m_SndFile.m_nInstruments) && (m_SndFile.GetType() & MOD_TYPE_XM))
		{
			if(m_SndFile.AllocateInstrument(1, 1))
			{
				m_SndFile.m_nInstruments = 1;
				InitializeInstrument(m_SndFile.Instruments[1]);
			}
		}
		if (m_SndFile.GetType() & (MOD_TYPE_IT | MOD_TYPE_MPT | MOD_TYPE_XM))
		{
			m_SndFile.m_SongFlags.set(SONG_LINEARSLIDES);
		}
	}
	m_SndFile.SetCurrentPos(0);

	return TRUE;
}


void CModDoc::PostMessageToAllViews(UINT uMsg, WPARAM wParam, LPARAM lParam)
//--------------------------------------------------------------------------
{
	POSITION pos = GetFirstViewPosition();
	while (pos != NULL)
	{
		CView* pView = GetNextView(pos);
		if (pView) pView->PostMessage(uMsg, wParam, lParam);
	} 
}


void CModDoc::SendMessageToActiveViews(UINT uMsg, WPARAM wParam, LPARAM lParam)
//-----------------------------------------------------------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	if (pMainFrm)
	{
		CMDIChildWnd *pMDI = pMainFrm->MDIGetActive();
		if (pMDI) pMDI->SendMessageToDescendants(uMsg, wParam, lParam);
	}
}


void CModDoc::ViewPattern(UINT nPat, UINT nOrd)
//---------------------------------------------
{
	SendMessageToActiveViews(WM_MOD_ACTIVATEVIEW, IDD_CONTROL_PATTERNS, ((nPat+1) << 16) | nOrd);
}


void CModDoc::ViewSample(UINT nSmp)
//---------------------------------
{
	SendMessageToActiveViews(WM_MOD_ACTIVATEVIEW, IDD_CONTROL_SAMPLES, nSmp);
}


void CModDoc::ViewInstrument(UINT nIns)
//-------------------------------------
{
	SendMessageToActiveViews(WM_MOD_ACTIVATEVIEW, IDD_CONTROL_INSTRUMENTS, nIns);
}


ScopedLogCapturer::ScopedLogCapturer(CModDoc &modDoc, const std::string &title, CWnd *parent, bool showLog) :
m_modDoc(modDoc), m_oldLogMode(m_modDoc.GetLogMode()), m_title(title), m_pParent(parent), m_showLog(showLog)
//-----------------------------------------------------------------------------------------------------------
{
	m_modDoc.SetLogMode(LogModeGather);
}


void ScopedLogCapturer::ShowLog(bool force)
//----------------------------------------
{
	if(force || m_oldLogMode == LogModeInstantReporting)
	{
		m_modDoc.ShowLog(m_title, m_pParent);
		m_modDoc.ClearLog();
	}
}


void ScopedLogCapturer::ShowLog(const std::string &preamble, bool force)
//----------------------------------------------------------------------
{
	if(force || m_oldLogMode == LogModeInstantReporting)
	{
		m_modDoc.ShowLog(preamble, m_title, m_pParent);
		m_modDoc.ClearLog();
	}
}


void ScopedLogCapturer::ShowLog(const std::wstring &preamble, bool force)
//-----------------------------------------------------------------------
{
	if(force || m_oldLogMode == LogModeInstantReporting)
	{
		m_modDoc.ShowLog(preamble, mpt::ToWide(mpt::CharsetLocale, m_title), m_pParent);
		m_modDoc.ClearLog();
	}
}


ScopedLogCapturer::~ScopedLogCapturer()
//-------------------------------------
{
	if(m_showLog)
		ShowLog();
	else
		m_modDoc.ClearLog();
	m_modDoc.SetLogMode(m_oldLogMode);
}


void CModDoc::AddToLog(LogLevel level, const std::string &text) const
//-------------------------------------------------------------------
{
	if(m_LogMode == LogModeGather)
	{
		m_Log.push_back(LogEntry(level, text));
	} else
	{
		switch(level)
		{
		case LogError:        Reporting::Error(text.c_str());        break;
		case LogWarning:      Reporting::Warning(text.c_str());      break;
		case LogInformation:  Reporting::Information(text.c_str());  break;
		case LogNotification: Reporting::Notification(text.c_str()); break;
		default:              Reporting::Information(text.c_str());  break;
		}
	}
}


std::string CModDoc::GetLogString() const
//---------------------------------------
{
	std::string ret;
	for(std::vector<LogEntry>::const_iterator i=m_Log.begin(); i!=m_Log.end(); ++i)
	{
		ret += (*i).message;
		ret += "\r\n";
	}
	return ret;
}


LogLevel CModDoc::GetMaxLogLevel() const
//--------------------------------------
{
	LogLevel retval = LogNotification;
	for(std::vector<LogEntry>::const_iterator i=m_Log.begin(); i!=m_Log.end(); ++i)
	{
		retval = std::max(retval, i->level);
	}
	return retval;
}


void CModDoc::ClearLog()
//----------------------
{
	m_Log.clear();
}


UINT CModDoc::ShowLog(const std::string &preamble, const std::string &title, CWnd *parent)
//----------------------------------------------------------------------------------------
{
	return ShowLog(mpt::ToWide(mpt::CharsetLocale, preamble), mpt::ToWide(mpt::CharsetLocale, title), parent);
}


UINT CModDoc::ShowLog(const std::wstring &preamble, const std::wstring &title, CWnd *parent)
//------------------------------------------------------------------------------------------
{
	if(!parent) parent = CMainFrame::GetMainFrame();
	if(GetLog().size() > 0)
	{
		std::wstring text = preamble + mpt::ToWide(mpt::CharsetLocale, GetLogString());
		std::wstring actualTitle = (title.length() == 0) ? MAINFRAME_TITLEW : title;
		switch(GetMaxLogLevel())
		{
		case LogError:        Reporting::Error(text.c_str(), actualTitle.c_str(), parent); break;
		case LogWarning:      Reporting::Warning(text.c_str(), actualTitle.c_str(), parent); break;
		case LogInformation:  Reporting::Information(text.c_str(), actualTitle.c_str(), parent); break;
		case LogNotification: Reporting::Notification(text.c_str(), actualTitle.c_str(), parent); break;
		default:              Reporting::Information(text.c_str(), actualTitle.c_str(), parent); break;
		}
		return IDOK;
	}
	return IDCANCEL;
}


uint8 CModDoc::GetPlaybackMidiChannel(const ModInstrument *pIns, CHANNELINDEX nChn) const
//---------------------------------------------------------------------------------------
{
	if(pIns->nMidiChannel == MidiMappedChannel)
	{
		if(nChn != CHANNELINDEX_INVALID)
		{
			return nChn % 16;
		}
	} else if(pIns->HasValidMIDIChannel())
	{
		return pIns->nMidiChannel - 1;
	}
	return 0;
}


void CModDoc::ProcessMIDI(uint32 midiData, INSTRUMENTINDEX ins, IMixPlugin *plugin, InputTargetContext ctx)
//---------------------------------------------------------------------------------------------------------
{
	static uint8 midiVolume = 127;

	MIDIEvents::EventType event  = MIDIEvents::GetTypeFromEvent(midiData);
	uint8 midiByte1 = MIDIEvents::GetDataByte1FromEvent(midiData);
	uint8 midiByte2 = MIDIEvents::GetDataByte2FromEvent(midiData);
	uint8 note  = midiByte1 + NOTE_MIN;
	int vol = midiByte2;

	if((event == MIDIEvents::evNoteOn) && !vol) event = MIDIEvents::evNoteOff;	//Convert event to note-off if req'd

	PLUGINDEX mappedIndex = 0;
	PlugParamIndex paramIndex = 0;
	uint8 paramValue = 0;
	bool captured = m_SndFile.GetMIDIMapper().OnMIDImsg(midiData, mappedIndex, paramIndex, paramValue);

	// Handle MIDI messages assigned to shortcuts
	CInputHandler *ih = CMainFrame::GetMainFrame()->GetInputHandler();
	if(ih->HandleMIDIMessage(ctx, midiData) != kcNull
		|| ih->HandleMIDIMessage(kCtxAllContexts, midiData) != kcNull)
	{
		// Mapped to a command, no need to pass message on.
		captured = true;
	}

	if(captured)
	{
		// Event captured by MIDI mapping or shortcut, no need to pass message on.
		return;
	}

	switch(event)
	{
	case MIDIEvents::evNoteOff:
		midiByte2 = 0;

	case MIDIEvents::evNoteOn:
		if(ins > 0 && ins <= GetNumInstruments())
		{
			LimitMax(note, NOTE_MAX);
			NoteOff(note, false, ins);
			if(midiByte2 & 0x7F)
			{
				vol = CMainFrame::ApplyVolumeRelatedSettings(midiData, midiVolume);
				PlayNote(note, ins, 0, false, vol);
			}
			return;
		}
		break;

	case MIDIEvents::evControllerChange:
		if(midiByte1 == MIDIEvents::MIDICC_Volume_Coarse)
		{
			midiVolume = midiByte2;
			break;
		}
	}

	if((TrackerSettings::Instance().m_dwMidiSetup & MIDISETUP_MIDITOPLUG) && CMainFrame::GetMainFrame()->GetModPlaying() == this && plugin != nullptr)
	{
		plugin->MidiSend(midiData);
		// Sending midi may modify the plug. For now, if MIDI data is not active sensing or aftertouch messages, set modified.
		if(midiData != MIDIEvents::System(MIDIEvents::sysActiveSense)
			&& event != MIDIEvents::evPolyAftertouch && event != MIDIEvents::evChannelAftertouch
			&& event != MIDIEvents::evPitchBend
			&& m_SndFile.GetModSpecifications().supportsPlugins)
		{
			CMainFrame::GetMainFrame()->ThreadSafeSetModified(this);
		}
	}
}


CHANNELINDEX CModDoc::PlayNote(UINT note, INSTRUMENTINDEX nins, SAMPLEINDEX nsmp, bool pause, LONG nVol, SmpLength loopStart, SmpLength loopEnd, CHANNELINDEX nCurrentChn, const SmpLength sampleOffset)
//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	CHANNELINDEX nChn = GetNumChannels();
	
	if (pMainFrm == nullptr || note == NOTE_NONE) return FALSE;
	if (nVol > 256) nVol = 256;
	if (ModCommand::IsNote(ModCommand::NOTE(note)))
	{

		//kill notes if required.
		if ( (pause) || (m_SndFile.IsPaused()) || pMainFrm->GetModPlaying() != this)
		{ 
			CriticalSection cs;

			//OnPlayerPause();					// pause song - pausing VSTis is too slow

			// All notes off
			for(CHANNELINDEX i = 0; i < MAX_CHANNELS; i++)
			{
				if ((i < GetNumChannels()) || (m_SndFile.m_PlayState.Chn[i].nMasterChn))
				{
					m_SndFile.m_PlayState.Chn[i].dwFlags.set(CHN_KEYOFF | CHN_NOTEFADE);
					m_SndFile.m_PlayState.Chn[i].nFadeOutVol = 0;
				}
			}
		}

		if (pMainFrm->GetModPlaying() != this)
		{
			m_SndFile.m_SongFlags.set(SONG_PAUSED);
			pMainFrm->PlayMod(this);
		}

		CriticalSection cs;

		// Find a channel to play on
		nChn = FindAvailableChannel();
		ModChannel &chn = m_SndFile.m_PlayState.Chn[nChn];
		
		// reset channel properties; in theory the chan is completely unused anyway.
		chn.Reset(ModChannel::resetTotal, m_SndFile, CHANNELINDEX_INVALID);
		chn.nMasterChn = 0;	// remove NNA association
		chn.nNewNote = chn.nLastNote = static_cast<uint8>(note);

		if (nins)
		{
			// Set instrument
			chn.ResetEnvelopes();
			m_SndFile.InstrumentChange(&chn, nins);
		} else if ((nsmp) && (nsmp < MAX_SAMPLES))	// Or set sample
		{
			ModSample &sample = m_SndFile.GetSample(nsmp);
			chn.pCurrentSample = sample.pSample;
			chn.pModInstrument = nullptr;
			chn.pModSample = &sample;
			chn.nFineTune = sample.nFineTune;
			chn.nC5Speed = sample.nC5Speed;
			chn.nLoopStart = sample.nLoopStart;
			chn.nLoopEnd = sample.nLoopEnd;
			chn.dwFlags = static_cast<ChannelFlags>(sample.uFlags) & (CHN_SAMPLEFLAGS & ~CHN_MUTE);
			chn.nPan = 128;
			if (sample.uFlags[CHN_PANNING]) chn.nPan = sample.nPan;
			chn.nInsVol = sample.nGlobalVol;
		}
		chn.nFadeOutVol = 0x10000;

		m_SndFile.NoteChange(&chn, note, false, true, true);
		if (nVol >= 0) chn.nVolume = nVol;
		
		// Handle sample looping.
		// Changed line to fix http://forum.openmpt.org/index.php?topic=1700.0
		//if ((loopstart + 16 < loopend) && (loopstart >= 0) && (loopend <= (LONG)pchn.nLength))
		if ((loopStart + 16 < loopEnd) && (loopStart >= 0) && (chn.pModSample != nullptr))
		{
			chn.nPos = loopStart;
			chn.nPosLo = 0;
			chn.nLoopStart = loopStart;
			chn.nLoopEnd = loopEnd;
			chn.nLength = std::min(loopEnd, chn.pModSample->nLength);
		}

		// Handle extra-loud flag
		chn.dwFlags.set(CHN_EXTRALOUD, !(TrackerSettings::Instance().m_dwPatternSetup & PATTERN_NOEXTRALOUD) && nsmp);

		// Handle custom start position
		if(sampleOffset > 0 && chn.pModSample)
		{
			chn.nPos = sampleOffset;
			// If start position is after loop end, set loop end to sample end so that the sample starts 
			// playing.
			if(chn.nLoopEnd < sampleOffset)
				chn.nLength = chn.nLoopEnd = chn.pModSample->nLength;
		}

		//rewbs.vstiLive
		if (nins <= m_SndFile.GetNumInstruments())
		{
			const ModInstrument *pIns = m_SndFile.Instruments[nins];
			if (pIns && pIns->HasValidMIDIChannel()) // instro sends to a midi chan
			{
				// UINT nPlugin = m_SndFile.GetBestPlugin(nChn, PRIORITISE_INSTRUMENT, EVEN_IF_MUTED);
				 
				PLUGINDEX nPlugin = 0;
				if (chn.pModInstrument) 
					nPlugin = chn.pModInstrument->nMixPlug;					// First try instrument plugin
				if ((!nPlugin || nPlugin > MAX_MIXPLUGINS) && nCurrentChn != CHANNELINDEX_INVALID)
					nPlugin = m_SndFile.ChnSettings[nCurrentChn].nMixPlugin;	// Then try channel plugin
				
				if ((nPlugin) && (nPlugin <= MAX_MIXPLUGINS))
				{
					IMixPlugin *pPlugin = m_SndFile.m_MixPlugins[nPlugin - 1].pMixPlugin;

					if(pPlugin != nullptr)
					{
						pPlugin->MidiCommand(GetPlaybackMidiChannel(pIns, nCurrentChn), pIns->nMidiProgram, pIns->wMidiBank, pIns->NoteMap[note - 1], static_cast<uint16>(chn.nVolume), MAX_BASECHANNELS);
					}
				}
			}
		}
		//end rewbs.vstiLive

	} else
	{
		CriticalSection cs;
		m_SndFile.NoteChange(&m_SndFile.m_PlayState.Chn[nChn], note);
		if (pause) m_SndFile.m_SongFlags.set(SONG_PAUSED);
	}
	return nChn;
}


bool CModDoc::NoteOff(UINT note, bool fade, INSTRUMENTINDEX ins, CHANNELINDEX currentChn, CHANNELINDEX stopChn)
//-------------------------------------------------------------------------------------------------------------
{
	CriticalSection cs;

	//rewbs.vstiLive
	if(ins != INSTRUMENTINDEX_INVALID && ins <= m_SndFile.GetNumInstruments() && ModCommand::IsNote(ModCommand::NOTE(note)))
	{

		ModInstrument *pIns = m_SndFile.Instruments[ins];
		if(pIns && pIns->HasValidMIDIChannel())	// instro sends to a midi chan
		{

			PLUGINDEX plug = pIns->nMixPlug;		// First try intrument VST
			if((!plug || plug > MAX_MIXPLUGINS)		// No good plug yet
				&& currentChn < MAX_BASECHANNELS)	// Chan OK
			{
				plug = m_SndFile.ChnSettings[currentChn].nMixPlugin;// Then try Channel VST
			}
			
			if(plug && plug <= MAX_MIXPLUGINS)
			{
				IMixPlugin *pPlugin =  m_SndFile.m_MixPlugins[plug - 1].pMixPlugin;
				if(pPlugin)
				{
					pPlugin->MidiCommand(GetPlaybackMidiChannel(pIns, currentChn), pIns->nMidiProgram, pIns->wMidiBank, pIns->NoteMap[note - 1] + NOTE_KEYOFF, 0, MAX_BASECHANNELS);
				}
			}
		}
	}
	//end rewbs.vstiLive

	const ChannelFlags mask = (fade ? CHN_NOTEFADE : (CHN_NOTEFADE | CHN_KEYOFF));
	ModChannel *pChn = &m_SndFile.m_PlayState.Chn[stopChn != CHANNELINDEX_INVALID ? stopChn : m_SndFile.m_nChannels];
	for(CHANNELINDEX i = m_SndFile.GetNumChannels(); i < MAX_CHANNELS; i++, pChn++)
	{
		// Fade all channels > m_nChannels which are playing this note and aren't NNA channels.
		if(!pChn->nMasterChn && !pChn->dwFlags[mask] && pChn->nLength && (note == pChn->nNewNote || !note))
		{
			m_SndFile.KeyOff(pChn);
			if (!m_SndFile.m_nInstruments) pChn->dwFlags.reset(CHN_LOOP);	// FIXME: If a sample with pingpong loop is playing backwards, stuff before the loop is played again!
			if (fade) pChn->dwFlags.set(CHN_NOTEFADE);
			// Instantly stop samples that would otherwise play forever
			if (pChn->pModInstrument && !pChn->pModInstrument->nFadeOut)
				pChn->nFadeOutVol = 0;
			if (note) break;
		}
	}

	return true;
}


// Check if a given note of an instrument or sample is playing.
// If note == 0, just check if an instrument or sample is playing.
bool CModDoc::IsNotePlaying(UINT note, SAMPLEINDEX nsmp, INSTRUMENTINDEX nins)
//----------------------------------------------------------------------------
{
	ModChannel *pChn = &m_SndFile.m_PlayState.Chn[m_SndFile.GetNumChannels()];
	for (CHANNELINDEX i = m_SndFile.GetNumChannels(); i < MAX_CHANNELS; i++, pChn++) if (!pChn->nMasterChn)
	{
		if(pChn->nLength != 0 && !pChn->dwFlags[CHN_NOTEFADE | CHN_KEYOFF| CHN_MUTE]
			&& (note == pChn->nNewNote || !note)
			&& (pChn->pModSample == &m_SndFile.GetSample(nsmp) || !nsmp)
			&& (pChn->pModInstrument == m_SndFile.Instruments[nins] || !nins)) return true;
	}
	return false;
}


bool CModDoc::MuteChannel(CHANNELINDEX nChn, bool doMute)
//-------------------------------------------------------
{
	if (nChn >= m_SndFile.GetNumChannels())
	{
		return false;
	}

	// Mark channel as muted in channel settings
	m_SndFile.ChnSettings[nChn].dwFlags.set(CHN_MUTE, doMute);

	const bool success = UpdateChannelMuteStatus(nChn);
	if(success)
	{
		//Mark IT/MPTM/S3M as modified
		if(m_SndFile.GetType() & (MOD_TYPE_IT | MOD_TYPE_MPT | MOD_TYPE_S3M))
		{
			CMainFrame::GetMainFrame()->ThreadSafeSetModified(this);
		}
	}

	return success;
}


bool CModDoc::UpdateChannelMuteStatus(CHANNELINDEX nChn)
//------------------------------------------------------
{
	const ChannelFlags muteType = (TrackerSettings::Instance().m_dwPatternSetup & PATTERN_SYNCMUTE) ? CHN_SYNCMUTE : CHN_MUTE;

	if (nChn >= m_SndFile.GetNumChannels())
	{
		return false;
	}

	const bool doMute = m_SndFile.ChnSettings[nChn].dwFlags[CHN_MUTE];

	// Mute pattern channel
	if (doMute)
	{
		m_SndFile.m_PlayState.Chn[nChn].dwFlags.set(muteType);
		// Kill VSTi notes on muted channel.
		PLUGINDEX nPlug = m_SndFile.GetBestPlugin(nChn, PrioritiseInstrument, EvenIfMuted);
		if ((nPlug) && (nPlug<=MAX_MIXPLUGINS))
		{
			CVstPlugin *pPlug = (CVstPlugin*)m_SndFile.m_MixPlugins[nPlug - 1].pMixPlugin;
			const ModInstrument* pIns = m_SndFile.m_PlayState.Chn[nChn].pModInstrument;
			if (pPlug && pIns)
			{
				pPlug->MidiCommand(m_SndFile.GetBestMidiChannel(nChn), pIns->nMidiProgram, pIns->wMidiBank, NOTE_KEYOFF, 0, nChn);
			}
		}
	} else
	{
		// On unmute alway cater for both mute types - this way there's no probs if user changes mute mode.
		m_SndFile.m_PlayState.Chn[nChn].dwFlags.reset(CHN_SYNCMUTE | CHN_MUTE);
	}

	// Mute any NNA'd channels
	for (CHANNELINDEX i = m_SndFile.GetNumChannels(); i < MAX_CHANNELS; i++)
	{
		if (m_SndFile.m_PlayState.Chn[i].nMasterChn == nChn + 1u)
		{
			if (doMute)
			{ 
				m_SndFile.m_PlayState.Chn[i].dwFlags.set(muteType);
			} else
			{
				// On unmute alway cater for both mute types - this way there's no probs if user changes mute mode.
				m_SndFile.m_PlayState.Chn[i].dwFlags.reset(CHN_SYNCMUTE | CHN_MUTE);
			}
		}
	}

	return true;
}

// -> CODE#0012
// -> DESC="midi keyboard split"
bool CModDoc::IsChannelSolo(CHANNELINDEX nChn) const
//--------------------------------------------------
{
	if (nChn >= m_SndFile.m_nChannels) return true;
	return m_SndFile.ChnSettings[nChn].dwFlags[CHN_SOLO];
}

bool CModDoc::SoloChannel(CHANNELINDEX nChn, bool bSolo)
//------------------------------------------------------
{
	if (nChn >= m_SndFile.m_nChannels) return false;
	if (m_SndFile.GetType() & (MOD_TYPE_IT | MOD_TYPE_MPT | MOD_TYPE_S3M)) SetModified();
	m_SndFile.ChnSettings[nChn].dwFlags.set(CHN_SOLO, bSolo);
	return true;
}
// -! NEW_FEATURE#0012


// -> CODE#0015
// -> DESC="channels management dlg"
bool CModDoc::IsChannelNoFx(CHANNELINDEX nChn) const
//--------------------------------------------------
{
	if (nChn >= m_SndFile.m_nChannels) return true;
	return m_SndFile.ChnSettings[nChn].dwFlags[CHN_NOFX];
}


bool CModDoc::NoFxChannel(CHANNELINDEX nChn, bool bNoFx, bool updateMix)
//----------------------------------------------------------------------
{
	if (nChn >= m_SndFile.m_nChannels) return false;
	m_SndFile.ChnSettings[nChn].dwFlags.set(CHN_NOFX, bNoFx);
	if(updateMix) m_SndFile.m_PlayState.Chn[nChn].dwFlags.set(CHN_NOFX, bNoFx);
	return true;
}


bool CModDoc::IsChannelRecord1(CHANNELINDEX channel) const
//--------------------------------------------------------
{
	return m_bsMultiRecordMask[channel];
}


bool CModDoc::IsChannelRecord2(CHANNELINDEX channel) const
//--------------------------------------------------------
{
	return m_bsMultiSplitRecordMask[channel];
}

BYTE CModDoc::IsChannelRecord(CHANNELINDEX channel) const
//-------------------------------------------------------
{
	if(IsChannelRecord1(channel)) return 1;
	if(IsChannelRecord2(channel)) return 2;
	return 0;
}

void CModDoc::Record1Channel(CHANNELINDEX channel, bool select)
//-------------------------------------------------------------
{
	if (!select)
	{
		m_bsMultiRecordMask.reset(channel);
		m_bsMultiSplitRecordMask.reset(channel);
	} else
	{
		m_bsMultiRecordMask.flip(channel);
		m_bsMultiSplitRecordMask.reset(channel);
	}
}

void CModDoc::Record2Channel(CHANNELINDEX channel, bool select)
//-------------------------------------------------------------
{
	if (!select)
	{
		m_bsMultiRecordMask.reset(channel);
		m_bsMultiSplitRecordMask.reset(channel);
	} else
	{
		m_bsMultiSplitRecordMask.flip(channel);
		m_bsMultiRecordMask.reset(channel);
	}
}

void CModDoc::ReinitRecordState(bool unselect)
//--------------------------------------------
{
	if (unselect)
	{
		m_bsMultiRecordMask.reset();
		m_bsMultiSplitRecordMask.reset();
	} else
	{
		m_bsMultiRecordMask.set();
		m_bsMultiSplitRecordMask.set();
	}
}
// -! NEW_FEATURE#0015


bool CModDoc::MuteSample(SAMPLEINDEX nSample, bool bMute)
//-------------------------------------------------------
{
	if ((nSample < 1) || (nSample > m_SndFile.GetNumSamples())) return false;
	m_SndFile.GetSample(nSample).uFlags.set(CHN_MUTE, bMute);
	return true;
}


bool CModDoc::MuteInstrument(INSTRUMENTINDEX nInstr, bool bMute)
//--------------------------------------------------------------
{
	if ((nInstr < 1) || (nInstr > m_SndFile.GetNumInstruments()) || (!m_SndFile.Instruments[nInstr])) return false;
	m_SndFile.Instruments[nInstr]->dwFlags.set(INS_MUTE, bMute);
	return true;
}


bool CModDoc::SurroundChannel(CHANNELINDEX nChn, bool surround)
//--------------------------------------------------------------
{
	if(nChn >= m_SndFile.GetNumChannels()) return false;

	if(!(m_SndFile.GetType() & (MOD_TYPE_IT | MOD_TYPE_MPT))) surround = false;

	if(surround != m_SndFile.ChnSettings[nChn].dwFlags[CHN_SURROUND])
	{
		// Update channel configuration
		if(m_SndFile.GetType() & (MOD_TYPE_IT | MOD_TYPE_MPT)) SetModified();

		m_SndFile.ChnSettings[nChn].dwFlags.set(CHN_SURROUND, surround);
		if(surround)
		{
			m_SndFile.ChnSettings[nChn].nPan = 128;
		}
	}

	// Update playing channel
	m_SndFile.m_PlayState.Chn[nChn].dwFlags.set(CHN_SURROUND, surround);
	if(surround)
	{
		m_SndFile.m_PlayState.Chn[nChn].nPan = 128;
	}
	return true;
}


bool CModDoc::SetChannelGlobalVolume(CHANNELINDEX nChn, uint16 nVolume)
//---------------------------------------------------------------------
{
	bool ok = false;
	if(nChn >= m_SndFile.GetNumChannels() || nVolume > 64) return false;
	if(m_SndFile.ChnSettings[nChn].nVolume != nVolume)
	{
		m_SndFile.ChnSettings[nChn].nVolume = nVolume;
		if(m_SndFile.GetType() & (MOD_TYPE_IT | MOD_TYPE_MPT)) SetModified();
		ok = true;
	}
	m_SndFile.m_PlayState.Chn[nChn].nGlobalVol = nVolume;
	return ok;
}


bool CModDoc::SetChannelDefaultPan(CHANNELINDEX nChn, uint16 nPan)
//----------------------------------------------------------------
{
	bool ok = false;
	if(nChn >= m_SndFile.GetNumChannels() || nPan > 256) return false;
	if(m_SndFile.ChnSettings[nChn].nPan != nPan || m_SndFile.ChnSettings[nChn].dwFlags[CHN_SURROUND])
	{
		m_SndFile.ChnSettings[nChn].nPan = nPan;
		m_SndFile.ChnSettings[nChn].dwFlags.reset(CHN_SURROUND);
		if(m_SndFile.GetType() & (MOD_TYPE_S3M | MOD_TYPE_IT | MOD_TYPE_MPT)) SetModified();
		ok = true;
	}
	m_SndFile.m_PlayState.Chn[nChn].nPan = nPan;
	m_SndFile.m_PlayState.Chn[nChn].dwFlags.reset(CHN_SURROUND);
	return ok;
}


bool CModDoc::IsChannelMuted(CHANNELINDEX nChn) const
//---------------------------------------------------
{
	if(nChn >= m_SndFile.GetNumChannels()) return true;
	return m_SndFile.ChnSettings[nChn].dwFlags[CHN_MUTE];
}


bool CModDoc::IsSampleMuted(SAMPLEINDEX nSample) const
//----------------------------------------------------
{
	if(!nSample || nSample > m_SndFile.GetNumSamples()) return false;
	return m_SndFile.GetSample(nSample).uFlags[CHN_MUTE];
}


bool CModDoc::IsInstrumentMuted(INSTRUMENTINDEX nInstr) const
//-----------------------------------------------------------
{
	if(!nInstr || nInstr > m_SndFile.GetNumInstruments() || !m_SndFile.Instruments[nInstr]) return false;
	return m_SndFile.Instruments[nInstr]->dwFlags[INS_MUTE];
}


UINT CModDoc::GetPatternSize(PATTERNINDEX nPat) const
//---------------------------------------------------
{
	if(nPat < m_SndFile.Patterns.Size() && m_SndFile.Patterns[nPat]) return m_SndFile.Patterns[nPat].GetNumRows();
	return 0;
}


void CModDoc::SetFollowWnd(HWND hwnd)
//-----------------------------------
{
	m_hWndFollow = hwnd;
}


bool CModDoc::IsChildSample(INSTRUMENTINDEX nIns, SAMPLEINDEX nSmp) const
//-----------------------------------------------------------------------
{
	return m_SndFile.IsSampleReferencedByInstrument(nSmp, nIns);
}


// Find an instrument that references the given sample.
// If no such instrument is found, INSTRUMENTINDEX_INVALID is returned.
INSTRUMENTINDEX CModDoc::FindSampleParent(SAMPLEINDEX sample) const
//-----------------------------------------------------------------
{
	if(sample == 0)
	{
		return INSTRUMENTINDEX_INVALID;
	}
	for(INSTRUMENTINDEX i = 1; i <= m_SndFile.GetNumInstruments(); i++)
	{
		const ModInstrument *pIns = m_SndFile.Instruments[i];
		if(pIns != nullptr)
		{
			for(size_t j = 0; j < NOTE_MAX; j++)
			{
				if(pIns->Keyboard[j] == sample)
				{
					return i;
				}
			}
		}
	}
	return INSTRUMENTINDEX_INVALID;
}


SAMPLEINDEX CModDoc::FindInstrumentChild(INSTRUMENTINDEX nIns) const
//------------------------------------------------------------------
{
	if ((!nIns) || (nIns > m_SndFile.GetNumInstruments())) return 0;
	const ModInstrument *pIns = m_SndFile.Instruments[nIns];
	if (pIns)
	{
		for (size_t i = 0; i < CountOf(pIns->Keyboard); i++)
		{
			SAMPLEINDEX n = pIns->Keyboard[i];
			if ((n) && (n <= m_SndFile.GetNumSamples())) return n;
		}
	}
	return 0;
}


LRESULT CModDoc::ActivateView(UINT nIdView, DWORD dwParam)
//--------------------------------------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	if (!pMainFrm) return 0;
	CMDIChildWnd *pMDIActive = pMainFrm->MDIGetActive();
	if (pMDIActive)
	{
		CView *pView = pMDIActive->GetActiveView();
		if ((pView) && (pView->GetDocument() == this))
		{
			return ((CChildFrame *)pMDIActive)->ActivateView(nIdView, dwParam);
		}
	}
	POSITION pos = GetFirstViewPosition();
	while (pos != NULL)
	{
		CView *pView = GetNextView(pos);
		if ((pView) && (pView->GetDocument() == this))
		{
			CChildFrame *pChildFrm = (CChildFrame *)pView->GetParentFrame();
			pChildFrm->MDIActivate();
			return pChildFrm->ActivateView(nIdView, dwParam);
		}
	}
	return 0;
}


// Activate document's window.
void CModDoc::ActivateWindow()
//----------------------------
{
	
	CChildFrame *pChildFrm = (CChildFrame *)GetChildFrame();
	if(pChildFrm) pChildFrm->MDIActivate();
}


void CModDoc::UpdateAllViews(CView *pSender, LPARAM lHint, CObject *pHint)
//------------------------------------------------------------------------
{
	CDocument::UpdateAllViews(pSender, lHint, pHint);
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	if (pMainFrm) pMainFrm->UpdateTree(this, lHint, pHint);
}


/////////////////////////////////////////////////////////////////////////////
// CModDoc commands

void CModDoc::OnFileWaveConvert()
//-------------------------------
{
	OnFileWaveConvert(ORDERINDEX_INVALID, ORDERINDEX_INVALID);
}

void CModDoc::OnFileWaveConvert(ORDERINDEX nMinOrder, ORDERINDEX nMaxOrder)
//-------------------------------------------------------------------------
{
	WAVEncoder wavencoder;
	FLACEncoder flacencoder;
	std::vector<EncoderFactoryBase*> encFactories;
	encFactories.push_back(&wavencoder);
	encFactories.push_back(&flacencoder);
	OnFileWaveConvert(nMinOrder, nMaxOrder, encFactories);
}

void CModDoc::OnFileWaveConvert(ORDERINDEX nMinOrder, ORDERINDEX nMaxOrder, const std::vector<EncoderFactoryBase*> &encFactories)
//-------------------------------------------------------------------------------------------------------------------------------
{
	ASSERT(!encFactories.empty());

	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();

	if ((!pMainFrm) || (!m_SndFile.GetType()) || encFactories.empty()) return;

	CWaveConvert wsdlg(pMainFrm, nMinOrder, nMaxOrder, m_SndFile.Order.GetLengthTailTrimmed() - 1, m_SndFile, encFactories);
	if (wsdlg.DoModal() != IDOK) return;

	EncoderFactoryBase *encFactory = wsdlg.m_Settings.GetEncoderFactory();

	const std::string extension = encFactory->GetTraits().fileExtension;

	FileDialog dlg = SaveFileDialog()
		.DefaultExtension(extension)
		.DefaultFilename(GetPathNameMpt().GetFileName() + MPT_PATHSTRING(".") + mpt::PathString::FromUTF8(extension))
		.ExtensionFilter(encFactory->GetTraits().fileDescription + " (*." + extension + ")|*." + extension + "||")
		.WorkingDirectory(TrackerDirectories::Instance().GetWorkingDirectory(DIR_EXPORT));
	if(!dlg.Show()) return;

	// will set default dir here because there's no setup option for export dir yet (feel free to add one...)
	TrackerDirectories::Instance().SetDefaultDirectory(dlg.GetWorkingDirectory(), DIR_EXPORT, true);

	mpt::PathString drive, dir, name, ext;
	dlg.GetFirstFile().SplitPath(&drive, &dir, &name, &ext);
	const mpt::PathString fileName = drive + dir + name;
	const mpt::PathString fileExt = ext;

	// Saving as wave file

	int nRenderPasses = 1;
	// Channel mode
	std::vector<bool> usedChannels;
	std::vector<ChannelFlags> channelFlags;
	// Instrument mode
	std::vector<bool> instrMuteState;

	// Channel mode: save song in multiple wav files (one for each enabled channels)
	if(wsdlg.m_bChannelMode)
	{
		// Don't save empty channels
		CheckUsedChannels(usedChannels);

		nRenderPasses = m_SndFile.GetNumChannels();
		channelFlags.resize(nRenderPasses, ChannelFlags(0));
		for(CHANNELINDEX i = 0; i < m_SndFile.GetNumChannels(); i++)
		{
			// Save channels' flags
			channelFlags[i] = m_SndFile.ChnSettings[i].dwFlags;
			// Ignore muted channels
			if(channelFlags[i] & CHN_MUTE) usedChannels[i] = false;
			// Mute each channel
			m_SndFile.ChnSettings[i].dwFlags.set(CHN_MUTE);
		}
	}
	// Instrument mode: Same as channel mode, but renders per instrument (or sample)
	if(wsdlg.m_bInstrumentMode)
	{
		if(m_SndFile.GetNumInstruments() == 0)
		{
			nRenderPasses = m_SndFile.GetNumSamples();
			instrMuteState.resize(nRenderPasses, false);
			for(SAMPLEINDEX i = 0; i < m_SndFile.GetNumSamples(); i++)
			{
				instrMuteState[i] = IsSampleMuted(i + 1);
				MuteSample(i + 1, true);
			}
		} else
		{
			nRenderPasses = m_SndFile.GetNumInstruments();
			instrMuteState.resize(nRenderPasses, false);
			for(INSTRUMENTINDEX i = 0; i < m_SndFile.GetNumInstruments(); i++)
			{
				instrMuteState[i] = IsInstrumentMuted(i + 1);
				MuteInstrument(i + 1, true);
			}
		}
	}

	UINT pos = m_SndFile.GetCurrentPos();
	pMainFrm->PauseMod();
	int oldRepeat = m_SndFile.GetRepeatCount();

	for(int i = 0 ; i < nRenderPasses ; i++)
	{
		mpt::PathString thisName = fileName;
		char fileNameAdd[_MAX_FNAME] = "";

		// Channel mode
		if(wsdlg.m_bChannelMode)
		{
			// Re-mute previously processed channel
			if(i > 0) m_SndFile.ChnSettings[i - 1].dwFlags.set(CHN_MUTE);

			// Was this channel actually muted? Don't process it then.
			if(usedChannels[i] == false)
				continue;
			// Add channel number & name (if available) to path string
			if(strcmp(m_SndFile.ChnSettings[i].szName, ""))
				sprintf(fileNameAdd, "-%03d_%s", i + 1, m_SndFile.ChnSettings[i].szName);
			else
				sprintf(fileNameAdd, "-%03d", i + 1);
			// Unmute channel to process
			m_SndFile.ChnSettings[i].dwFlags.reset(CHN_MUTE);
		}
		// Instrument mode
		if(wsdlg.m_bInstrumentMode)
		{
			if(m_SndFile.GetNumInstruments() == 0)
			{
				// Re-mute previously processed sample
				if(i > 0) MuteSample((SAMPLEINDEX)i, true);

				if(m_SndFile.GetSample((SAMPLEINDEX)(i + 1)).pSample == nullptr || !m_SndFile.IsSampleUsed((SAMPLEINDEX)(i + 1)))
					continue;
				// Add sample number & name (if available) to path string
				if(strcmp(m_SndFile.m_szNames[i + 1], ""))
					sprintf(fileNameAdd, "-%03d_%s", i + 1, m_SndFile.m_szNames[i + 1]);
				else
					sprintf(fileNameAdd, "-%03d", i + 1);
				// Unmute sample to process
				MuteSample((SAMPLEINDEX)(i + 1), false);
			} else
			{
				// Re-mute previously processed instrument
				if(i > 0) MuteInstrument((INSTRUMENTINDEX)i, true);

				if(m_SndFile.Instruments[i + 1] == nullptr || !m_SndFile.IsInstrumentUsed((INSTRUMENTINDEX)(i + 1)))
					continue;
				if(strcmp(m_SndFile.Instruments[i + 1]->name, ""))
					sprintf(fileNameAdd, "-%03d_%s", i + 1, m_SndFile.Instruments[i + 1]->name);
				else
					sprintf(fileNameAdd, "-%03d", i + 1);
				// Unmute instrument to process
				MuteInstrument((INSTRUMENTINDEX)(i + 1), false);
			}
		}
		
		if(strcmp(fileNameAdd, ""))
		{
			SanitizeFilename(fileNameAdd);
			thisName += mpt::PathString::FromLocale(fileNameAdd);
		}
		thisName += fileExt;

		// Render song (or current channel, or current sample/instrument)
		CDoWaveConvert dwcdlg(m_SndFile, thisName, wsdlg.m_Settings, pMainFrm);
		dwcdlg.m_dwFileLimit = wsdlg.m_dwFileLimit;
		dwcdlg.m_bGivePlugsIdleTime = wsdlg.m_bGivePlugsIdleTime;
		dwcdlg.m_dwSongLimit = wsdlg.m_dwSongLimit;

		if(dwcdlg.DoModal() != IDOK) break;
	}

	// Restore channels' flags
	if(wsdlg.m_bChannelMode)
	{
		for(CHANNELINDEX i = 0; i < m_SndFile.GetNumChannels(); i++)
		{
			m_SndFile.ChnSettings[i].dwFlags = channelFlags[i];
		}
	}
	// Restore instruments' / samples' flags
	if(wsdlg.m_bInstrumentMode)
	{
		if(m_SndFile.GetNumInstruments() == 0)
		{
			for(SAMPLEINDEX i = 0; i < m_SndFile.GetNumSamples(); i++)
			{
				MuteSample(i + 1, instrMuteState[i]);
			}
		} else
		{
			for(INSTRUMENTINDEX i = 0; i < m_SndFile.GetNumInstruments(); i++)
			{
				MuteInstrument(i + 1, instrMuteState[i]);
			}
		}
	}

	m_SndFile.SetRepeatCount(oldRepeat);
	m_SndFile.SetCurrentPos(pos);
	CMainFrame::UpdateAudioParameters(m_SndFile, true);
}


void CModDoc::OnFileMP3Convert()
//------------------------------
{
	OnFileMP3Convert(ORDERINDEX_INVALID, ORDERINDEX_INVALID, true);
}

void CModDoc::OnFileMP3Convert(ORDERINDEX nMinOrder, ORDERINDEX nMaxOrder, bool showWarning)
//------------------------------------------------------------------------------------------
{
	WAVEncoder wavencoder;
	FLACEncoder flacencoder;
	OggOpusEncoder opusencoder;
	VorbisEncoder vorbisencoder;
	MP3Encoder mp3lame(MP3EncoderLame);
	MP3Encoder mp3blade(MP3EncoderBlade);
	MP3Encoder mp3acm(MP3EncoderACM);
	std::vector<EncoderFactoryBase*> encoders;
	if(wavencoder.IsAvailable())    encoders.push_back(&wavencoder);
	if(flacencoder.IsAvailable())   encoders.push_back(&flacencoder);
	if(opusencoder.IsAvailable())   encoders.push_back(&opusencoder);
	if(vorbisencoder.IsAvailable()) encoders.push_back(&vorbisencoder);
	if(mp3lame.IsAvailable())       encoders.push_back(&mp3lame);
	if(mp3blade.IsAvailable())      encoders.push_back(&mp3blade);
	if(mp3acm.IsAvailable())        encoders.push_back(&mp3acm);
	if(showWarning && encoders.size() == 2)
	{
		Reporting::Warning(
			"No Opus/Vorbis/MP3 codec found.\n"
			"Please copy\n"
			" - Xipg.Org Opus libraries\n"
			" - Ogg Vorbis libraries\n"
			" - libmp3lame.dll or Lame_Enc.dll\n"
			"into OpenMPT's root directory.\n"
			"Alternatively, you can install an MP3 ACM codec.",
			"OpenMPT - Export");
	}
	OnFileWaveConvert(nMinOrder, nMaxOrder, encoders);
}


void CModDoc::OnFileMidiConvert()
//-------------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();

	if ((!pMainFrm) || (!m_SndFile.GetType())) return;

	mpt::PathString filename = GetPathNameMpt().ReplaceExt(MPT_PATHSTRING(".mid"));

	FileDialog dlg = SaveFileDialog()
		.DefaultExtension("mid")
		.DefaultFilename(filename)
		.ExtensionFilter("Midi Files (*.mid,*.rmi)|*.mid;*.rmi||");
	if(!dlg.Show()) return;

	CModToMidi mididlg(dlg.GetFirstFile(), &m_SndFile, pMainFrm);
	if(mididlg.DoModal() == IDOK)
	{
		BeginWaitCursor();
		mididlg.DoConvert();
		EndWaitCursor();
	}
}

//HACK: This is a quick fix. Needs to be better integrated into player and GUI.
void CModDoc::OnFileCompatibilitySave()
//-------------------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	if (!pMainFrm) return;

	std::string pattern;

	const MODTYPE type = m_SndFile.GetType();

	switch (type)
	{
		case MOD_TYPE_IT:
			pattern = FileFilterIT;
			MsgBoxHidable(CompatExportDefaultWarning);
			break;
		case MOD_TYPE_XM:
			pattern = FileFilterXM;
			MsgBoxHidable(CompatExportDefaultWarning);
			break;
		default:
			// Not available for this format.
			return;
	}

	std::string ext = m_SndFile.GetModSpecifications().fileExtension;

	mpt::PathString filename;

	{
		mpt::PathString drive;
		mpt::PathString dir;
		mpt::PathString fileName;
		GetPathNameMpt().SplitPath(&drive, &dir, &fileName, nullptr);

		filename = drive;
		filename += dir;
		filename += fileName;
		if(!strstr(fileName.ToUTF8().c_str(), "compat"))
			filename += MPT_PATHSTRING(".compat.");
		else
			filename += MPT_PATHSTRING(".");
		filename += mpt::PathString::FromUTF8(ext);
	}

	FileDialog dlg = SaveFileDialog()
		.DefaultExtension(ext)
		.DefaultFilename(filename)
		.ExtensionFilter(pattern)
		.WorkingDirectory(TrackerDirectories::Instance().GetWorkingDirectory(DIR_MODS));
	if(!dlg.Show()) return;

	ScopedLogCapturer logcapturer(*this);
	FixNullStrings();
	switch (type)
	{
		case MOD_TYPE_XM:
			m_SndFile.SaveXM(dlg.GetFirstFile(), true);
			break;
		case MOD_TYPE_IT:
			m_SndFile.SaveIT(dlg.GetFirstFile(), true);
			break;
	}
}


void CModDoc::OnPlayerPlay()
//--------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	if (pMainFrm)
	{
		CChildFrame *pChildFrm = (CChildFrame *) GetChildFrame();
 		if (strcmp("CViewPattern", pChildFrm->GetCurrentViewClassName()) == 0)
		{
			//User has sent play song command: set loop pattern checkbox to false.
			pChildFrm->SendViewMessage(VIEWMSG_PATTERNLOOP, 0);
		}

		bool isPlaying = (pMainFrm->GetModPlaying() == this);
		if(isPlaying && !m_SndFile.m_SongFlags[SONG_PAUSED | SONG_STEP/*|SONG_PATTERNLOOP*/])
		{
			OnPlayerPause();
			return;
		}

		CriticalSection cs;

		for(CHANNELINDEX i = m_SndFile.GetNumChannels(); i < MAX_CHANNELS; i++) if (!m_SndFile.m_PlayState.Chn[i].nMasterChn)
		{
			m_SndFile.m_PlayState.Chn[i].dwFlags.set(CHN_NOTEFADE | CHN_KEYOFF);
			if (!isPlaying) m_SndFile.m_PlayState.Chn[i].nLength = 0;
		}

		m_SndFile.m_PlayState.m_bPositionChanged = true;

		if(isPlaying)
		{
			m_SndFile.StopAllVsti();
		}

		cs.Leave();

		m_SndFile.m_SongFlags.reset(SONG_STEP | SONG_PAUSED | SONG_PATTERNLOOP);
		pMainFrm->PlayMod(this);
	}
}


void CModDoc::OnPlayerPause()
//---------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	if (pMainFrm)
	{
		if (pMainFrm->GetModPlaying() == this)
		{
			bool isLooping = m_SndFile.m_SongFlags[SONG_PATTERNLOOP];
			PATTERNINDEX nPat = m_SndFile.m_PlayState.m_nPattern;
			ROWINDEX nRow = m_SndFile.m_PlayState.m_nRow;
			ROWINDEX nNextRow = m_SndFile.m_PlayState.m_nNextRow;
			pMainFrm->PauseMod();

			if ((isLooping) && (nPat < m_SndFile.Patterns.Size()))
			{
				CriticalSection cs;

				if ((m_SndFile.m_PlayState.m_nCurrentOrder < m_SndFile.Order.size()) && (m_SndFile.Order[m_SndFile.m_PlayState.m_nCurrentOrder] == nPat))
				{
					m_SndFile.m_PlayState.m_nNextOrder = m_SndFile.m_PlayState.m_nCurrentOrder;
					m_SndFile.m_PlayState.m_nNextRow = nNextRow;
					m_SndFile.m_PlayState.m_nRow = nRow;
				} else
				{
					for (ORDERINDEX nOrd = 0; nOrd < m_SndFile.Order.size(); nOrd++)
					{
						if (m_SndFile.Order[nOrd] == m_SndFile.Order.GetInvalidPatIndex()) break;
						if (m_SndFile.Order[nOrd] == nPat)
						{
							m_SndFile.m_PlayState.m_nCurrentOrder = nOrd;
							m_SndFile.m_PlayState.m_nNextOrder = nOrd;
							m_SndFile.m_PlayState.m_nNextRow = nNextRow;
							m_SndFile.m_PlayState.m_nRow = nRow;
							break;
						}
					}
				}
			}
		} else
		{
			pMainFrm->PauseMod();
		}
	}
}


void CModDoc::OnPlayerStop()
//--------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	if (pMainFrm) pMainFrm->StopMod();
}


void CModDoc::OnPlayerPlayFromStart()
//-----------------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	if (pMainFrm)
	{
		CChildFrame *pChildFrm = (CChildFrame *) GetChildFrame();
		if (strcmp("CViewPattern", pChildFrm->GetCurrentViewClassName()) == 0)
		{
			//User has sent play song command: set loop pattern checkbox to false.
			pChildFrm->SendViewMessage(VIEWMSG_PATTERNLOOP, 0);
		}
		
		pMainFrm->PauseMod();
		CriticalSection cs;
		m_SndFile.m_SongFlags.reset(SONG_STEP | SONG_PATTERNLOOP);
		m_SndFile.SetCurrentPos(0);
		//m_SndFile.visitedSongRows.Initialize(true);
		m_SndFile.m_PlayState.m_lTotalSampleCount = 0;

		m_SndFile.m_PlayState.m_bPositionChanged = true;

		cs.Leave();

		pMainFrm->PlayMod(this);
	}
}


void CModDoc::OnEditGlobals()
//---------------------------
{
	SendMessageToActiveViews(WM_MOD_ACTIVATEVIEW,	IDD_CONTROL_GLOBALS);
}


void CModDoc::OnEditPatterns()
//----------------------------
{
	SendMessageToActiveViews(WM_MOD_ACTIVATEVIEW, IDD_CONTROL_PATTERNS, -1);
}


void CModDoc::OnEditSamples()
//---------------------------
{
	SendMessageToActiveViews(WM_MOD_ACTIVATEVIEW, IDD_CONTROL_SAMPLES);
}


void CModDoc::OnEditInstruments()
//-------------------------------
{
	//if (m_SndFile.m_nInstruments) rewbs.cosmetic: allow keyboard access to instruments even with no instruments
	SendMessageToActiveViews(WM_MOD_ACTIVATEVIEW, IDD_CONTROL_INSTRUMENTS);
}


void CModDoc::OnEditComments()
//----------------------------
{
	SendMessageToActiveViews(WM_MOD_ACTIVATEVIEW, IDD_CONTROL_COMMENTS);
}

//rewbs.graph
void CModDoc::OnEditGraph()
//-------------------------
{
	if (m_SndFile.GetType() & (MOD_TYPE_XM | MOD_TYPE_IT | MOD_TYPE_MPT)) SendMessageToActiveViews(WM_MOD_ACTIVATEVIEW, IDD_CONTROL_GRAPH);
}
//end rewbs.graph


void CModDoc::OnShowCleanup()
//---------------------------
{
	CModCleanupDlg dlg(*this, CMainFrame::GetMainFrame());
	dlg.DoModal();
}


// Enable menu item only module types that support MIDI Mappings
void CModDoc::OnUpdateHasMIDIMappings(CCmdUI *p)
//----------------------------------------------
{
	if(p)
		p->Enable((m_SndFile.GetModSpecifications().MIDIMappingDirectivesMax > 0) ? TRUE : FALSE);
}


// Enable menu item only for IT / MPTM / XM files
void CModDoc::OnUpdateXMITMPTOnly(CCmdUI *p)
//---------------------------------------
{
	if (p)
		p->Enable((m_SndFile.GetType() & (MOD_TYPE_XM | MOD_TYPE_IT | MOD_TYPE_MPT)) ? TRUE : FALSE);
}


// Enable menu item only for IT / MPTM files
void CModDoc::OnUpdateITMPTOnly(CCmdUI *p)
//---------------------------------------
{
	if (p)
		p->Enable((m_SndFile.GetType() & (MOD_TYPE_IT | MOD_TYPE_MPT)) ? TRUE : FALSE);
}


// Enable menu item if current module type supports compatibility export
void CModDoc::OnUpdateCompatExportableOnly(CCmdUI *p)
//---------------------------------------------------
{
	if (p)
		p->Enable((m_SndFile.GetType() & (MOD_TYPE_XM | MOD_TYPE_IT)) ? TRUE : FALSE);
}


void CModDoc::OnInsertPattern()
//-----------------------------
{
	const PATTERNINDEX pat = InsertPattern();
	if(pat != PATTERNINDEX_INVALID)
	{
		ORDERINDEX ord = 0;
		for (ORDERINDEX i = 0; i < m_SndFile.Order.size(); i++)
		{
			if (m_SndFile.Order[i] == pat) ord = i;
			if (m_SndFile.Order[i] == m_SndFile.Order.GetInvalidPatIndex()) break;
		}
		ViewPattern(pat, ord);
	}
}


void CModDoc::OnInsertSample()
//----------------------------
{
	SAMPLEINDEX smp = InsertSample();
	if (smp != SAMPLEINDEX_INVALID) ViewSample(smp);
}


void CModDoc::OnInsertInstrument()
//--------------------------------
{
	INSTRUMENTINDEX ins = InsertInstrument();
	if (ins != INSTRUMENTINDEX_INVALID) ViewInstrument(ins);
}



void CModDoc::OnEstimateSongLength()
//----------------------------------
{
	CString s;
	double songLength = m_SndFile.GetSongTime();
	if(songLength != std::numeric_limits<double>::infinity())
	{
		songLength = Util::Round(songLength);
		s.Format("Approximate song length: %.0fmn%02.0fs", std::floor(songLength / 60.0), fmod(songLength, 60.0));
	} else
	{
		s = "Song too long!";
	}
	Reporting::Information(s);
}

void CModDoc::OnApproximateBPM()
//------------------------------
{
	//Convert BPM to string:
	CString Message;
	double bpm = CMainFrame::GetMainFrame()->GetApproxBPM();


	switch(m_SndFile.m_nTempoMode)
	{
		case tempo_mode_alternative:
			Message.Format("Using alternative tempo interpretation.\n\nAssuming:\n. %d ticks per second\n. %d ticks per row\n. %d rows per beat\nthe tempo is approximately: %.8g BPM",
			m_SndFile.m_PlayState.m_nMusicTempo, m_SndFile.m_PlayState.m_nMusicSpeed, m_SndFile.m_PlayState.m_nCurrentRowsPerBeat, bpm);
			break;

		case tempo_mode_modern:
			Message.Format("Using modern tempo interpretation.\n\nThe tempo is: %.8g BPM", bpm);
			break;

		case tempo_mode_classic:
		default:
			Message.Format("Using standard tempo interpretation.\n\nAssuming:\n. A mod tempo (tick duration factor) of %d\n. %d ticks per row\n. %d rows per beat\nthe tempo is approximately: %.8g BPM",
			m_SndFile.m_PlayState.m_nMusicTempo, m_SndFile.m_PlayState.m_nMusicSpeed, m_SndFile.m_PlayState.m_nCurrentRowsPerBeat, bpm);
			break;
	}

	Reporting::Information(Message);
}


//rewbs.customKeys
void* CModDoc::GetChildFrame()
//----------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	if (!pMainFrm) return nullptr;
	CMDIChildWnd *pMDIActive = pMainFrm->MDIGetActive();
	if (pMDIActive)
	{
		CView *pView = pMDIActive->GetActiveView();
		if ((pView) && (pView->GetDocument() == this))
			return ((CChildFrame *)pMDIActive);
	}
	POSITION pos = GetFirstViewPosition();
	while (pos != NULL)
	{
		CView *pView = GetNextView(pos);
		if ((pView) && (pView->GetDocument() == this))
			return (CChildFrame *)pView->GetParentFrame();
	}

	return nullptr;
}

HWND CModDoc::GetEditPosition(ROWINDEX &row, PATTERNINDEX &pat, ORDERINDEX &ord)
//------------------------------------------------------------------------------
{
	HWND followSonghWnd;
	CChildFrame *pChildFrm = (CChildFrame *) GetChildFrame();

	if(strcmp("CViewPattern", pChildFrm->GetCurrentViewClassName()) == 0) // dirty HACK
	{
		followSonghWnd = pChildFrm->GetHwndView();
		PATTERNVIEWSTATE patternViewState;
		pChildFrm->SendViewMessage(VIEWMSG_SAVESTATE, (LPARAM)(&patternViewState));
		
		pat = patternViewState.nPattern;
		row = patternViewState.cursor.GetRow();
		ord = patternViewState.nOrder;
	} else
	{
		//patern editor object does not exist (i.e. is not active)  - use saved state.
		followSonghWnd = NULL;
		PATTERNVIEWSTATE *patternViewState = pChildFrm->GetPatternViewState();

		pat = patternViewState->nPattern;
		row = patternViewState->cursor.GetRow();
		ord = patternViewState->nOrder;
	}

	if(ord >= m_SndFile.Order.size())
	{
		ord = 0;
		pat = m_SndFile.Order[ord];
	}
	if(!m_SndFile.Patterns.IsValidPat(pat))
	{
		pat = 0;
		row = 0;
	} else if(row >= m_SndFile.Patterns[pat].GetNumRows())
	{
		row = 0;
	}

	//ensure order correlates with pattern.
	if(m_SndFile.Order[ord] != pat)
	{
		ORDERINDEX tentativeOrder = m_SndFile.Order.FindOrder(pat);
		if (tentativeOrder != ORDERINDEX_INVALID)	//ensure a valid order exists.
		{
			ord = tentativeOrder;
		}
	}

	return followSonghWnd;

}


////////////////////////////////////////////////////////////////////////////////////////
// Playback


void CModDoc::OnPatternRestart(bool loop)
//---------------------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	CChildFrame *pChildFrm = (CChildFrame *) GetChildFrame();

	if ((pMainFrm) && (pChildFrm))
	{
		if (strcmp("CViewPattern", pChildFrm->GetCurrentViewClassName()) == 0)
		{
			//User has sent play pattern command: set loop pattern checkbox to true.
			pChildFrm->SendViewMessage(VIEWMSG_PATTERNLOOP, loop ? 1 : 0);
		}

		ROWINDEX nRow;
		PATTERNINDEX nPat;
		ORDERINDEX nOrd;

		HWND followSonghWnd;

		followSonghWnd = GetEditPosition(nRow, nPat, nOrd);
		CModDoc *pModPlaying = pMainFrm->GetModPlaying();
		
		CriticalSection cs;

		// Cut instruments/samples
		for(CHANNELINDEX i = 0; i < MAX_CHANNELS; i++)
		{
			m_SndFile.m_PlayState.Chn[i].nPatternLoopCount = 0;
			m_SndFile.m_PlayState.Chn[i].nPatternLoop = 0;
			m_SndFile.m_PlayState.Chn[i].nFadeOutVol = 0;
			m_SndFile.m_PlayState.Chn[i].dwFlags.set(CHN_NOTEFADE | CHN_KEYOFF);
		}
		if ((nOrd < m_SndFile.Order.size()) && (m_SndFile.Order[nOrd] == nPat)) m_SndFile.m_PlayState.m_nCurrentOrder = m_SndFile.m_PlayState.m_nNextOrder = nOrd;
		m_SndFile.m_SongFlags.reset(SONG_PAUSED | SONG_STEP);
		if(loop)
			m_SndFile.LoopPattern(nPat);
		else
			m_SndFile.LoopPattern(PATTERNINDEX_INVALID);
		m_SndFile.m_PlayState.m_nNextRow = 0;

		// set playback timer in the status bar (and update channel status)
		SetElapsedTime(nOrd, 0, true);

		if(pModPlaying == this)
		{
			m_SndFile.StopAllVsti();
		}

		cs.Leave();
		
		if(pModPlaying != this)
		{
			SetNotifications(m_notifyType|Notification::Position|Notification::VUMeters, m_notifyItem);
			SetFollowWnd(followSonghWnd);
			pMainFrm->PlayMod(this); //rewbs.fix2977
		}
	}
	//SwitchToView();
}

void CModDoc::OnPatternPlay()
//---------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	CChildFrame *pChildFrm = (CChildFrame *) GetChildFrame();

	if ((pMainFrm) && (pChildFrm))
	{
		if (strcmp("CViewPattern", pChildFrm->GetCurrentViewClassName()) == 0)
		{
			//User has sent play pattern command: set loop pattern checkbox to true.
			pChildFrm->SendViewMessage(VIEWMSG_PATTERNLOOP, 1);
		}

		ROWINDEX nRow;
		PATTERNINDEX nPat;
		ORDERINDEX nOrd;

		HWND followSonghWnd;

		followSonghWnd = GetEditPosition(nRow,nPat,nOrd);
		CModDoc *pModPlaying = pMainFrm->GetModPlaying();
	
		CriticalSection cs;

		// Cut instruments/samples
		for(CHANNELINDEX i = m_SndFile.GetNumChannels(); i < MAX_CHANNELS; i++)
		{
			m_SndFile.m_PlayState.Chn[i].dwFlags.set(CHN_NOTEFADE | CHN_KEYOFF);
		}
		if ((nOrd < m_SndFile.Order.size()) && (m_SndFile.Order[nOrd] == nPat)) m_SndFile.m_PlayState.m_nCurrentOrder = m_SndFile.m_PlayState.m_nNextOrder = nOrd;
		m_SndFile.m_SongFlags.reset(SONG_PAUSED | SONG_STEP);
		m_SndFile.LoopPattern(nPat);
		m_SndFile.m_PlayState.m_nNextRow = nRow;
		
		// set playback timer in the status bar (and update channel status)
		SetElapsedTime(nOrd, nRow, true);

		if(pModPlaying == this)
		{
			m_SndFile.StopAllVsti();
		}

		cs.Leave();

		if(pModPlaying != this)
		{
			SetNotifications(m_notifyType|Notification::Position|Notification::VUMeters, m_notifyItem);
			SetFollowWnd(followSonghWnd);
			pMainFrm->PlayMod(this);  //rewbs.fix2977
		}
	}
	//SwitchToView();

}

void CModDoc::OnPatternPlayNoLoop()
//---------------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	CChildFrame *pChildFrm = (CChildFrame *) GetChildFrame();

	if ((pMainFrm) && (pChildFrm))
	{
		if (strcmp("CViewPattern", pChildFrm->GetCurrentViewClassName()) == 0)
		{
			//User has sent play song command: set loop pattern checkbox to false.
			pChildFrm->SendViewMessage(VIEWMSG_PATTERNLOOP, 0);
		}

		ROWINDEX nRow;
		PATTERNINDEX nPat;
		ORDERINDEX nOrd;

		HWND followSonghWnd;

		followSonghWnd = GetEditPosition(nRow,nPat,nOrd);
		CModDoc *pModPlaying = pMainFrm->GetModPlaying();

		CriticalSection cs;
		// Cut instruments/samples
		for(CHANNELINDEX i = m_SndFile.GetNumChannels(); i < MAX_CHANNELS; i++)
		{
			m_SndFile.m_PlayState.Chn[i].dwFlags.set(CHN_NOTEFADE | CHN_KEYOFF);
		}
		m_SndFile.m_SongFlags.reset(SONG_PAUSED | SONG_STEP);
		m_SndFile.SetCurrentOrder(nOrd);
		if (m_SndFile.Order[nOrd] == nPat)
			m_SndFile.DontLoopPattern(nPat, nRow);
		else
			m_SndFile.LoopPattern(nPat);
		m_SndFile.m_PlayState.m_nNextRow = nRow;

		// set playback timer in the status bar (and update channel status)
		SetElapsedTime(nOrd, nRow, true);

		if(pModPlaying == this)
		{
			m_SndFile.StopAllVsti();
		}

		cs.Leave();

		if(pModPlaying != this)
		{
			SetNotifications(m_notifyType|Notification::Position|Notification::VUMeters, m_notifyItem);
			SetFollowWnd(followSonghWnd);
			pMainFrm->PlayMod(this);  //rewbs.fix2977
		}
	}
	//SwitchToView();
}


void CModDoc::OnViewEditHistory()
//-------------------------------
{
	CEditHistoryDlg dlg(CMainFrame::GetMainFrame(), this);
	dlg.DoModal();
}


void CModDoc::OnViewMPTHacks()
//----------------------------
{
	ScopedLogCapturer logcapturer(*this);
	if(!HasMPTHacks())
	{
		AddToLog("No hacks found.");
	}
}


LRESULT CModDoc::OnCustomKeyMsg(WPARAM wParam, LPARAM /*lParam*/)
//---------------------------------------------------------------
{
	if (wParam == kcNull)
		return NULL;

	switch(wParam)
	{
		case kcViewGeneral: OnEditGlobals(); break;
		case kcViewPattern: OnEditPatterns(); break;
		case kcViewSamples: OnEditSamples(); break;
		case kcViewInstruments: OnEditInstruments(); break;
		case kcViewComments: OnEditComments(); break;
		case kcViewGraph: OnEditGraph(); break; //rewbs.graph
		case kcViewSongProperties: SongProperties(); break;

		case kcFileSaveAsWave:	OnFileWaveConvert(); break;
		case kcFileSaveAsMP3:	OnFileMP3Convert(); break;
		case kcFileSaveMidi:	OnFileMidiConvert(); break;
		case kcFileExportCompat:  OnFileCompatibilitySave(); break;
		case kcEstimateSongLength: OnEstimateSongLength(); break;
		case kcApproxRealBPM:	OnApproximateBPM(); break;
		case kcFileSave:		DoSave(GetPathNameMpt(), 0); break;
		case kcFileSaveAs:		DoSave(mpt::PathString(), TRUE); break;
		case kcFileSaveTemplate: OnSaveTemplateModule(); break;
		case kcFileClose:		SafeFileClose(); break;
		case kcFileAppend:		OnAppendModule(); break;

		case kcPlayPatternFromCursor: OnPatternPlay(); break;
		case kcPlayPatternFromStart: OnPatternRestart(); break;
		case kcPlaySongFromCursor: OnPatternPlayNoLoop(); break;
		case kcPlaySongFromStart: OnPlayerPlayFromStart(); break;
		case kcPlayPauseSong: OnPlayerPlay(); break;
		case kcPlaySongFromPattern: OnPatternRestart(false); break;
		case kcStopSong: OnPlayerStop(); break;
		case kcPanic: OnPanic(); break;
//		case kcPauseSong: OnPlayerPause(); break;
			

	}

	return wParam;
}
//end rewbs.customKeys

void CModDoc::TogglePluginEditor(UINT m_nCurrentPlugin)
//-----------------------------------------------------
{
	if(m_nCurrentPlugin < MAX_MIXPLUGINS)
	{
		SNDMIXPLUGIN &plugin = m_SndFile.m_MixPlugins[m_nCurrentPlugin];
		CVstPlugin *pVstPlugin = dynamic_cast<CVstPlugin *>(plugin.pMixPlugin);
		if(pVstPlugin != nullptr)
		{
			pVstPlugin->ToggleEditor();
		}
	}
}

void CModDoc::ChangeFileExtension(MODTYPE nNewType)
//-------------------------------------------------
{
	//Not making path if path is empty(case only(?) for new file)
	if(!GetPathNameMpt().empty())
	{
		mpt::PathString drive;
		mpt::PathString dir;
		mpt::PathString fname;
		mpt::PathString fext;
		GetPathNameMpt().SplitPath(&drive, &dir, &fname, &fext);

		mpt::PathString newPath = drive + dir;

		//Catch case where we don't have a filename yet.
		if(fname.empty())
		{
			newPath += mpt::PathString::FromCStringSilent(GetTitle()).SanitizeComponent();
		} else
		{
			newPath += fname;
		}

		newPath += MPT_PATHSTRING(".") + mpt::PathString::FromUTF8(CSoundFile::GetModSpecifications(nNewType).fileExtension);

		//Forcing savedialog to appear after extension change - otherwise
		//unnotified file overwriting may occur.
		m_ShowSavedialog = true;

		SetPathName(newPath, FALSE);
	}

	UpdateAllViews(NULL, HINT_MODTYPE);
}


CHANNELINDEX CModDoc::FindAvailableChannel() const
//------------------------------------------------
{
	CHANNELINDEX nStoppedChannel = CHANNELINDEX_INVALID;
	// Search for available channel
	for(CHANNELINDEX j = MAX_CHANNELS - 1; j >= m_SndFile.m_nChannels; j--)
	{
		const ModChannel &chn = m_SndFile.m_PlayState.Chn[j];
		if(!chn.nLength)
			return j;
		else if(chn.dwFlags[CHN_NOTEFADE])
			nStoppedChannel = j;
	}

	// Nothing found: return one that's stopped
	if(nStoppedChannel != CHANNELINDEX_INVALID)
		return nStoppedChannel;
	
	//Last resort: go for first virtual channel.
	return m_SndFile.m_nChannels;
}


void CModDoc::RecordParamChange(PLUGINDEX plugSlot, PlugParamIndex paramIndex)
//----------------------------------------------------------------------------
{
	SendMessageToActiveViews(WM_MOD_RECORDPARAM, plugSlot, paramIndex);
}


void CModDoc::LearnMacro(int macroToSet, PlugParamIndex paramToUse)
//-----------------------------------------------------------------
{
	if (macroToSet < 0 || macroToSet > NUM_MACROS)
	{
		return;
	}

	//if macro already exists for this param, alert user and return
	for (int checkMacro = 0; checkMacro < NUM_MACROS; checkMacro++)
	{
		int macroType = m_SndFile.m_MidiCfg.GetParameteredMacroType(checkMacro);
		
		if (macroType == sfx_plug && m_SndFile.m_MidiCfg.MacroToPlugParam(checkMacro) == paramToUse)
		{
			CString message;
			message.Format("Parameter %02d can already be controlled with macro %X.", paramToUse, checkMacro);
			Reporting::Information(message, "Macro exists for this parameter");
			return;
		}
	}

	//set new macro
	if(paramToUse < 384)
	{
		m_SndFile.m_MidiCfg.CreateParameteredMacro(macroToSet, sfx_plug, paramToUse);
	} else
	{
		CString message;
		message.Format("Parameter %02d beyond controllable range. Use Parameter Control Events to automate this parameter.", paramToUse);
		Reporting::Information(message, "Macro not assigned for this parameter");
		return;
	}

	CString message;
	message.Format("Parameter %02d can now be controlled with macro %X.", paramToUse, macroToSet);
	Reporting::Information(message, "Macro assigned for this parameter");
	
	return;
}


void CModDoc::SongProperties()
//----------------------------
{
	CModTypeDlg dlg(m_SndFile, CMainFrame::GetMainFrame());
	if (dlg.DoModal() == IDOK)
	{	
		ScopedLogCapturer logcapturer(*this, "Conversion Status");
		bool bShowLog = false;
		if(dlg.m_nType)
		{
			if (!ChangeModType(dlg.m_nType)) return;
			bShowLog = true;
		}
		
		CHANNELINDEX nNewChannels = Clamp(dlg.m_nChannels, m_SndFile.GetModSpecifications().channelsMin, m_SndFile.GetModSpecifications().channelsMax);

		if (nNewChannels != GetNumChannels())
		{
			const bool showCancelInRemoveDlg = m_SndFile.GetModSpecifications().channelsMax >= m_SndFile.GetNumChannels();
			if(ChangeNumChannels(nNewChannels, showCancelInRemoveDlg)) bShowLog = true;
		}

		// Force update of pattern highlights / num channels
		UpdateAllViews(NULL, HINT_PATTERNDATA | HINT_MODCHANNELS);

		SetModified();
	}
}


// Sets playback timer to playback time at given position.
// At the same time, the playback parameters (global volume, channel volume and stuff like that) are calculated for this position.
// Sample channels positions are only updated if setSamplePos is true *and* the user has chosen to update sample play positions on seek.
void CModDoc::SetElapsedTime(ORDERINDEX nOrd, ROWINDEX nRow, bool setSamplePos)
//-----------------------------------------------------------------------------
{
	double t = m_SndFile.GetPlaybackTimeAt(nOrd, nRow, true, setSamplePos && (TrackerSettings::Instance().m_dwPatternSetup & PATTERN_SYNCSAMPLEPOS) != 0);
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	if(pMainFrm != nullptr)
	{
		pMainFrm->SetElapsedTime(std::max(0.0, t));
	}
}


CString CModDoc::GetPatternViewInstrumentName(INSTRUMENTINDEX nInstr,
											  bool bEmptyInsteadOfNoName /* = false*/,
											  bool bIncludeIndex /* = true*/) const
//-----------------------------------------------------------------------------------
{
	if(nInstr >= MAX_INSTRUMENTS || m_SndFile.GetNumInstruments() == 0 || m_SndFile.Instruments[nInstr] == nullptr)
		return TEXT("");

	CString displayName, instrumentName, pluginName;
					
	// Get instrument name.
	instrumentName = m_SndFile.GetInstrumentName(nInstr);

	// If instrument name is empty, use name of the sample mapped to C-5.
	if (instrumentName.IsEmpty())
	{
		const SAMPLEINDEX nSmp = m_SndFile.Instruments[nInstr]->Keyboard[NOTE_MIDDLEC - 1];
		if (nSmp <= m_SndFile.GetNumSamples() && m_SndFile.GetSample(nSmp).pSample)
			instrumentName.Format(TEXT("s: %s"), m_SndFile.GetSampleName(nSmp));
	}

	// Get plugin name.
	const PLUGINDEX nPlug = m_SndFile.Instruments[nInstr]->nMixPlug;
	if (nPlug > 0 && nPlug < MAX_MIXPLUGINS)
		pluginName = m_SndFile.m_MixPlugins[nPlug-1].GetName();

	if (pluginName.IsEmpty())
	{
		if(bEmptyInsteadOfNoName && instrumentName.IsEmpty())
			return TEXT("");
		if(instrumentName.IsEmpty())
			instrumentName = TEXT("(no name)");
		if (bIncludeIndex)
			displayName.Format(TEXT("%02d: %s"), nInstr, (LPCTSTR)instrumentName);
		else
			displayName.Format(TEXT("%s"), (LPCTSTR)instrumentName);
	} else
	{
		if (bIncludeIndex)
			displayName.Format(TEXT("%02d: %s (%s)"), nInstr, (LPCTSTR)instrumentName, (LPCTSTR)pluginName);
		else
			displayName.Format(TEXT("%s (%s)"), (LPCTSTR)instrumentName, (LPCTSTR)pluginName);
	}
	return displayName;
}


void CModDoc::SafeFileClose()
//---------------------------
{
	// Verify that the main window has the focus. This saves us a lot of trouble because active dialogs normally don't check if their pSndFile pointers are still valid.
	if(GetActiveWindow() == CMainFrame::GetMainFrame()->m_hWnd)
		OnFileClose();
}


// "Panic button". At the moment, it just resets all VSTi and sample notes.
void CModDoc::OnPanic()
//---------------------
{
	CriticalSection cs;
	m_SndFile.ResetChannels();
	m_SndFile.StopAllVsti();
}


// Before saving, make sure that every char after the terminating null char is also null.
// Else, garbage might end up in various text strings that wasn't supposed to be there.
void CModDoc::FixNullStrings()
//----------------------------
{
	// Song title
	// std::string, doesn't need to be fixed.

	// Sample names + filenames
	for(SAMPLEINDEX nSmp = 1; nSmp <= m_SndFile.GetNumSamples(); nSmp++)
	{
		mpt::String::FixNullString(m_SndFile.m_szNames[nSmp]);
		mpt::String::FixNullString(m_SndFile.GetSample(nSmp).filename);
	}

	// Instrument names
	for(INSTRUMENTINDEX nIns = 1; nIns <= m_SndFile.GetNumInstruments(); nIns++)
	{
		if(m_SndFile.Instruments[nIns] != nullptr)
		{
			mpt::String::FixNullString(m_SndFile.Instruments[nIns]->name);
			mpt::String::FixNullString(m_SndFile.Instruments[nIns]->filename);
		}
	}

	// Channel names
	for(CHANNELINDEX nChn = 0; nChn < m_SndFile.GetNumChannels(); nChn++)
	{
		mpt::String::FixNullString(m_SndFile.ChnSettings[nChn].szName);
	}

	// Macros
	m_SndFile.m_MidiCfg.Sanitize();

	// Pattern names
	// std::string, doesn't need to be fixed.
	
	// Sequence names.
	// std::string, doesn't need to be fixed.
}


void CModDoc::OnSaveTemplateModule()
//----------------------------------
{
	// Create template folder if doesn't exist already.
	const mpt::PathString templateFolder = TrackerDirectories::Instance().GetDefaultDirectory(DIR_TEMPLATE_FILES_USER);
	if (!templateFolder.IsDirectory())
	{
		if (!CreateDirectoryW(templateFolder.AsNative().c_str(), nullptr))
		{
			Reporting::Notification(L"Error: Unable to create template folder '" + templateFolder.ToWide() + L"'");
			return;
		}
	}

	// Generate file name candidate.
	mpt::PathString sName;
	for(size_t i = 0; i < 1000; ++i)
	{
		sName += MPT_PATHSTRING("newTemplate") + mpt::PathString::FromWide(StringifyW(i));
		sName += MPT_PATHSTRING(".") + mpt::PathString::FromUTF8(m_SndFile.GetModSpecifications().fileExtension);
		if (!Util::sdOs::IsPathFileAvailable(templateFolder + sName, Util::sdOs::FileModeExists))
			break;
	}

	// Ask file name from user.
	FileDialog dlg = SaveFileDialog()
		.DefaultExtension(m_SndFile.GetModSpecifications().fileExtension)
		.DefaultFilename(sName)
		.ExtensionFilter(ModTypeToFilter(m_SndFile))
		.WorkingDirectory(templateFolder);
	if(!dlg.Show())
		return;

	const CString sOldPath = m_strPathName;
	OnSaveDocument(dlg.GetFirstFile(), true/*template file*/);
	m_strPathName = sOldPath;
}


// Create an undo point that stores undo data for all existing patterns
void CModDoc::PrepareUndoForAllPatterns(bool storeChannelInfo, const char *description)
//-------------------------------------------------------------------------------------
{
	bool linkUndo = false;

	PATTERNINDEX lastPat = PATTERNINDEX_INVALID;
	for(PATTERNINDEX pat = 0; pat < m_SndFile.Patterns.Size(); pat++)
	{
		if(m_SndFile.Patterns.IsValidPat(pat)) lastPat = pat;
	}

	for(PATTERNINDEX pat = 0; pat < m_SndFile.Patterns.Size(); pat++)
	{
		if(m_SndFile.Patterns.IsValidPat(pat))
		{
			GetPatternUndo().PrepareUndo(pat, 0, 0, GetNumChannels(), m_SndFile.Patterns[pat].GetNumRows(), description, linkUndo, storeChannelInfo && pat == lastPat);
			linkUndo = true;
		}
	}
}


OPENMPT_NAMESPACE_END
