#include "stdafx.h"
#include <uuids.h>
#include <dmoreg.h>
#include <shlwapi.h>
#include <medparam.h>
#include "mainfrm.h"
#include "vstplug.h"
#include "moddoc.h"
#include "sndfile.h"
#include "fxp.h"					//rewbs.VSTpresets
#include "AbstractVstEditor.h"		//rewbs.defaultPlugGUI
#include "VstEditor.h"				//rewbs.defaultPlugGUI
#include "defaultvsteditor.h"		//rewbs.defaultPlugGUI
#include "midi.h"
#include "version.h"
#include "midimappingdialog.h"
#include "../common/StringFixer.h"
#ifdef VST_USE_ALTERNATIVE_MAGIC	//Pelya's plugin ID fix. Breaks fx presets, so let's avoid it for now.
#define ZLIB_WINAPI
#include "../zlib/zlib.h"			//For CRC32 calculation (to detect plugins with same UID)
#endif // VST_USE_ALTERNATIVE_MAGIC

#define new DEBUG_NEW

#ifndef NO_VST

char CVstPluginManager::s_szHostProductString[64] = "OpenMPT";
char CVstPluginManager::s_szHostVendorString[64] = "OpenMPT project";
VstIntPtr CVstPluginManager::s_nHostVendorVersion = MptVersion::num;

//#define VST_LOG
//#define ENABLE_BUZZ
#define DMO_LOG

#ifdef ENABLE_BUZZ
#define STATIC_BUILD
#include <machineinterface.h>	// Buzz
AEffect *Buzz2Vst(CMachineInterface *pBuzzMachine, const CMachineInfo *pBuzzInfo);
#endif // ENABLE_BUZZ

AEffect *DmoToVst(PVSTPLUGINLIB pLib);

#ifdef VST_USE_ALTERNATIVE_MAGIC
UINT32 CalculateCRC32fromFilename(const char *s)
//----------------------------------------------
{
	char fn[_MAX_PATH];
	strncpy(fn, s, sizeof(fn));
	fn[sizeof(fn)-1] = 0;
	int f;
	for(f = 0; fn[f] != 0; f++) fn[f] = toupper(fn[f]);
	return LittleEndian(crc32(0, (BYTE *)fn, f));

}
#endif // VST_USE_ALTERNATIVE_MAGIC

VstIntPtr VSTCALLBACK CVstPluginManager::MasterCallBack(AEffect *effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float opt)
//----------------------------------------------------------------------------------------------------------------------------------------------
{
	CVstPluginManager *that = theApp.GetPluginManager();
	if (that)
	{
		return that->VstCallback(effect, opcode, index, value, ptr, opt);
	}
	return 0;
}


BOOL CVstPluginManager::CreateMixPluginProc(PSNDMIXPLUGIN pMixPlugin, CSoundFile* pSndFile)
//-----------------------------------------------------------------------------------------
{
	CVstPluginManager *that = theApp.GetPluginManager();
	if (that)
	{
		return that->CreateMixPlugin(pMixPlugin, pSndFile);
	}
	return FALSE;
}


CVstPluginManager::CVstPluginManager()
//------------------------------------
{
	m_pVstHead = NULL;
	//m_bNeedIdle = FALSE; //rewbs.VSTCompliance - now member of plugin class
	CSoundFile::gpMixPluginCreateProc = CreateMixPluginProc;
	EnumerateDirectXDMOs();
}


CVstPluginManager::~CVstPluginManager()
//-------------------------------------
{
	CSoundFile::gpMixPluginCreateProc = NULL;
	while (m_pVstHead)
	{
		PVSTPLUGINLIB p = m_pVstHead;
		m_pVstHead = m_pVstHead->pNext;
		if (m_pVstHead) m_pVstHead->pPrev = NULL;
		p->pPrev = p->pNext = NULL;
		while ((volatile void *)(p->pPluginsList) != NULL)
		{
			p->pPluginsList->Release();
		}
		delete p;
	}
}


BOOL CVstPluginManager::IsValidPlugin(const VSTPLUGINLIB *pLib)
//-------------------------------------------------------------
{
	PVSTPLUGINLIB p = m_pVstHead;
	while (p)
	{
		if (p == pLib) return TRUE;
		p = p->pNext;
	}
	return FALSE;
}


void CVstPluginManager::EnumerateDirectXDMOs()
//--------------------------------------------
{
	HKEY hkEnum;
	CHAR keyname[128];
	CHAR s[256];
	WCHAR w[100];
	LONG cr;
	DWORD index;

	cr = RegOpenKey(HKEY_LOCAL_MACHINE, "software\\classes\\DirectShow\\MediaObjects\\Categories\\f3602b3f-0592-48df-a4cd-674721e7ebeb", &hkEnum);
	index = 0;
	while (cr == ERROR_SUCCESS)
	{
		if ((cr = RegEnumKey(hkEnum, index, (LPTSTR)keyname, sizeof(keyname))) == ERROR_SUCCESS)
		{
			CLSID clsid;

			wsprintf(s, "{%s}", keyname);
			MultiByteToWideChar(CP_ACP, 0, (LPCSTR)s,-1,(LPWSTR)w,98);
			if (CLSIDFromString(w, &clsid) == S_OK)
			{
				HKEY hksub;

				wsprintf(s, "software\\classes\\DirectShow\\MediaObjects\\%s", keyname);
				if (RegOpenKey(HKEY_LOCAL_MACHINE, s, &hksub) == ERROR_SUCCESS)
				{
					DWORD datatype = REG_SZ;
					DWORD datasize = 64;

					if (ERROR_SUCCESS == RegQueryValueEx(hksub, NULL, 0, &datatype, (LPBYTE)s, &datasize))
					{
						PVSTPLUGINLIB p = new VSTPLUGINLIB;

						p->pPrev = NULL;
						p->pNext = m_pVstHead;
						p->dwPluginId1 = kDmoMagic;
						p->dwPluginId2 = clsid.Data1;
						p->pPluginsList = NULL;
						p->bIsInstrument = FALSE;
						lstrcpyn(p->szLibraryName, s, sizeof(p->szLibraryName));
						StringFromGUID2(clsid, w, 100);
						WideCharToMultiByte(CP_ACP, 0, w, -1, p->szDllPath, sizeof(p->szDllPath), NULL, NULL);
					#ifdef DMO_LOG
						if (theApp.IsDebug()) Log("Found \"%s\" clsid=%s\n", p->szLibraryName, p->szDllPath);
					#endif
						if (m_pVstHead) m_pVstHead->pPrev = p;
						m_pVstHead = p;
					}
					RegCloseKey(hksub);
				}
			}
		}
		index++;
	}
	if (hkEnum) RegCloseKey(hkEnum);
}

//
// PluginCache format:
// LibraryName = ID100000ID200000
// ID100000ID200000 = FullDllPath
// ID100000ID200000.Flags = Plugin Flags (for now, just isInstrument).



PVSTPLUGINLIB CVstPluginManager::AddPlugin(LPCSTR pszDllPath, BOOL bCache, const bool checkFileExistence, CString* const errStr)
//------------------------------------------------------------------------------------------------------------------------------
{
	TCHAR szPath[_MAX_PATH];

	if(checkFileExistence && (PathFileExists(pszDllPath) == FALSE))
	{
		if(errStr)
		{
			*errStr += "\nUnable to find ";
			*errStr += pszDllPath;
		}
	}

	PVSTPLUGINLIB pDup = m_pVstHead;
	while (pDup)
	{
		if (!lstrcmpi(pszDllPath, pDup->szDllPath)) return pDup;
		pDup = pDup->pNext;
	}
	// Look if the plugin info is stored in the PluginCache
	if (bCache)
	{
		const CString cacheSection = "PluginCache";
		const CString cacheFile = theApp.GetPluginCacheFileName();
		char fileName[_MAX_PATH];
		_splitpath(pszDllPath, NULL, NULL, fileName, NULL);

		CString IDs = CMainFrame::GetPrivateProfileCString(cacheSection, fileName, "", cacheFile);

		if (IDs.GetLength() >= 16)
		{
			// Get path from cache file
			GetPrivateProfileString(cacheSection, IDs, "", szPath, CountOf(szPath), cacheFile);
			StringFixer::SetNullTerminator(szPath);
			theApp.RelativePathToAbsolute(szPath);

			if ((szPath[0]) && (!lstrcmpi(szPath, pszDllPath)))
			{
				PVSTPLUGINLIB p;
				try
				{
					p = new VSTPLUGINLIB;
				} catch(MPTMemoryException)
				{
					return nullptr;
				}
				p->dwPluginId1 = 0;
				p->dwPluginId2 = 0;
				p->bIsInstrument = FALSE;
				p->pPluginsList = NULL;
				lstrcpyn(p->szDllPath, pszDllPath, sizeof(p->szDllPath));
				_splitpath(pszDllPath, NULL, NULL, p->szLibraryName, NULL);
				p->szLibraryName[63] = 0;
				p->pNext = m_pVstHead;
				p->pPrev = NULL;
				if (m_pVstHead) m_pVstHead->pPrev = p;
				m_pVstHead = p;
				// Extract plugin Ids
				for (UINT i=0; i<16; i++)
				{
					UINT n = ((LPCTSTR)IDs)[i] - '0';
					if (n > 9) n = ((LPCTSTR)IDs)[i] + 10 - 'A';
					n &= 0x0f;
					if (i < 8)
					{
						p->dwPluginId1 = (p->dwPluginId1<<4) | n;
					} else
					{
						p->dwPluginId2 = (p->dwPluginId2<<4) | n;
					}
				}
				CString flagKey;
				flagKey.Format("%s.Flags", IDs);
				int infoex = CMainFrame::GetPrivateProfileLong(cacheSection, flagKey, 0, cacheFile);
				if (infoex&1) p->bIsInstrument = TRUE;
			#ifdef VST_USE_ALTERNATIVE_MAGIC
				if( p->dwPluginId1 == kEffectMagic )
				{
					p->dwPluginId1 = CalculateCRC32fromFilename(p->szLibraryName); // Make Plugin ID unique for sure (for VSTs with same UID)
				};
			#endif // VST_USE_ALTERNATIVE_MAGIC
			#ifdef VST_LOG
				Log("Plugin \"%s\" found in PluginCache\n", p->szLibraryName);
			#endif // VST_LOG
				return p;
			} else
			{
			#ifdef VST_LOG
				Log("Plugin \"%s\" mismatch in PluginCache: \"%s\" [%s]=\"%s\"\n", s, pszDllPath, (LPCTSTR)IDs, (LPCTSTR)strFullPath);
			#endif // VST_LOG
			}
		}
	}

	HINSTANCE hLib = NULL;


	// If this key contains a file name on program launch, a plugin previously crashed OpenMPT.
	WritePrivateProfileString("VST Plugins", "FailedPlugin", pszDllPath, theApp.GetConfigFileName());

	try
	{
		hLib = LoadLibrary(pszDllPath);

#ifdef _DEBUG
		DWORD dw = GetLastError();
		if (!hLib && dw != ERROR_MOD_NOT_FOUND)	// "File not found errors" are annoying.
		{
			TCHAR szBuf[256];
			wsprintf(szBuf, "Warning: encountered problem when loading plugin dll. Error %d: %s", dw, (LPCTSTR)GetErrorMessage(dw));
			Reporting::Error(szBuf, "DEBUG: Error when loading plugin dll");
		}
#endif //_DEBUG

	} catch(...)
	{
		CVstPluginManager::ReportPlugException("Exception caught in LoadLibrary (%s)", pszDllPath);
	}
	if ((hLib) && (hLib != INVALID_HANDLE_VALUE))
	{
		BOOL bOk = FALSE;
		PVSTPLUGENTRY pMainProc = (PVSTPLUGENTRY)GetProcAddress(hLib, "main");
		if(pMainProc == NULL)
		{
			pMainProc = (PVSTPLUGENTRY)GetProcAddress(hLib, "VSTPluginMain");
		}
	#ifdef ENABLE_BUZZ
		GET_INFO pBuzzGetInfo = (GET_INFO)GetProcAddress(hLib, "GetInfo");
	#endif // ENABLE_BUZZ
		if (pMainProc)
		{
			PVSTPLUGINLIB p;
			try
			{
				p = new VSTPLUGINLIB;
			} catch(MPTMemoryException)
			{
				return nullptr;
			}
			p->dwPluginId1 = 0;
			p->dwPluginId2 = 0;
			p->bIsInstrument = FALSE;
			p->pPluginsList = NULL;
			lstrcpyn(p->szDllPath, pszDllPath, CountOf(p->szDllPath));
			_splitpath(pszDllPath, NULL, NULL, p->szLibraryName, NULL);
			p->szLibraryName[63] = 0;
			p->pNext = m_pVstHead;
			p->pPrev = NULL;
			if (m_pVstHead) m_pVstHead->pPrev = p;
			m_pVstHead = p;

			try
			{
				AEffect *pEffect = pMainProc(MasterCallBack);
				if ((pEffect) && (pEffect->magic == kEffectMagic)
				 && (pEffect->dispatcher))
				{
					pEffect->dispatcher(pEffect, effOpen, 0, 0, 0, 0);
				#ifdef VST_USE_ALTERNATIVE_MAGIC
					p->dwPluginId1 = CalculateCRC32fromFilename(p->szLibraryName); // Make Plugin ID unique for sure
				#else
					p->dwPluginId1 = pEffect->magic;
				#endif // VST_USE_ALTERNATIVE_MAGIC
					p->dwPluginId2 = pEffect->uniqueID;
					if ((pEffect->flags & effFlagsIsSynth) || (!pEffect->numInputs)) p->bIsInstrument = TRUE;
				#ifdef VST_LOG
					int nver = pEffect->dispatcher(pEffect, effGetVstVersion, 0,0, NULL, 0);
					if (!nver) nver = pEffect->version;
					Log("%-20s: v%d.0, %d in, %d out, %2d programs, %2d params, flags=0x%04X realQ=%d offQ=%d\n",
						p->szLibraryName, nver,
						pEffect->numInputs, pEffect->numOutputs,
						pEffect->numPrograms, pEffect->numParams,
						pEffect->flags, pEffect->realQualities, pEffect->offQualities);
				#endif // VST_LOG

					bOk = TRUE;
				}
			} catch(...)
			{
				CVstPluginManager::ReportPlugException("Exception while trying to load plugin \"%s\"!\n", p->szLibraryName);
			}

			// If OK, write the information in PluginCache
			if (bOk)
			{
				const CString cacheSection = "PluginCache";
				const CString cacheFile = theApp.GetPluginCacheFileName();
				CString IDs, flagsKey;
				IDs.Format("%08X%08X", p->dwPluginId1, p->dwPluginId2);
				flagsKey.Format("%s.Flags", IDs);

				_tcsncpy(szPath, pszDllPath, CountOf(szPath) - 1);
				if(theApp.IsPortableMode())
				{
					theApp.AbsolutePathToRelative(szPath);
				}
				StringFixer::SetNullTerminator(szPath);

				WritePrivateProfileString(cacheSection, IDs, szPath, cacheFile);
				CMainFrame::WritePrivateProfileCString(cacheSection, IDs, pszDllPath, cacheFile);
				CMainFrame::WritePrivateProfileCString(cacheSection, p->szLibraryName, IDs, cacheFile);
				CMainFrame::WritePrivateProfileLong(cacheSection, flagsKey, p->bIsInstrument, cacheFile);
			}
		} else
	#ifdef ENABLE_BUZZ
		if (pBuzzGetInfo)
		{
			PVSTPLUGINLIB p
			try
			{
				p = new VSTPLUGINLIB;
			} catch(MPTMemoryException)
			{
				return nullptr;
			}
			
			p->dwPluginId1 = kBuzzMagic;
			p->dwPluginId2 = 0;
			p->pPluginsList = NULL;
			lstrcpyn(p->szDllPath, pszDllPath, CountOf(p->szDllPath));
			_splitpath(pszDllPath, NULL, NULL, p->szLibraryName, NULL);
			p->szLibraryName[63] = 0;
			p->pNext = m_pVstHead;
			p->pPrev = NULL;
			if (m_pVstHead) m_pVstHead->pPrev = p;
			m_pVstHead = p;
			try
			{
				const CMachineInfo *pInfo = pBuzzGetInfo();
				if (pInfo)
				{
					p->dwPluginId2 = pInfo->Version;
					if (pInfo->Name)
					{
						lstrcpyn(p->szLibraryName, pInfo->Name, CountOf(p->szLibraryName));
					}
					bOk = TRUE;
				}
			} catch (...)
			{
				CVstPluginManager::ReportPlugException("Exception while trying to load buzz plugin \"%s\"!\n", p->szLibraryName);
			}
		} else
	#endif // ENABLE_BUZZ
		{
		#ifdef VST_LOG
			Log("Entry point not found!\n");
		#endif // VST_LOG
		}

		FreeLibrary(hLib);

		// Now it should be safe to assume that this plugin loaded properly. :)
		WritePrivateProfileString("VST Plugins", "FailedPlugin", NULL, theApp.GetConfigFileName());

		return (bOk) ? m_pVstHead : nullptr;
	} else
	{
	#ifdef VST_LOG
		Log("LoadLibrary(%s) failed!\n", pszDllPath);
	#endif // VST_LOG

		WritePrivateProfileString("VST Plugins", "FailedPlugin", NULL, theApp.GetConfigFileName());
	}
	return nullptr;
}


bool CVstPluginManager::RemovePlugin(PVSTPLUGINLIB pFactory)
//----------------------------------------------------------
{
	PVSTPLUGINLIB p = m_pVstHead;

	while (p)
	{
		if (p == pFactory)
		{
			if (p == m_pVstHead) m_pVstHead = p->pNext;
			if (p->pPrev) p->pPrev->pNext = p->pNext;
			if (p->pNext) p->pNext->pPrev = p->pPrev;
			p->pPrev = p->pNext = NULL;

			try
			{
				CriticalSection cs;

				while ((volatile void *)(p->pPluginsList) != NULL)
				{
					p->pPluginsList->Release();
				}
				delete p;
			} catch (...)
			{
				CVstPluginManager::ReportPlugException("Exception while trying to release plugin \"%s\"!\n", pFactory->szLibraryName);
			}

			return true;
		}
		p = p->pNext;
	}
	return false;
}


BOOL CVstPluginManager::CreateMixPlugin(PSNDMIXPLUGIN pMixPlugin, CSoundFile* pSndFile)
//-------------------------------------------------------------------------------------
{
	UINT nMatch = 0;
	PVSTPLUGINLIB pFound = NULL;

	if (pMixPlugin)
	{
		PVSTPLUGINLIB p = m_pVstHead;

		while (p)
		{
			if (pMixPlugin)
			{
				bool b1 = false, b2 = false;
				if ((p->dwPluginId1 == pMixPlugin->Info.dwPluginId1)
				 && (p->dwPluginId2 == pMixPlugin->Info.dwPluginId2))
				{
					b1 = true;
				}
				if (!_strnicmp(p->szLibraryName, pMixPlugin->Info.szLibraryName, 64))
				{
					b2 = true;
				}
				if ((b1) && (b2))
				{
					nMatch = 3;
					pFound = p;
				} else
				if ((b1) && (nMatch < 2))
				{
					nMatch = 2;
					pFound = p;
				} else
				if ((b2) && (nMatch < 1))
				{
					nMatch = 1;
					pFound = p;
				}
			}
			p = p->pNext;
		}
	}
	if (pMixPlugin->Info.dwPluginId1 == kDmoMagic)
	{
		if (!pFound) return FALSE;
		AEffect *pEffect = DmoToVst(pFound);
		if ((pEffect) && (pEffect->dispatcher) && (pEffect->magic == kDmoMagic))
		{
			BOOL bOk = FALSE;

			CriticalSection cs;

			CVstPlugin *pVstPlug = new CVstPlugin(NULL, pFound, pMixPlugin, pEffect);
			if (pVstPlug)
			{
				pVstPlug->Initialize(pSndFile);
				bOk = TRUE;
			}
			return bOk;
		}
	}
	if ((!pFound) && (pMixPlugin->Info.szLibraryName[0]))
	{
		CHAR s[_MAX_PATH], dir[_MAX_PATH];
		_splitpath(theApp.GetConfigFileName(), s, dir, NULL, NULL);
		strcat(s, dir);
		int len = strlen(s);
		if ((len > 0) && (s[len-1] != '\\')) strcat(s, "\\");
		strncat(s, "plugins\\", _MAX_PATH-1);
		strncat(s, pMixPlugin->Info.szLibraryName, _MAX_PATH-1);
		strncat(s, ".dll", _MAX_PATH-1);
		pFound = AddPlugin(s);
		if (!pFound)
		{
			CString cacheSection = "PluginCache";
			CString cacheFile = theApp.GetPluginCacheFileName();
			CString IDs = CMainFrame::GetPrivateProfileCString(cacheSection, pMixPlugin->Info.szLibraryName, "", cacheFile);
			if (IDs.GetLength() >= 16)
			{
				CString strFullPath = CMainFrame::GetPrivateProfileCString(cacheSection, IDs, "", cacheFile);
				if ((strFullPath) && (strFullPath[0])) pFound = AddPlugin(strFullPath);
			}
		}
	}
	if (pFound)
	{
		HINSTANCE hLibrary = NULL;
		BOOL bOk = FALSE;

		try
		{
			CriticalSection cs;

			hLibrary = LoadLibrary(pFound->szDllPath);
			if (hLibrary != NULL)
			{
				AEffect *pEffect = NULL;
				PVSTPLUGENTRY pEntryPoint = (PVSTPLUGENTRY)GetProcAddress(hLibrary, "main");
				if(pEntryPoint == NULL)
				{
					pEntryPoint = (PVSTPLUGENTRY)GetProcAddress(hLibrary, "VSTPluginMain");
				}
			#ifdef ENABLE_BUZZ
				GET_INFO pBuzzGetInfo = (GET_INFO)GetProcAddress(hLibrary, "GetInfo");
				CREATE_MACHINE pBuzzCreateMachine = (CREATE_MACHINE)GetProcAddress(hLibrary, "CreateMachine");
			#endif
				if (pEntryPoint)
				{
					pEffect = pEntryPoint(MasterCallBack);
				} else
			#ifdef ENABLE_BUZZ
				if ((pBuzzCreateMachine) && (pBuzzGetInfo))
				{
					CMachineInterface *pBuzzMachine = pBuzzCreateMachine();
					const CMachineInfo *pBuzzInfo = pBuzzGetInfo();
					if ((pBuzzMachine) && (pBuzzInfo) && (pBuzzInfo->Type == MT_EFFECT))
					{
						pEffect = Buzz2Vst(pBuzzMachine, pBuzzInfo);
					}
				} else
			#endif
				{
				#ifdef VST_LOG
					Log("Entry point not found! (handle=%08X)\n", hLibrary);
				#endif
				}
				if ((pEffect) && (pEffect->dispatcher)
					&& ((pEffect->magic == kEffectMagic) || (pEffect->magic == kBuzzMagic)))
				{
					bOk = TRUE;
					if ((pEffect->flags & effFlagsIsSynth) || (!pEffect->numInputs))
					{
						if (!pFound->bIsInstrument)
						{
							CString cacheSection = "PluginCache";
							CString cacheFile = theApp.GetPluginCacheFileName();
							//LPCSTR pszSection = "PluginCache";
							pFound->bIsInstrument = TRUE;
							CString flagsKey;
							flagsKey.Format("%08X%08X.Flags", pFound->dwPluginId1, pFound->dwPluginId2);
							CMainFrame::WritePrivateProfileLong(cacheSection, flagsKey, 1, cacheFile);
						}
					}
					CVstPlugin *pVstPlug = new CVstPlugin(hLibrary, pFound, pMixPlugin, pEffect);
					if (pVstPlug) pVstPlug->Initialize(pSndFile);
				}
			} else
			{
			#ifdef VST_LOG
				Log("LoadLibrary(\"%s\") failed!\n", pFound->szDllPath);
			#endif
			}
			if ((!bOk) && (hLibrary)) FreeLibrary(hLibrary);
		} catch(...)
		{
			CVstPluginManager::ReportPlugException("Exception while trying to create plugin \"%s\"!\n", pFound->szLibraryName);
		}

		return bOk;
	} else
	{
		// "plug not found" notification code MOVED to CSoundFile::Create
		#ifdef VST_LOG
			Log("Unknown plugin\n");
		#endif
	}
	return FALSE;
}


void CVstPluginManager::OnIdle()
//------------------------------
{
	PVSTPLUGINLIB pFactory = m_pVstHead;

	while (pFactory)
	{
		CVstPlugin *p = pFactory->pPluginsList;
		while (p)
		{
			//rewbs. VSTCompliance: A specific plug has requested indefinite periodic processing time.
			if (p->m_bNeedIdle)
			{
				if (!(p->Dispatch(effIdle, 0,0, NULL, 0)))
					p->m_bNeedIdle=false;
			}
			//We need to update all open editors
			if ((p->m_pEditor) && (p->m_pEditor->m_hWnd))
			{
				p->m_pEditor->UpdateParamDisplays();
			}
			//end rewbs. VSTCompliance:

			p = p->m_pNext;
		}
		pFactory = pFactory->pNext;
	}

}

//rewbs.VSTCompliance: Added support for lots of opcodes
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
		pVstPlugin = reinterpret_cast<CVstPlugin *>(effect->resvd1);
	}

	switch(opcode)
	{
	// Called when plugin param is changed via gui
	case audioMasterAutomate:
		if (pVstPlugin != nullptr && pVstPlugin->Dispatch(effCanBeAutomated, index, nullptr, nullptr, 0.0f) != 0)
		{
			// This parameter can be automated. Ugo Motion constantly sends automation callback events for parameters that cannot be automated...
			pVstPlugin->AutomateParameter((PlugParamIndex)index);
		}
		return 0;

	// Called when plugin asks for VST version supported by host
	case audioMasterVersion:
		return kVstVersion;

	// Returns the unique id of a plug that's currently loading
	// (not sure what this is actually for - we return *effect's UID, cos Herman Seib does something similar :)
	//  Let's see what happens...)
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
		//if (effect)
		//{
		//	OnIdle();
		//	if (value)
		//		return ((index < effect->numOutputs) && (index < 2)) ? 0 : 1;
		//	else
		//		return ((index < effect->numInputs) && (index < 2)) ? 0 : 1;
		//}
		//Olivier seemed to do some superfluous stuff (above). Time will tell if it was necessary.
		if (value)	//input:
			return (index < 2) ? 0 : 1;		//we only support up to 2 inputs. Remember: 0 means yes.
		else		//output:
			return (index < 2) ? 0 : 1;		//2 outputs max too

	//---from here VST 2.0 extension opcodes------------------------------------------------------

	// <value> is a filter which is currently ignored - DEPRECATED in VST 2.4
	// Herman Seib only processes Midi events for plugs that call this. Keep in mind.
	case audioMasterWantMidi:
		return 1;

	// returns const VstTimeInfo* (or 0 if not supported)
	// <value> should contain a mask indicating which fields are required
	case audioMasterGetTime:
	{

		MemsetZero(timeInfo);
		timeInfo.sampleRate = CMainFrame::GetMainFrame()->GetSampleRate();

		if (pVstPlugin)
		{
			CSoundFile* pSndFile = pVstPlugin->GetSoundFile();

			if (pVstPlugin->IsSongPlaying())
			{
				timeInfo.flags |= kVstTransportPlaying;
				timeInfo.samplePos = CMainFrame::GetMainFrame()->GetTotalSampleCount();
				if (timeInfo.samplePos == 0) //samplePos=0 means we just started playing
				{
					timeInfo.flags |= kVstTransportChanged;
				}
			} else
			{
				timeInfo.flags |= kVstTransportChanged; //just stopped.
				timeInfo.samplePos = 0;
			}
			if ((value & kVstNanosValid))
			{
				timeInfo.flags |= kVstNanosValid;
				timeInfo.nanoSeconds = timeGetTime() * 1000000;
			}
			if ((value & kVstPpqPosValid) && pSndFile)
			{
				timeInfo.flags |= kVstPpqPosValid;
				if (timeInfo.flags & kVstTransportPlaying)
				{
					timeInfo.ppqPos = (timeInfo.samplePos / timeInfo.sampleRate) * (pSndFile->GetCurrentBPM() / 60.0);
				} else
				{
					timeInfo.ppqPos = 0;
				}
			}
			if ((value & kVstTempoValid) && pSndFile)
			{
				timeInfo.tempo = pSndFile->GetCurrentBPM();
				if (timeInfo.tempo)
				{
					timeInfo.flags |= kVstTempoValid;
				}
			}
			if ((value & kVstTimeSigValid) && pSndFile)
			{
				timeInfo.flags |= kVstTimeSigValid;

				// Time signature. numerator = rows per beats / rows pear measure (should sound somewhat logical to you).
				// the denominator is a bit more tricky, since it cannot be set explicitely. so we just assume quarters for now.
				timeInfo.timeSigNumerator = pSndFile->m_nCurrentRowsPerMeasure / max(pSndFile->m_nCurrentRowsPerBeat, 1);
				timeInfo.timeSigDenominator = 4; //gcd(pSndFile->m_nCurrentRowsPerMeasure, pSndFile->m_nCurrentRowsPerBeat);
			}
		}
		return ToVstPtr(&timeInfo);
	}

	// VstEvents* in <ptr>
	// We don't support plugs that send VSTEvents to the host
	case audioMasterProcessEvents:
		Log("VST plugin to host: Process Events\n");
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
			CSoundFile *pSndFile = pVstPlugin->GetSoundFile();
			if (pSndFile)
			{
				return (VstInt32)(pSndFile->GetCurrentBPM() * 10000);
			}
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
		if (pVstPlugin != nullptr)
		{
			CriticalSection cs;
			return pVstPlugin->InitializeIOBuffers() ? 1 : 0;
		}
		break;
	
	// plug needs idle calls (outside its editor window) - DEPRECATED in VST 2.4
	case audioMasterNeedIdle:
		if (pVstPlugin != nullptr)
		{
			pVstPlugin->m_bNeedIdle = true;
		}

		return 1;
	
	// index: width, value: height
	case audioMasterSizeWindow:
		if (pVstPlugin != nullptr)
		{
			CAbstractVstEditor *pVstEditor = pVstPlugin->GetEditor();
			if (pVstEditor && pVstEditor->IsResizable())
			{
				pVstEditor->SetSize(index, value);
			}
		}
		Log("VST plugin to host: Size Window\n");
		return 1;
	
	case audioMasterGetSampleRate:
		return CMainFrame::GetMainFrame()->GetSampleRate();
	
	case audioMasterGetBlockSize:
		return MIXBUFFERSIZE;
	
	case audioMasterGetInputLatency:
		Log("VST plugin to host: Get Input Latency\n");
		break;
	
	case audioMasterGetOutputLatency:
		{
			VstIntPtr latency = CMainFrame::GetSettings().m_nBufferLength * (CMainFrame::GetMainFrame()->GetSampleRate() / 1000L);
			return latency;
		}
	
	// input pin in <value> (-1: first to come), returns cEffect* - DEPRECATED in VST 2.4
	case audioMasterGetPreviousPlug:
		Log("VST plugin to host: Get Previous Plug\n");
		break;
	
	// output pin in <value> (-1: first to come), returns cEffect* - DEPRECATED in VST 2.4
	case audioMasterGetNextPlug:
		Log("VST plugin to host: Get Next Plug\n");
		break;
	
	// realtime info
	// returns: 0: not supported, 1: replace, 2: accumulate - DEPRECATED in VST 2.4 (replace is default)
	case audioMasterWillReplaceOrAccumulate:
		return 1; //we replace.
	
	case audioMasterGetCurrentProcessLevel:
		//Log("VST plugin to host: Get Current Process Level\n");
		//TODO: Support offline processing
/*		if (CMainFrame::GetMainFrame()->IsRendering()) {
			return 4;   //Offline
		} else {
			return 2;   //Unknown.
		}
*/
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
		//strcpy((char*)ptr,"Steinberg");
		//return 0;
		return 1;

	case audioMasterGetProductString:
		strcpy((char *) ptr, s_szHostProductString);
		//strcpy((char*)ptr,"Cubase VST");
		//return 0;
		return 1;

	case audioMasterGetVendorVersion:
		return s_nHostVendorVersion;
		//return 7000;

	case audioMasterVendorSpecific:
		return 0;
	
	// void* in <ptr>, format not defined yet - DEPRECATED in VST 2.4
	case audioMasterSetIcon:
		Log("VST plugin to host: Set Icon\n");
		break;
	
	// string in ptr, see below
	case audioMasterCanDo:
		//Other possible Can Do strings are:
		//"receiveVstEvents",
		//"receiveVstMidiEvent",
		//"receiveVstTimeInfo",
		//"asyncProcessing",
		//"offline",
		//"supportShell"
		//"shellCategory"
		//"editFile"
		//"startStopProcess"
		//"sendVstMidiEventFlagIsRealtime"

		if ((strcmp((char*)ptr,"sendVstEvents") == 0 ||
				strcmp((char*)ptr,"sendVstMidiEvent") == 0 ||
				strcmp((char*)ptr,"sendVstTimeInfo") == 0 ||
				strcmp((char*)ptr,"supplyIdle") == 0 ||
				strcmp((char*)ptr,"sizeWindow") == 0 ||
				strcmp((char*)ptr,"openFileSelector") == 0 ||
				strcmp((char*)ptr,"closeFileSelector") == 0 ||
				strcmp((char*)ptr,"acceptIOChanges") == 0 ||
				strcmp((char*)ptr,"reportConnectionChanges") == 0
			))
			return HostCanDo;
		else
			return HostCanNotDo;

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
		return ToVstPtr(CMainFrame::GetSettings().GetDefaultDirectory(DIR_PLUGINS));
	
	// something has changed, update 'multi-fx' display
	case audioMasterUpdateDisplay:
		if (pVstPlugin != nullptr)
		{
			// Note to self for testing: Electri-Q sends opcode.
			CAbstractVstEditor *pVstEditor = pVstPlugin->GetEditor();
			if (pVstEditor && ::IsWindow(pVstEditor->m_hWnd))
			{
				pVstEditor->SetupMenu();
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
		return VstFileSelector(opcode == audioMasterCloseFileSelector, (VstFileSelect *)ptr, effect);

	// open an editor for audio (defined by XML text in ptr) - DEPRECATED in VST 2.4
	case audioMasterEditFile:
		Log("VST plugin to host: Edit File\n");
		break;
	
	// get the native path of currently loading bank or project
	// (called from writeChunk) void* in <ptr> (char[2048], or sizeof(FSSpec)) - DEPRECATED in VST 2.4
	case audioMasterGetChunkFile:
		Log("VST plugin to host: Get Chunk File\n");
		break;

	//---from here VST 2.3 extension opcodes------------------------------------------------------

	// result a VstSpeakerArrangement in ret - DEPRECATED in VST 2.4
	case audioMasterGetInputSpeakerArrangement:
		Log("VST plugin to host: Get Input Speaker Arrangement\n");
		break;

	//---from here VST 2.4 extension opcodes------------------------------------------------------

	// Floating point processing precision
	case effSetProcessPrecision:
		return (value == kVstProcessPrecision32 ? 1 : 0);

	}

	// Unknown codes:

	return 0;
}
//end rewbs. VSTCompliance:


// Helper function for file selection dialog stuff.
VstIntPtr CVstPluginManager::VstFileSelector(const bool destructor, VstFileSelect *pFileSel, const AEffect *effect)
//-----------------------------------------------------------------------------------------------------------------
{
	if(pFileSel == nullptr)
	{
		return 0;
	}

	if(!destructor)
	{
		pFileSel->nbReturnPath = 0;
		pFileSel->reserved = 0;

		if(pFileSel->command != kVstDirectorySelect)
		{
			// Plugin wants to load or save a file.
			std::string extensions, workingDir;
			for(VstInt32 i = 0; i < pFileSel->nbFileTypes; i++)
			{
				VstFileType *pType = &(pFileSel->fileTypes[i]);
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

			if(pFileSel->initialPath != nullptr)
			{
				workingDir = pFileSel->initialPath;
			} else
			{
				// Plugins are probably looking for presets...?
				workingDir = ""; //CMainFrame::GetSettings().GetWorkingDirectory(DIR_PLUGINPRESETS);
			}

			FileDlgResult files = CTrackApp::ShowOpenSaveFileDialog(
				(pFileSel->command != kVstFileSave),
				"", "", extensions, workingDir,
				(pFileSel->command == kVstMultipleFilesLoad)
				);

			if(files.abort)
			{
				return 0;
			}

			if(pFileSel->command == kVstMultipleFilesLoad)
			{
				// Multiple paths
				pFileSel->nbReturnPath = files.filenames.size();
				pFileSel->returnMultiplePaths = new char *[pFileSel->nbReturnPath];
				for(size_t i = 0; i < files.filenames.size(); i++)
				{
					char *fname = new char[files.filenames[i].length() + 1];
					strcpy(fname, files.filenames[i].c_str());
					pFileSel->returnMultiplePaths[i] = fname;
				}
				return 1;
			} else
			{
				// Single path

				// VOPM doesn't initialize required information properly (it doesn't memset the struct to 0)...
				if(CCONST('V', 'O', 'P', 'M') == effect->uniqueID)
				{
					pFileSel->sizeReturnPath = _MAX_PATH;
				}

				if(pFileSel->returnPath == nullptr || pFileSel->sizeReturnPath == 0)
				{

					// Provide some memory for the return path.
					pFileSel->sizeReturnPath = files.first_file.length() + 1;
					pFileSel->returnPath = new char[pFileSel->sizeReturnPath];
					if(pFileSel->returnPath == nullptr)
					{
						return 0;
					}
					pFileSel->returnPath[pFileSel->sizeReturnPath - 1] = '\0';
					pFileSel->reserved = 1;
				} else
				{
					pFileSel->reserved = 0;
				}
				strncpy(pFileSel->returnPath, files.first_file.c_str(), pFileSel->sizeReturnPath - 1);
				pFileSel->nbReturnPath = 1;
				pFileSel->returnMultiplePaths = nullptr;
			}
			return 1;

		} else
		{
			// Plugin wants a directory

			char szInitPath[_MAX_PATH];
			MemsetZero(szInitPath);
			if(pFileSel->initialPath)
			{
				strncpy(szInitPath, pFileSel->initialPath, _MAX_PATH - 1);
			}

			char szBuffer[_MAX_PATH];
			MemsetZero(szBuffer);

			BROWSEINFO bi;
			MemsetZero(bi);
			bi.hwndOwner = CMainFrame::GetMainFrame()->m_hWnd;
			bi.lpszTitle = pFileSel->title;
			bi.pszDisplayName = szInitPath;
			bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
			LPITEMIDLIST pid = SHBrowseForFolder(&bi);
			if(pid != NULL && SHGetPathFromIDList(pid, szBuffer))
			{
				if(CCONST('V', 'S', 'T', 'r') == effect->uniqueID && pFileSel->returnPath != nullptr && pFileSel->sizeReturnPath == 0)
				{
					// old versions of reViSiT (which still relied on the host's file selection code) seem to be dodgy.
					// They report a path size of 0, but when using an own buffer, they will crash.
					// So we'll just assume that reViSiT can handle long enough (_MAX_PATH) paths here.
					pFileSel->sizeReturnPath = strlen(szBuffer) + 1;
					pFileSel->returnPath[pFileSel->sizeReturnPath - 1] = '\0';
				}
				if(pFileSel->returnPath == nullptr || pFileSel->sizeReturnPath == 0)
				{
					// Provide some memory for the return path.
					pFileSel->sizeReturnPath = strlen(szBuffer) + 1;
					pFileSel->returnPath = new char[pFileSel->sizeReturnPath];
					if(pFileSel->returnPath == nullptr)
					{
						return 0;
					}
					pFileSel->returnPath[pFileSel->sizeReturnPath - 1] = '\0';
					pFileSel->reserved = 1;
				} else
				{
					pFileSel->reserved = 0;
				}
				strncpy(pFileSel->returnPath, szBuffer, pFileSel->sizeReturnPath - 1);
				pFileSel->nbReturnPath = 1;
				return 1;
			} else
			{
				return 0;
			}
		}
	} else
	{
		// Close file selector - delete allocated strings.
		if(pFileSel->command == kVstMultipleFilesLoad && pFileSel->returnMultiplePaths != nullptr)
		{
			for(VstInt32 i = 0; i < pFileSel->nbReturnPath; i++)
			{
				if(pFileSel->returnMultiplePaths[i] != nullptr)
				{
					delete[] pFileSel->returnMultiplePaths[i];
				}
			}
			delete[] pFileSel->returnMultiplePaths;
		} else
		{
			if(pFileSel->reserved == 1 && pFileSel->returnPath != nullptr)
			{
				delete[] pFileSel->returnPath;
				pFileSel->returnPath = nullptr;
			}
		}
		return 1;
	}
}


void CVstPluginManager::ReportPlugException(LPCSTR format,...)
//------------------------------------------------------------
{
	CHAR cBuf[1024];
	va_list va;
	va_start(va, format);
	wvsprintf(cBuf, format, va);
	Reporting::Notification(cBuf);
#ifdef VST_LOG
	Log(cBuf);
#endif

	//Stop song - TODO: figure out why this causes a kernel hang from IASIO->release();
/*	END_CRITICAL();
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
		if (pMainFrm) pMainFrm->StopMod();
*/
	va_end(va);
}


//////////////////////////////////////////////////////////////////////////////
//
// CVstPlugin
//

CVstPlugin::CVstPlugin(HMODULE hLibrary, PVSTPLUGINLIB pFactory, PSNDMIXPLUGIN pMixStruct, AEffect *pEffect)
//----------------------------------------------------------------------------------------------------------
{
	m_hLibrary = hLibrary;
	m_nRefCount = 1;
	m_pPrev = nullptr;
	m_pNext = nullptr;
	m_pFactory = pFactory;
	m_pMixStruct = pMixStruct;
	m_pEffect = pEffect;
	m_pEditor = nullptr;
	m_nInputs = m_nOutputs = 0;
	m_nEditorX = m_nEditorY = -1;
	m_pEvList = nullptr;
	m_pModDoc = nullptr; //rewbs.plugDocAware
	m_nPreviousMidiChan = nInvalidMidiChan; //rewbs.VSTCompliance
	m_pProcessFP = nullptr;

	// Insert ourselves in the beginning of the list
	if (m_pFactory)
	{
		m_pNext = m_pFactory->pPluginsList;
		if (m_pFactory->pPluginsList)
		{
			m_pFactory->pPluginsList->m_pPrev = this;
		}
		m_pFactory->pPluginsList = this;
	}
	m_MixState.dwFlags = 0;
	m_MixState.nVolDecayL = 0;
	m_MixState.nVolDecayR = 0;
	m_MixState.pMixBuffer = (int *)((((DWORD)m_MixBuffer)+7)&~7);
	m_MixState.pOutBufferL = mixBuffer.GetInputBuffer(0);
	m_MixState.pOutBufferR = mixBuffer.GetInputBuffer(1);

	m_bSongPlaying = false; //rewbs.VSTCompliance
	m_bPlugResumed = false;
	m_bModified = false;
	m_dwTimeAtStartOfProcess=0;
	m_nSampleRate = nInvalidSampleRate; //rewbs.VSTCompliance: gets set on Resume()
	MemsetZero(m_MidiCh);

	for (int ch=0; ch<16; ch++)
	{
		m_nMidiPitchBendPos[ch]=MIDI_PitchBend_Centre; //centre pitch bend on all channels
	}

	processCalled = CreateEvent(NULL,FALSE,FALSE,NULL);
}


void CVstPlugin::Initialize(CSoundFile* pSndFile)
//-----------------------------------------------
{
	if (!m_pEvList)
	{
		m_pEvList = (VstEvents *)new char[sizeof(VstEvents)+sizeof(VstEvent*)*VSTEVENT_QUEUE_LEN];
	}
	if (m_pEvList)
	{
		m_pEvList->numEvents = 0;
		m_pEvList->reserved = 0;
	}
	m_bNeedIdle=false;
	m_bRecordAutomation=false;
	m_bPassKeypressesToPlug=false;

	MemsetZero(m_MidiCh);

	//rewbs.VSTcompliance
	//Store a pointer so we can get the CVstPlugin object from the basic VST effect object.
    m_pEffect->resvd1 = ToVstPtr(this);
	//rewbs.plugDocAware
	m_pSndFile = pSndFile;
	m_pModDoc = pSndFile->GetpModDoc();
	m_nSlot = FindSlot();
	//end rewbs.plugDocAware

	Dispatch(effOpen, 0, 0, NULL, 0);
	// VST 2.0 plugins return 2 here, VST 2.4 plugins return 2400... Great!
	m_bIsVst2 = (CVstPlugin::Dispatch(effGetVstVersion, 0,0, NULL, 0) >= 2);
    if (m_bIsVst2)
	{
		// Set VST speaker in/out setup to Stereo. Required for some plugins (possibly all VST 2.4+ plugins?)
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

		Dispatch(effConnectInput, 0, 1, NULL, 0);
		if (m_pEffect->numInputs > 1) Dispatch(effConnectInput, 1, 1, NULL, 0.0f);
		Dispatch(effConnectOutput, 0, 1, NULL, 0);
		if (m_pEffect->numOutputs > 1) Dispatch(effConnectOutput, 1, 1, NULL, 0.0f);
		//rewbs.VSTCompliance: disable all inputs and outputs beyond stereo left and right:
		for (int i=2; i<m_pEffect->numInputs; i++)
			Dispatch(effConnectInput, i, 0, NULL, 0.0f);
		for (int i=2; i<m_pEffect->numOutputs; i++)
			Dispatch(effConnectOutput, i, 0, NULL, 0.0f);
		//end rewbs.VSTCompliance

	}

	m_nSampleRate=CSoundFile::gdwMixingFreq;
	Dispatch(effSetSampleRate, 0, 0, NULL, static_cast<float>(CSoundFile::gdwMixingFreq));
	Dispatch(effSetBlockSize, 0, MIXBUFFERSIZE, NULL, 0.0f);
	if (m_pEffect->numPrograms > 0)
	{
		Dispatch(effSetProgram, 0, 0, NULL, 0);
	}
	Dispatch(effMainsChanged, 0, 1, NULL, 0.0f);

	InitializeIOBuffers();

	Dispatch(effSetProcessPrecision, 0, kVstProcessPrecision32, nullptr, 0.0f);

#ifdef VST_LOG
	Log("%s: vst ver %d.0, flags=%04X, %d programs, %d parameters\n",
		m_pFactory->szLibraryName, (m_bIsVst2) ? 2 : 1, m_pEffect->flags,
		m_pEffect->numPrograms, m_pEffect->numParams);
#endif
	// Update Mix structure
	if (m_pMixStruct)
	{
		m_pMixStruct->pMixPlugin = this;
		m_pMixStruct->pMixState = &m_MixState;
	}

	//rewbs.VSTcompliance
	m_bIsInstrument = isInstrument();
	RecalculateGain();
	m_pProcessFP = (m_pEffect->flags & effFlagsCanReplacing) ?  m_pEffect->processReplacing : m_pEffect->process;

 // issue samplerate again here, cos some plugs like it before the block size, other like it right at the end.
	Dispatch(effSetSampleRate, 0, 0, NULL, static_cast<float>(CSoundFile::gdwMixingFreq));

}


bool CVstPlugin::InitializeIOBuffers()
//------------------------------------
{
	m_nInputs = m_pEffect->numInputs;
	m_nOutputs = m_pEffect->numOutputs;

	// Input pointer array size must be >= 2 for now - the input buffer assignment might write to non allocated mem. otherwise
	bool result = mixBuffer.Initialize(max(m_nInputs, 2), m_nOutputs);
	m_MixState.pOutBufferL = mixBuffer.GetInputBuffer(0);
	m_MixState.pOutBufferR = mixBuffer.GetInputBuffer(1);

	return result;
}


CVstPlugin::~CVstPlugin()
//-----------------------
{
#ifdef VST_LOG
	Log("~CVstPlugin: m_nRefCount=%d\n", m_nRefCount);
#endif
	// First thing to do, if we don't want to hang in a loop
	if ((m_pFactory) && (m_pFactory->pPluginsList == this)) m_pFactory->pPluginsList = m_pNext;
	if (m_pMixStruct)
	{
		m_pMixStruct->pMixPlugin = NULL;
		m_pMixStruct->pMixState = NULL;
		m_pMixStruct = NULL;
	}
	if (m_pNext) m_pNext->m_pPrev = m_pPrev;
	if (m_pPrev) m_pPrev->m_pNext = m_pNext;
	m_pPrev = NULL;
	m_pNext = NULL;
	if (m_pEditor)
	{
		if (m_pEditor->m_hWnd) m_pEditor->OnClose();
		if ((volatile void *)m_pEditor) delete m_pEditor;
		m_pEditor = NULL;
	}
	if (m_bIsVst2)
	{
		Dispatch(effConnectInput, 0, 0, NULL, 0);
		if (m_pEffect->numInputs > 1) Dispatch(effConnectInput, 1, 0, NULL, 0);
		Dispatch(effConnectOutput, 0, 0, NULL, 0);
		if (m_pEffect->numOutputs > 1) Dispatch(effConnectOutput, 1, 0, NULL, 0);
	}
	CVstPlugin::Dispatch(effMainsChanged, 0,0, NULL, 0);
	CVstPlugin::Dispatch(effClose, 0, 0, NULL, 0);
	m_pFactory = NULL;
	if (m_hLibrary)
	{
		FreeLibrary(m_hLibrary);
		m_hLibrary = NULL;
	}

	delete[] (char *)m_pEvList;
	m_pEvList = nullptr;

	CloseHandle(processCalled);
}


int CVstPlugin::Release()
//-----------------------
{
	if (!(--m_nRefCount))
	{
		try
		{
			delete this;
		} catch (...)
		{
			CVstPluginManager::ReportPlugException("Exception while destroying plugin!\n");
		}

		return 0;
	}
	return m_nRefCount;
}


void CVstPlugin::GetPluginType(LPSTR pszType)
//-------------------------------------------
{
	pszType[0] = 0;
	if (m_pEffect)
	{
		if (m_pEffect->numInputs < 1) strcpy(pszType, "No input"); else
		if (m_pEffect->numInputs == 1) strcpy(pszType, "Mono-In"); else
		strcpy(pszType, "Stereo-In");
		strcat(pszType, ", ");
		if (m_pEffect->numOutputs < 1) strcat(pszType, "No output"); else
		if (m_pEffect->numInputs == 1) strcat(pszType, "Mono-Out"); else
		strcat(pszType, "Stereo-Out");
	}
}


BOOL CVstPlugin::HasEditor()
//--------------------------
{
	if ((m_pEffect) && (m_pEffect->flags & effFlagsHasEditor))
	{
		return TRUE;
	}
	return FALSE;
}

//rewbs.VSTcompliance: changed from BOOL to long
long CVstPlugin::GetNumPrograms()
//-------------------------------
{
	if ((m_pEffect) && (m_pEffect->numPrograms > 0))
	{
		return m_pEffect->numPrograms;
	}
	return 0;
}


PlugParamIndex CVstPlugin::GetNumParameters()
//-------------------------------------------
{
	if ((m_pEffect) && (m_pEffect->numParams > 0))
	{
		return m_pEffect->numParams;
	}
	return 0;
}


VstInt32 CVstPlugin::GetUID()
//---------------------------
{
	if (!(m_pEffect))
		return 0;
	return m_pEffect->uniqueID;
}


VstInt32 CVstPlugin::GetVersion()
//-------------------------------
{
	if (!(m_pEffect))
		return 0;

	return m_pEffect->version;
}


bool CVstPlugin::GetParams(float *param, VstInt32 min, VstInt32 max)
//------------------------------------------------------------------
{
	if (!(m_pEffect))
		return false;

	LimitMax(max, m_pEffect->numParams);

	for (VstInt32 p = min; p < max; p++)
		param[p - min]=GetParameter(p);

	return true;

}


bool CVstPlugin::RandomizeParams(VstInt32 minParam, VstInt32 maxParam)
//--------------------------------------------------------------------
{
	if (!(m_pEffect))
		return false;

	if (minParam == 0 && maxParam == 0)
	{
		maxParam = m_pEffect->numParams;

	}
	LimitMax(maxParam, m_pEffect->numParams);

	for (VstInt32 p = minParam; p < maxParam; p++)
		SetParameter(p, (rand() / float(RAND_MAX)));

	return true;
}


bool CVstPlugin::SaveProgram(CString fileName)
//--------------------------------------------
{
	if (!(m_pEffect))
		return false;

	bool success;
	//Collect required data
	long ID = GetUID();
	long plugVersion = GetVersion();

	Cfxp* fxp = nullptr;

	//Construct & save fxp

	// try chunk-based preset:
	if(m_pEffect->flags & effFlagsProgramChunks)
	{
		void *chunk = NULL;
		long chunkSize = Dispatch(effGetChunk, 1,0, &chunk, 0);
		if (chunkSize && chunk)
			fxp = new Cfxp(ID, plugVersion, 1, chunkSize, chunk);
	}
	// fall back on parameter based preset:
	if (fxp == nullptr)
	{
		//Collect required data
		PlugParamIndex numParams = GetNumParameters();
		float *params = new float[numParams];
		GetParams(params, 0, numParams);

		fxp = new Cfxp(ID, plugVersion, numParams, params);

		delete[] params;
	}

	success = fxp->Save(fileName);
	if (fxp)
		delete fxp;

	return success;

}


bool CVstPlugin::LoadProgram(CString fileName)
//--------------------------------------------
{
	if (!(m_pEffect))
		return false;

	Cfxp fxp(fileName);	//load from file

	//Verify
	if (m_pEffect->uniqueID != fxp.fxID)
		return false;

	if (fxp.fxMagic == fMagic) //Load preset based fxp
	{
		if (m_pEffect->numParams != fxp.numParams)
			return false;
		for (int p=0; p<fxp.numParams; p++)
			SetParameter(p, fxp.params[p]);
	}
	else if (fxp.fxMagic == chunkPresetMagic)
	{
		Dispatch(effSetChunk, 1, fxp.chunkSize, (BYTE*)fxp.chunk, 0);
	}

	return true;
}
//end rewbs.VSTpresets


VstIntPtr CVstPlugin::Dispatch(VstInt32 opCode, VstInt32 index, VstIntPtr value, void *ptr, float opt)
//----------------------------------------------------------------------------------------------------
{
	long lresult = 0;

	try
	{
		if ((m_pEffect) && (m_pEffect->dispatcher))
		{
			#ifdef VST_LOG
			Log("About to Dispatch(%d) (Plugin=\"%s\"), index: %d, value: %d, value: %h, value: %f!\n", opCode, m_pFactory->szLibraryName, index, value, ptr, opt);
			#endif
			lresult = m_pEffect->dispatcher(m_pEffect, opCode, index, value, ptr, opt);
		}
	} catch (...)
	{
		CVstPluginManager::ReportPlugException("Exception in Dispatch(%d) (Plugin=\"%s\")!\n", opCode, m_pFactory->szLibraryName);
	}

	return lresult;
}


long CVstPlugin::GetCurrentProgram()
//----------------------------------
{
	if ((m_pEffect) && (m_pEffect->numPrograms > 0))
	{
		return Dispatch(effGetProgram, 0, 0, NULL, 0);
	}
	return 0;
}


bool CVstPlugin::GetProgramNameIndexed(long index, long category, char *text)
//---------------------------------------------------------------------------
{
	if ((m_pEffect) && (m_pEffect->numPrograms > 0))
	{
		return (Dispatch(effGetProgramNameIndexed, index, category, text, 0) == 1);
	}
	return false;
}


CString CVstPlugin::GetFormattedProgramName(VstInt32 index, bool allowFallback)
//-----------------------------------------------------------------------------
{
	char rawname[256];	// kVstMaxProgNameLen is 24...
	if(!GetProgramNameIndexed(index, -1, rawname))
	{
		// Fallback: Try to get current program name.
		if(!allowFallback || Dispatch(effGetProgramName, 0, 0, rawname, 0) != 1)
		{
			strcpy(rawname, "");
		}
	}
	StringFixer::SetNullTerminator(rawname);

	// Let's start counting at 1 for the program name (as most MIDI hardware / software does)
	index++;

	CString formattedName;
	if(rawname[0] < ' ')
	{
		formattedName.Format("%02d - Program %d", index, index);
	}
	else
	{
		formattedName.Format("%02d - %s", index, rawname);
	}

	return formattedName;
}


void CVstPlugin::SetCurrentProgram(UINT nIndex)
//---------------------------------------------
{
	if ((m_pEffect) && (m_pEffect->numPrograms > 0))
	{
		if (nIndex < (UINT)m_pEffect->numPrograms)
		{
			Dispatch(effSetProgram, 0, nIndex, NULL, 0);
		}
	}
}


PlugParamValue CVstPlugin::GetParameter(PlugParamIndex nIndex)
//------------------------------------------------------------
{
	float fResult = 0;
	if ((m_pEffect) && (nIndex < m_pEffect->numParams) && (m_pEffect->getParameter))
	{
		try
		{
			fResult = m_pEffect->getParameter(m_pEffect, nIndex);
		} catch (...)
		{
			//CVstPluginManager::ReportPlugException("Exception in getParameter (Plugin=\"%s\")!\n", m_pFactory->szLibraryName);
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
		if ((m_pEffect) && (nIndex < m_pEffect->numParams) && (m_pEffect->setParameter))
		{
			if ((fValue >= 0.0f) && (fValue <= 1.0f))
				m_pEffect->setParameter(m_pEffect, nIndex, fValue);
		}
	} catch (...)
	{
		CVstPluginManager::ReportPlugException("Exception in SetParameter(%d, 0.%03d) (Plugin=%s)\n", nIndex, (int)(fValue*1000), m_pFactory->szLibraryName);
	}
}


// Helper function for retreiving parameter name / label / display
CString CVstPlugin::GetParamPropertyString(VstInt32 param, VstInt32 opcode)
//-------------------------------------------------------------------------
{
	CHAR s[max(kVstMaxParamStrLen, 64)]; // Increased to 64 bytes since 32 bytes doesn't seem to suffice for all plugs. Kind of ridiculous if you consider that kVstMaxParamStrLen = 8...
	s[0] = '\0';

	if(m_pEffect != nullptr && m_pEffect->numParams > 0 && param < m_pEffect->numParams)
	{
		Dispatch(opcode, param, 0, s, 0);
		StringFixer::SetNullTerminator(s);
	}
	return CString(s);
}


CString CVstPlugin::GetFormattedParamName(PlugParamIndex param)
//-------------------------------------------------------------
{
	const CString paramName = GetParamName(param);
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
	if (m_bIsVst2)
	{
		Dispatch(effGetEffectName, 0, 0, pszName, 0);
		return TRUE;
	}
	return FALSE;
}

void CVstPlugin::Init(unsigned long /*nFreq*/, int /*bReset*/)
//------------------------------------------------------------
{

}

void CVstPlugin::Resume()
//-----------------------
{
	const DWORD sampleRate = CSoundFile::gdwMixingFreq;

	try
	{
		//reset some stuff
		m_MixState.nVolDecayL = 0;
		m_MixState.nVolDecayR = 0;
		if(m_bPlugResumed)
		{
			Dispatch(effStopProcess, 0, 0, NULL, 0.0f);
			Dispatch(effMainsChanged, 0, 0, NULL, 0.0f);	// calls plugin's suspend
		}
		if (sampleRate != m_nSampleRate)
		{
			m_nSampleRate = sampleRate;
			Dispatch(effSetSampleRate, 0, 0, NULL, static_cast<float>(m_nSampleRate));
		}
		Dispatch(effSetBlockSize, 0, MIXBUFFERSIZE, NULL, 0);
		//start off some stuff
		Dispatch(effMainsChanged, 0, 1, NULL, 0.0f);	// calls plugin's resume
		Dispatch(effStartProcess, 0, 0, NULL, 0.0f);
		m_bPlugResumed = true;
	} catch (...)
	{
		CVstPluginManager::ReportPlugException("Exception in Resume() (Plugin=%s)\n", m_pFactory->szLibraryName);
	}


}

void CVstPlugin::Suspend()
//------------------------
{
	try
	{
		if(m_bPlugResumed)
		{
			Dispatch(effStopProcess, 0, 0, NULL, 0.0f);
			Dispatch(effMainsChanged, 0, 0, NULL, 0.0f); // calls plugin's suspend (theoretically, plugins should clean their buffers here, but oh well, the number of plugins which don't do this is surprisingly high.)
			m_bPlugResumed = false;
		}
	} catch (...)
	{
		CVstPluginManager::ReportPlugException("Exception in Suspend() (Plugin=%s)\n", m_pFactory->szLibraryName);
	}
}


void CVstPlugin::ProcessVSTEvents()
//---------------------------------
{
	// Process VST events
	if ((m_pEffect) && (m_pEffect->dispatcher) && (m_pEvList) && (m_pEvList->numEvents > 0))
	{
		try
		{
			m_pEffect->dispatcher(m_pEffect, effProcessEvents, 0, 0, m_pEvList, 0);
		} catch (...)
		{
			CVstPluginManager::ReportPlugException("Exception in ProcessVSTEvents() (Plugin=%s, pEventList:%p, numEvents:%d, MidicodeCh:%d, MidicodeEv:%d, MidicodeNote:%d, MidiCodeVel:%d)\n",
				m_pFactory->szLibraryName, m_pEvList, m_pEvList->numEvents,
				(((VstMidiEvent *)(m_pEvList->events[0]))->midiData[0])&0x0f,
				(((VstMidiEvent *)(m_pEvList->events[0]))->midiData[0])&0xf0,
				(((VstMidiEvent *)(m_pEvList->events[0]))->midiData[1])&0xff,
				(((VstMidiEvent *)(m_pEvList->events[0]))->midiData[2])&0xff);
		}
	}

}

void CVstPlugin::ClearVSTEvents()
//-------------------------------
{
	// Clear VST events
	if ((m_pEvList) && (m_pEvList->numEvents > 0))
	{
		m_pEvList->numEvents = 0;
	}
}

void CVstPlugin::RecalculateGain()
//--------------------------------
{
	float gain = 0.1f * (float)( m_pMixStruct ? (m_pMixStruct->Info.dwInputRouting>>16) & 0xff : 10 );
	if(gain < 0.1f) gain = 1.0f;

	if (m_bIsInstrument && m_pSndFile)
	{
		gain /= m_pSndFile->m_pConfig->getVSTiAttenuation();
		gain = static_cast<float>(gain * (m_pSndFile->m_nVSTiVolume / m_pSndFile->m_pConfig->getNormalVSTiVol()));
	}
	m_fGain = gain;
}


void CVstPlugin::SetDryRatio(UINT param)
//--------------------------------------
{
	param = min(param, 127);
	m_pMixStruct->fDryRatio = static_cast<float>(1.0-(static_cast<double>(param)/127.0));
}


void CVstPlugin::Process(float *pOutL, float *pOutR, size_t nSamples)
//-------------------------------------------------------------------
{
	ProcessVSTEvents();

	//If the plug is found & ok, continue
	if ((m_pEffect) && (m_pProcessFP) && (mixBuffer.GetInputBufferArray()) && (mixBuffer.GetOutputBufferArray()) && (m_pMixStruct))
	{

		//RecalculateGain();

		// Merge stereo input before sending to the plug if the plug can only handle one input.
		if (m_pEffect->numInputs == 1)
		{
			for (size_t i = 0; i < nSamples; i++)
			{
				m_MixState.pOutBufferL[i] = 0.5f * m_MixState.pOutBufferL[i] + 0.5f * m_MixState.pOutBufferR[i];
			}
		}

		float **outputBuffers = mixBuffer.GetOutputBufferArray();
		mixBuffer.ClearOutputBuffers(nSamples);

		m_dwTimeAtStartOfProcess = timeGetTime();
		// Do the VST processing magic
		try
		{
			ASSERT(nSamples <= MIXBUFFERSIZE);
			m_pProcessFP(m_pEffect, mixBuffer.GetInputBufferArray(), outputBuffers, nSamples);
		} catch (...)
		{
			Bypass();
			CString processMethod = (m_pEffect->flags & effFlagsCanReplacing) ? "processReplacing" : "process";
			CVstPluginManager::ReportPlugException("The plugin %s threw an exception in %s. It has automatically been set to \"Bypass\".", m_pMixStruct->Info.szName, processMethod);
			ClearVSTEvents();
//			SetEvent(processCalled);
		}

		ASSERT(outputBuffers != nullptr);

		// Mix outputs of multi-output VSTs:
		if(m_nOutputs > 2)
		{
			// first, mix extra outputs on a stereo basis
			UINT nOuts = m_nOutputs;
			// so if nOuts is not even, let process the last output later
			if((nOuts % 2) == 1) nOuts--;

			// mix extra stereo outputs
			for(UINT iOut = 2; iOut < nOuts; iOut++)
			{
				for(size_t i = 0; i < nSamples; i++)
				{
					outputBuffers[iOut % 2][i] += outputBuffers[iOut][i]; // assumed stereo.
				}
			}

			// if m_nOutputs is odd, mix half the signal of last output to each channel
			if(nOuts != m_nOutputs)
			{
				// trick : if we are here, nOuts = m_nOutputs - 1 !!!
				for(size_t i = 0; i < nSamples; i++)
				{
					float v = 0.5f * outputBuffers[nOuts][i];
					outputBuffers[0][i] += v;
					outputBuffers[1][i] += v;
				}
			}
		}

		ProcessMixOps(pOutL, pOutR, nSamples);

		// If dry mix is ticked, we add the unprocessed buffer,
		// except if this is an instrument since this it has already been done:
		if((m_pMixStruct->Info.dwInputRouting & MIXPLUG_INPUTF_WETMIX) && !m_bIsInstrument)
		{
			for(size_t i = 0; i < nSamples; i++)
			{
				pOutL[i] += m_MixState.pOutBufferL[i];
				pOutR[i] += m_MixState.pOutBufferR[i];
			}
		}
	}

	ClearVSTEvents();
	//SetEvent(processCalled);
}


void CVstPlugin::ProcessMixOps(float *pOutL, float *pOutR, size_t nSamples)
//-------------------------------------------------------------------------
{
	float *leftPlugOutput;
	float *rightPlugOutput;

	if(m_pEffect->numOutputs == 1)
	{
		// If there was just the one plugin output we copy it into our 2 outputs
		leftPlugOutput = rightPlugOutput = mixBuffer.GetOutputBuffer(0);
	} else if(m_pEffect->numOutputs > 1)
	{
		// Otherwise we actually only cater for two outputs max (outputs > 2 have been mixed together already).
		leftPlugOutput = mixBuffer.GetOutputBuffer(0);
		rightPlugOutput = mixBuffer.GetOutputBuffer(1);
	} else
	{
		return;
	}

	// -> mixop == 0 : normal processing
	// -> mixop == 1 : MIX += DRY - WET * wetRatio
	// -> mixop == 2 : MIX += WET - DRY * dryRatio
	// -> mixop == 3 : MIX -= WET - DRY * wetRatio
	// -> mixop == 4 : MIX -= middle - WET * wetRatio + middle - DRY
	// -> mixop == 5 : MIX_L += wetRatio * (WET_L - DRY_L) + dryRatio * (DRY_R - WET_R)
	//				   MIX_R += dryRatio * (WET_L - DRY_L) + wetRatio * (DRY_R - WET_R)

	int mixop;
	if(m_bIsInstrument)
	{
		mixop = 0; // Force normal mix mode for instruments
	} else
	{
		mixop = m_pMixStruct ? (m_pMixStruct->Info.dwInputRouting >> 8) & 0xFF : 0;
	}

	float wetRatio = 1 - m_pMixStruct->fDryRatio;
	float dryRatio = m_bIsInstrument ? 1 : m_pMixStruct->fDryRatio; // Always mix full dry if this is an instrument

	// Wet / Dry range expansion [0,1] -> [-1,1]
	if(m_pEffect->numInputs > 0 && m_pMixStruct->Info.dwInputRouting & MIXPLUG_INPUTF_MIXEXPAND)
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
		if(m_pMixStruct->Info.dwInputRouting & MIXPLUG_INPUTF_MIXEXPAND)
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


bool CVstPlugin::MidiSend(DWORD dwMidiCode)
//-----------------------------------------
{
	if ((m_pEvList) && (m_pEvList->numEvents < VSTEVENT_QUEUE_LEN-1))
	{
		int insertPos;
		if ((dwMidiCode & 0xF0) == 0x80)
		{
			// noteoffs go at the start of the queue.
			if (m_pEvList->numEvents)
			{
				for (int i=m_pEvList->numEvents; i>=1; i--)
				{
					m_pEvList->events[i] = m_pEvList->events[i-1];
				}
			}
			insertPos=0;
		} else
		{
			insertPos=m_pEvList->numEvents;
		}

		VstMidiEvent *pev = &m_ev_queue[m_pEvList->numEvents];
		m_pEvList->events[insertPos] = (VstEvent *)pev;
		pev->type = kVstMidiType;
		pev->byteSize = 24;
		pev->deltaFrames = 0;
		pev->flags = 0;
		pev->noteLength = 0;
		pev->noteOffset = 0;
		pev->detune = 0;
		pev->noteOffVelocity = 0;
		pev->reserved1 = 0;
		pev->reserved2 = 0;
		*(DWORD *)pev->midiData = dwMidiCode;
		m_pEvList->numEvents++;
	#ifdef VST_LOG
		Log("Sending Midi %02X.%02X.%02X\n", pev->midiData[0]&0xff, pev->midiData[1]&0xff, pev->midiData[2]&0xff);
	#endif

		return true; //rewbs.instroVST
	}
	else
	{
		Log("VST Event queue overflow!\n");
		m_pEvList->numEvents = VSTEVENT_QUEUE_LEN-1;

		return false; //rewbs.instroVST
	}
}

//rewbs.VSTiNoteHoldonStopFix
void CVstPlugin::HardAllNotesOff()
//--------------------------------
{
	bool overflow;
	float in[2][SCRATCH_BUFFER_SIZE], out[2][SCRATCH_BUFFER_SIZE]; // scratch buffers

	// Relies on a wait on processCalled, which will never get set
	// if plug is bypassed.
	//if (IsBypassed())
	//	return;

	// The JUCE framework doesn't like processing while being suspended.
	const bool wasSuspended = !IsResumed();
	if(wasSuspended)
	{
		Resume();
	}

	do
	{
		overflow=false;
		for (int mc=0; mc<16; mc++)		//all midi chans
		{
			DWORD dwMidiCode = 0x80 | (mc & 0x0f); //command|channel|velocity
			PVSTINSTCH pCh = &m_MidiCh[mc];

			MidiPitchBend(mc, MIDI_PitchBend_Centre); // centre pitch bend
			MidiSend(0xB0|mc|(0x79<<8));			  // reset all controllers
			MidiSend(0xB0|mc|(0x7b<<8));			  // all notes off
			MidiSend(0xB0|mc|(0x78<<8));			  // all sounds off

			for (size_t i = 0; i < CountOf(pCh->uNoteOnMap); i++)	//all notes
			{
				for (CHANNELINDEX c = 0; c < CountOf(pCh->uNoteOnMap[i]); c++)
				{
					while (pCh->uNoteOnMap[i][c] && !overflow)
					{
						overflow=!(MidiSend(dwMidiCode | (i << 8)));
						pCh->uNoteOnMap[i][c]--;
					}
					if (overflow) break;	//yuck
				}
				if (overflow) break;	//yuck
			}
			if (overflow) break;	//yuck
		}
		// let plug process events
		// TODO: wait for notification from audio thread that process has been called,
		//       rather than re-call it here
		//ResetEvent(processCalled);				 // Unset processCalled.
		//WaitForSingleObject(processCalled, 10000); // Will not return until processCalled is set again,
		//                                           // i.e. until processReplacing() has been called.
		Process((float*)in, (float*)out, SCRATCH_BUFFER_SIZE);

	// If we had hit an overflow, we need to loop around and start again.
	} while (overflow);

	if(wasSuspended)
	{
		Suspend();
	}

}
//end rewbs.VSTiNoteHoldonStopFix

void CVstPlugin::MidiCC(UINT nMidiCh, UINT nController, UINT nParam, UINT /*trackChannel*/)
//------------------------------------------------------------------------------------------
{
	//Error checking
	LimitMax(nController, 127u);
	LimitMax(nParam, 127u);

	if(m_pSndFile && m_pSndFile->GetModFlag(MSF_MIDICC_BUGEMULATION))
		MidiSend((nController << 16) | (nParam << 8) | (0xB0 | nMidiCh));
	else
		MidiSend((nParam << 16) | (nController << 8) | (0xB0 | nMidiCh));
}

short CVstPlugin::getMIDI14bitValueFromShort(short value)
//-------------------------------------------------------
{
	//http://www.srm.com/qtma/davidsmidispec.html:
	// The two bytes of the pitch bend message form a 14 bit number, 0 to 16383.
	// The value 8192 (sent, LSB first, as 0x00 0x40), is centered, or "no pitch bend."
	// The value 0 (0x00 0x00) means, "bend as low as possible," and,
	// similarly, 16383 (0x7F 0x7F) is to "bend as high as possible."
	// The exact range of the pitch bend is specific to the synthesizer.

	// pre: 0 <= value <= 16383

	BYTE byte1 = static_cast<BYTE>(value >> 7);		// get last   7 bytes only
	BYTE byte2 = static_cast<BYTE>(value & 0x7F);	// get first  7 bytes only
	short converted = byte1<<8 | byte2; // merge

	return converted;

}

//Bend midi pitch for given midi channel using tracker param (0x00-0xFF)
void CVstPlugin::MidiPitchBend(UINT nMidiCh, int nParam, UINT /*trackChannel*/)
//-----------------------------------------------------------------------------
{
	const int16 increment = static_cast<int16>(nParam * 0x2000/0xFF);
	int16 newPitchBendPos = m_nMidiPitchBendPos[nMidiCh] + increment;
	Limit(newPitchBendPos, int16(MIDI_PitchBend_Min), int16(MIDI_PitchBend_Max));

	MidiPitchBend(nMidiCh, newPitchBendPos);
}

//Set midi pitch for given midi channel using uncoverted midi value (0-16383)
void CVstPlugin::MidiPitchBend(UINT nMidiCh, short newPitchBendPos)
//-----------------------------------------------------------------
{
	m_nMidiPitchBendPos[nMidiCh] = newPitchBendPos; //store pitch bend position
	short converted = getMIDI14bitValueFromShort(newPitchBendPos);
	MidiSend(converted<<8 | MIDI_PitchBend_Command|nMidiCh);
}


//rewbs.introVST - many changes to MidiCommand, still to be refined.
void CVstPlugin::MidiCommand(UINT nMidiCh, UINT nMidiProg, WORD wMidiBank, UINT note, UINT vol, UINT trackChannel)
//----------------------------------------------------------------------------------------------------------------
{
	PVSTINSTCH pCh = &m_MidiCh[nMidiCh];
	DWORD dwMidiCode = 0;
	bool bankChanged = (pCh->wMidiBank != --wMidiBank) && (wMidiBank < 0x80);
	bool progChanged = (pCh->nProgram != --nMidiProg) && (nMidiProg < 0x80);
	//bool chanChanged = nCh != m_nPreviousMidiChan;
	//get vol in [0,128[
	vol = min(vol / 2, 127);

	// Note: Some VSTis update bank/prog on midi channel change, others don't.
	//       For those that don't, we do it for them.
	// Note 17/12/2005 - I have disabled that because it breaks Edirol Hypercanvas
	// and I can't remember why it would be useful.

	// Bank change
	if ((wMidiBank < 0x80) && ( bankChanged /*|| chanChanged */))
	{
		pCh->wMidiBank = wMidiBank;
		MidiSend(((wMidiBank<<16)|(0x20<<8))|(0xB0|nMidiCh));
	}
	// Program change
	// Note: Some plugs (Edirol Orchestral) don't update on bank change only -
	// need to force it by sending prog change too.
	if ((nMidiProg < 0x80) && (progChanged || bankChanged /*|| chanChanged */ ))
	{
		pCh->nProgram = nMidiProg;
		//GetSoundFile()->ProcessMIDIMacro(trackChannel, false, GetSoundFile()->m_MidiCfg.szMidiGlb[MIDIOUT_PROGRAM], 0);
		MidiSend((nMidiProg<<8)|(0xC0|nMidiCh));
	}


	// Specific Note Off
	if (note > NOTE_KEYOFF)			//rewbs.vstiLive
	{
		dwMidiCode = 0x80|nMidiCh; //note off, on chan nCh

		note--;
		UINT i = note - NOTE_KEYOFF;
		if (pCh->uNoteOnMap[i][trackChannel])
		{
			pCh->uNoteOnMap[i][trackChannel]--;
			MidiSend(dwMidiCode|(i<<8));
		}
	}

	// "Hard core" All Sounds Off on this midi and tracker channel
	// This one doesn't check the note mask - just one note off per note.
	// Also less likely to cause a VST event buffer overflow.
	else if (note == NOTE_NOTECUT)	// ^^
	{
		//MidiSend(0xB0|nCh|(0x79<<8)); // reset all controllers
		MidiSend(0xB0|nMidiCh|(0x7b<<8));   // all notes off
		MidiSend(0xB0|nMidiCh|(0x78<<8));   // all sounds off

		dwMidiCode = 0x80|nMidiCh|(vol<<16); //note off, on chan nCh; vol is note off velocity.
		for (UINT i=0; i<128; i++)	//all notes
		{
			pCh->uNoteOnMap[i][trackChannel]=0;
			MidiSend(dwMidiCode|(i<<8));
		}

	}

	// All "active" notes off on this midi and tracker channel
	// using note mask.
	else if (note > 0x80) // ==
	{
		dwMidiCode = 0x80|nMidiCh|(vol<<16); //note off, on chan nCh; vol is note off velocity.

		for (UINT i=0; i<128; i++)
		{
			// Some VSTis need a note off for each instance of a note on, e.g. fabfilter.
			// But this can cause a VST event overflow if we have many active notes...
			while (pCh->uNoteOnMap[i][trackChannel])
			{
				if (MidiSend(dwMidiCode|(i<<8)))
				{
					pCh->uNoteOnMap[i][trackChannel]--;
				} else
				{
					// VST event queue overflow, no point in submitting more note offs.
                    break; //todo: secondary buffer?
				}
			}
		}
	}

	// Note On
	else if (note > 0)
	{
		dwMidiCode = 0x90|nMidiCh; //note on, on chan nCh

		note--;

		//reset pitch bend on each new note, tracker style.
		if (m_nMidiPitchBendPos[nMidiCh] != MIDI_PitchBend_Centre)
		{
			MidiPitchBend(nMidiCh, MIDI_PitchBend_Centre);
		}

		// count instances of active notes.
		// This is to send a note off for each instance of a note, for plugs like Fabfilter.
		// Problem: if a note dies out naturally and we never send a note off, this counter
		// will block at max until note off. Is this a problem?
		// Safe to assume we won't need more than 16 note offs max on a given note?
		if (pCh->uNoteOnMap[note][trackChannel]<17)
			pCh->uNoteOnMap[note][trackChannel]++;



		MidiSend(dwMidiCode|(note<<8)|(vol<<16));
	}

	m_nPreviousMidiChan = nMidiCh;

}


bool CVstPlugin::isPlaying(UINT note, UINT midiChn, UINT trackerChn)
//------------------------------------------------------------------
{
	note--;
	PVSTINSTCH pMidiCh = &m_MidiCh[midiChn];
	return (pMidiCh->uNoteOnMap[note][trackerChn] != 0);
}


bool CVstPlugin::MoveNote(UINT note, UINT midiChn, UINT sourceTrackerChn, UINT destTrackerChn)
//---------------------------------------------------------------------------------------------
{
	note--;
	PVSTINSTCH pMidiCh = &m_MidiCh[midiChn & 0x0f];

	if (!(pMidiCh->uNoteOnMap[note][sourceTrackerChn]))
		return false;

	pMidiCh->uNoteOnMap[note][sourceTrackerChn]--;
	pMidiCh->uNoteOnMap[note][destTrackerChn]++;
	return true;
}
//end rewbs.introVST


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
		if(pModDoc->GetSoundFile() && pModDoc->GetSoundFile()->GetModSpecifications().supportsPlugins)
		{
			CMainFrame::GetMainFrame()->ThreadSafeSetModified(pModDoc);
		}

		// TODO: Could be used to update general tab in real time, but causes flickers in treeview
		// Better idea: add an update hint just for plugin params?
		//pModDoc->UpdateAllViews(NULL, HINT_MIXPLUGINS, NULL);

		if (CMainFrame::GetInputHandler()->ShiftPressed())
		{
			// Shift pressed -> Open MIDI mapping dialog
			CMainFrame::GetInputHandler()->SetModifierMask(0); // Make sure that the dialog will open only once.

			const HWND oldMIDIRecondWnd = CMainFrame::GetMainFrame()->GetMidiRecordWnd();
			CMIDIMappingDialog dlg(pVstEditor, *pModDoc->GetSoundFile());
			dlg.m_Setting.SetParamIndex(param);
			dlg.m_Setting.SetPlugIndex(GetSlot() + 1);
			dlg.DoModal();
			CMainFrame::GetMainFrame()->SetMidiRecordWnd(oldMIDIRecondWnd);
		}

		// Learn macro
		int macroToLearn = pVstEditor->GetLearnMacro();
		if (macroToLearn > -1)
		{
			pModDoc->LearnMacro(macroToLearn, param);
			pVstEditor->SetLearnMacro(-1);
		}
	}
}


void CVstPlugin::SaveAllParameters()
//----------------------------------
{
	if ((m_pEffect) && (m_pMixStruct))
	{
		m_pMixStruct->defaultProgram = -1;
		m_bModified = false;

		if ((m_pEffect->flags & effFlagsProgramChunks)
		 && (Dispatch(effIdentify, 0,0, NULL, 0) == 'NvEf'))
		{
			void *p = nullptr;
			LONG nByteSize = 0;

			// Try to get whole bank
			nByteSize = Dispatch(effGetChunk, 0,0, &p, 0);

			if (!p)
			{
				nByteSize = Dispatch(effGetChunk, 1,0, &p, 0); 	// Getting bank failed, try to get get just preset
			} else
			{
				m_pMixStruct->defaultProgram = GetCurrentProgram();	//we managed to get the bank, now we need to remember which program we're on.
			}
			if (p != nullptr)
			{
				if ((m_pMixStruct->pPluginData) && (m_pMixStruct->nPluginDataSize >= (UINT)(nByteSize+4)))
				{
					m_pMixStruct->nPluginDataSize = nByteSize+4;
				} else
				{
					delete[] m_pMixStruct->pPluginData;
					m_pMixStruct->nPluginDataSize = 0;
					m_pMixStruct->pPluginData = new char[nByteSize+4];
					if (m_pMixStruct->pPluginData)
					{
						m_pMixStruct->nPluginDataSize = nByteSize+4;
					}
				}
				if (m_pMixStruct->pPluginData)
				{
					*(ULONG *)m_pMixStruct->pPluginData = 'NvEf';
					memcpy(((BYTE*)m_pMixStruct->pPluginData)+4, p, nByteSize);
					return;
				}
			}
		}
		// This plug doesn't support chunks: save parameters
		UINT nParams = (m_pEffect->numParams > 0) ? m_pEffect->numParams : 0;
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
			for (UINT i = 0; i < nParams; i++)
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
	if ((m_pEffect) && (m_pMixStruct) && (m_pMixStruct->pPluginData) && (m_pMixStruct->nPluginDataSize >= 4))
	{
		UINT nParams = (m_pEffect->numParams > 0) ? m_pEffect->numParams : 0;
		UINT nLen = nParams * sizeof(float);
		ULONG nType = *(ULONG *)m_pMixStruct->pPluginData;

		if ((Dispatch(effIdentify, 0, nullptr, nullptr, 0) == 'NvEf') && (nType == 'NvEf'))
		{
			void *p = NULL;
			Dispatch(effGetChunk, 0,0, &p, 0); //init plug for chunk reception

			if ((nProgram>=0) && (nProgram < m_pEffect->numPrograms))
			{
				// Bank
				Dispatch(effSetChunk, 0, m_pMixStruct->nPluginDataSize - 4, ((BYTE *)m_pMixStruct->pPluginData) + 4, 0);
				SetCurrentProgram(nProgram);
			} else
			{
				// Program
				Dispatch(effSetChunk, 1, m_pMixStruct->nPluginDataSize - 4, ((BYTE *)m_pMixStruct->pPluginData) + 4, 0);
			}

		} else
		{
			float *p = (float *)m_pMixStruct->pPluginData;
			if (m_pMixStruct->nPluginDataSize >= nLen + 4) p++;
			if (m_pMixStruct->nPluginDataSize >= nLen)
			{
				for (UINT i = 0; i < nParams; i++)
				{
					SetParameter(i, p[i]);
				}
			}
		}
	}
}


void CVstPlugin::ToggleEditor()
//-----------------------------
{
	if (!m_pEffect) return;

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
			//rewbs.defaultPlugGui
			if (HasEditor())
				m_pEditor =  new COwnerVstEditor(this);
			else
				m_pEditor = new CDefaultVstEditor(this);
			//end rewbs.defaultPlugGui

			if (m_pEditor)
				m_pEditor->OpenEditor(CMainFrame::GetMainFrame());
		}
	} catch (...)
	{
		CVstPluginManager::ReportPlugException("Exception in ToggleEditor() (Plugin=%s)\n", m_pFactory->szLibraryName);
	}
}


UINT CVstPlugin::GetNumCommands()
//-------------------------------
{
	if ((m_pEffect) && (m_pEffect->magic == kBuzzMagic))
	{
		return Dispatch(effBuzzGetNumCommands, 0, nullptr, nullptr, 0.0f);
	}
	else if (m_pEffect)
	{
		return m_pEffect->numParams;
	}
	return 0;
}


BOOL CVstPlugin::GetCommandName(UINT nIndex, LPSTR pszName)
//---------------------------------------------------------
{
	if ((m_pEffect) && (m_pEffect->magic == kBuzzMagic))
	{
		return Dispatch(effBuzzGetCommandName, nIndex, nullptr, pszName, 0.0f);
	}
	else if (m_pEffect)
	{
		return Dispatch(effGetParamName, nIndex, nullptr, pszName, 0.0f);
	}
	return 0;
}


BOOL CVstPlugin::ExecuteCommand(UINT nIndex)
//------------------------------------------
{
	if ((m_pEffect) && (m_pEffect->magic == kBuzzMagic))
	{
		return Dispatch(effBuzzExecuteCommand, nIndex, nullptr, nullptr, 0.0f);
	}
	return 0;
}


//rewbs.defaultPlugGui
CAbstractVstEditor* CVstPlugin::GetEditor()
//-----------------------------------------
{
	return m_pEditor;
}


bool CVstPlugin::Bypass(bool bypass)
//-----------------------------------
{
	if (bypass)
		m_pMixStruct->Info.dwInputRouting |= MIXPLUG_INPUTF_BYPASS;
	else
		m_pMixStruct->Info.dwInputRouting &= ~MIXPLUG_INPUTF_BYPASS;

	Dispatch(effSetBypass, bypass ? 1 : 0, nullptr, nullptr, 0.0f);

#ifdef MODPLUG_TRACKER
	if (m_pModDoc)
		m_pModDoc->UpdateAllViews(NULL, HINT_MIXPLUGINS, NULL);
#endif // MODPLUG_TRACKER

	return bypass;
}


//end rewbs.defaultPlugGui
//rewbs.defaultPlugGui: CVstEditor now COwnerVstEditor


//rewbs.VSTcompliance
bool CVstPlugin::GetSpeakerArrangement()
//--------------------------------------
{
	VstSpeakerArrangement **pSA = NULL;
	Dispatch(effGetSpeakerArrangement, 0, 0, pSA, 0);
	//Dispatch(effGetSpeakerArrangement, 0,(long)pSA,NULL,0);
	if (pSA)
	{
		MemCopy(speakerArrangement, **pSA);
	}

	return true;
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
	if (m_pSndFile)
	{
		while ((m_pMixStruct != &(m_pSndFile->m_MixPlugins[slot])) && slot < MAX_MIXPLUGINS - 1)
		{
			slot++;
		}
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


void CVstPlugin::UpdateMixStructPtr(PSNDMIXPLUGIN p)
//--------------------------------------------------
{
	m_pMixStruct = p;
}

//end rewbs.VSTcompliance

bool CVstPlugin::isInstrument() // ericus 18/02/2005
//-----------------------------
{
	if(m_pEffect) return ((m_pEffect->flags & effFlagsIsSynth) || (!m_pEffect->numInputs)); // rewbs.dryRatio
	return false;
}


bool CVstPlugin::CanRecieveMidiEvents()
//-------------------------------------
{
	return (CVstPlugin::Dispatch(effCanDo, 0, nullptr, "receiveVstMidiEvent", 0.0f) != 0);
}


void CVstPlugin::GetOutputPlugList(CArray<CVstPlugin*, CVstPlugin*> &list)
//------------------------------------------------------------------------
{
	// At the moment we know there will only be 1 output.
	// Returning NULL ptr means plugin outputs directly to master.
	list.RemoveAll();

	CVstPlugin *pOutputPlug = NULL;
	if (m_pMixStruct->Info.dwOutputRouting & 0x80)
	{
		UINT nOutput = m_pMixStruct->Info.dwOutputRouting & 0x7f;
		if (m_pSndFile && (nOutput > m_nSlot) && (nOutput < MAX_MIXPLUGINS))
		{
			pOutputPlug = reinterpret_cast<CVstPlugin *>(m_pSndFile->m_MixPlugins[nOutput].pMixPlugin);
		}
	}
	list.Add(pOutputPlug);

	return;
}

void CVstPlugin::GetInputPlugList(CArray<CVstPlugin*, CVstPlugin*> &list)
//-----------------------------------------------------------------------
{
	if(m_pSndFile == 0) return;

	CArray<CVstPlugin*, CVstPlugin*> candidatePlugOutputs;
	CVstPlugin* pCandidatePlug = NULL;
	list.RemoveAll();

	for (int nPlug = 0; nPlug < MAX_MIXPLUGINS; nPlug++)
	{
		pCandidatePlug = reinterpret_cast<CVstPlugin *>(m_pSndFile->m_MixPlugins[nPlug].pMixPlugin);
		if (pCandidatePlug)
		{
			pCandidatePlug->GetOutputPlugList(candidatePlugOutputs);

			for(int nOutput=0; nOutput<candidatePlugOutputs.GetSize(); nOutput++)
			{
				if (candidatePlugOutputs[nOutput] == this)
				{
					list.Add(pCandidatePlug);
					break;
				}
			}
		}
	}

	return;
}

void CVstPlugin::GetInputInstrumentList(CArray<UINT,UINT> &list)
//--------------------------------------------------------------
{
	list.RemoveAll();
	if(m_pSndFile == nullptr) return;

	const PLUGINDEX nThisMixPlug = m_nSlot + 1;		//m_nSlot is position in mixplug array.
	for (int nIns = 0; nIns <= m_pSndFile->GetNumInstruments(); nIns++)
	{
		if (m_pSndFile->Instruments[nIns] != nullptr && (m_pSndFile->Instruments[nIns]->nMixPlug == nThisMixPlug))
		{
			list.Add(nIns);
		}
	}

	return;

}

void CVstPlugin::GetInputChannelList(CArray<UINT, UINT> &list)
//------------------------------------------------------------
{
	if(m_pSndFile == 0) return;
	list.RemoveAll();

	UINT nThisMixPlug = m_nSlot+1;		//m_nSlot is position in mixplug array.
	const CHANNELINDEX chnCount = m_pSndFile->GetNumChannels();
	for (CHANNELINDEX nChn=0; nChn<chnCount; nChn++)
	{
		if (m_pSndFile->ChnSettings[nChn].nMixPlugin==nThisMixPlug)
		{
			list.Add(nChn);
		}
	}

	return;

}


//////////////////////////////////////////////////////////////////////////////////////
//
// BUZZ -> VST converter
//

#ifdef ENABLE_BUZZ

class CBuzz2Vst;

//===========================================
class CVSTDataInput: public CMachineDataInput
//===========================================
{
protected:
	CBuzz2Vst *m_pParent;

public:
	CVSTDataInput(CBuzz2Vst *p) { m_pParent = p; }
	virtual void Read(void *pbuf, int const numbytes) { Log("Read(%08X, %d)\n", pbuf, numbytes);}
};


//==================================
class CBuzz2Vst: public CMICallbacks
//==================================
{
protected:
	AEffect m_Effect;
	const CMachineInfo *m_pMachineInfo;
	CMachineInterface *m_pMachineInterface;
	CVSTDataInput *m_pDataInput;
	CMasterInfo m_MasterInfo;
	CWaveLevel m_WaveLevel;
	short m_Waveform[32];

public:
	CBuzz2Vst(CMachineInterface *, const CMachineInfo *);
	~CBuzz2Vst();
	AEffect *GetEffect() { return &m_Effect; }
	VstIntPtr Dispatcher(VstInt32 opCode, VstInt32 index, VstIntPtr value, void *ptr, float opt);
	void SetParameter(VstInt32 index, float parameter);
	float GetParameter(VstInt32 index);
	void Process(float **inputs, float **outputs, long sampleframes);

public:
	static VstIntPtr VSTCALLBACK BuzzDispatcher(AEffect *effect, VstInt32 opCode, VstInt32 index, VstIntPtr value, void *ptr, float opt);
	static void VSTCALLBACK BuzzSetParameter(AEffect *effect, VstInt32 index, float parameter);
	static float VSTCALLBACK BuzzGetParameter(AEffect *effect, VstInt32 index);
	static void VSTCALLBACK BuzzProcess(AEffect *effect, float **inputs, float **outputs, VstInt32 sampleframes);

	// CMICallbacks interface
public:
	virtual CWaveInfo const *GetWave(int const) { Log("GetWave\n"); return NULL; }
	virtual CWaveLevel const *GetWaveLevel(int const i, int const level) { Log("GetWaveLevel\n"); return NULL; }
	virtual void MessageBox(char const *txt) { Reporting::Notification(txt, m_pMachineInfo->Name); }
	virtual void Lock() { Log("Lock\n"); }
	virtual void Unlock() { Log("Unlock\n"); }
	virtual int GetWritePos() { Log("GetWritePos\n"); return 0; }
	virtual int GetPlayPos() { Log("GetPlayPos\n"); return 0; }
	virtual float *GetAuxBuffer() { Log("GetAuxBuffer\n"); return NULL; }
	virtual void ClearAuxBuffer() { Log("ClearAuxBuffer\n"); }
	virtual int GetFreeWave() { Log("GetFreeWave\n"); return 0; }
	virtual bool AllocateWave(int const i, int const size, char const *name) { Log("AllocateWave\n"); return false; }
	virtual void ScheduleEvent(int const time, dword const data) { Log("ScheduleEvent\n"); }
	virtual void MidiOut(int const dev, dword const data) { Log("MidiOut\n"); }
	virtual short const *GetOscillatorTable(int const waveform) { Log("GetOscillatorTable\n"); return NULL; }

	// envelopes
	virtual int GetEnvSize(int const wave, int const env) { Log("GetEnvSize\n"); return 0; }
	virtual bool GetEnvPoint(int const wave, int const env, int const i, word &x, word &y, int &flags) { Log("GetEnvPoint\n"); return false; }
	virtual CWaveLevel const *GetNearestWaveLevel(int const i, int const note) { Log("GetNearestWaveLevel(%d, %d)\n", i, note); return &m_WaveLevel; }

	// pattern editing
	virtual void SetNumberOfTracks(int const n) { Log("SetNumberOfTracks\n"); }
	virtual CPattern *CreatePattern(char const *name, int const length) { Log("CreatePattern\n"); return NULL; }
	virtual CPattern *GetPattern(int const index) { Log("GetPattern\n"); return NULL; }
	virtual char const *GetPatternName(CPattern *ppat) { Log("GetPatternName\n"); return ""; }
	virtual void RenamePattern(char const *oldname, char const *newname) { Log("RenamePattern\n"); }
	virtual void DeletePattern(CPattern *ppat) { Log("DeletePattern\n"); }
	virtual int GetPatternData(CPattern *ppat, int const row, int const group, int const track, int const field) { Log("GetPatternData\n"); return 0; }
	virtual void SetPatternData(CPattern *ppat, int const row, int const group, int const track, int const field, int const value) { Log("SetPatternData\n"); }

	// sequence editing
	virtual CSequence *CreateSequence() { Log("CreateSequence\n"); return NULL; }
	virtual void DeleteSequence(CSequence *pseq) { Log("DeleteSequence\n"); }

	// special ppat values for GetSequenceData and SetSequenceData
	// empty = NULL
	// <break> = (CPattern *)1
	// <mute> = (CPattern *)2
	// <thru> = (CPattern *)3
	virtual CPattern *GetSequenceData(int const row) { Log("GetSequenceData\n"); return NULL; }
	virtual void SetSequenceData(int const row, CPattern *ppat) { Log("SetSequenceData\n"); }

	// buzz v1.2 (MI_VERSION 15) additions start here
	virtual void SetMachineInterfaceEx(CMachineInterfaceEx *pex) { Log("SetMachineInterfaceEx\n"); }
	// group 1=global, 2=track
	virtual void ControlChange__obsolete__(int group, int track, int param, int value) { Log("ControlChange__obsolete__\n"); }

	// direct calls to audiodriver, used by WaveInput and WaveOutput
	// shouldn't be used for anything else
	virtual int ADGetnumChannels(bool input) { Log("ADGetNumChannels\n"); return 2; }
	virtual void ADWrite(int channel, float *psamples, int numsamples) { Log("ADWrite\n"); }
	virtual void ADRead(int channel, float *psamples, int numsamples) { Log("ADRead\n"); }

	virtual CMachine *GetThisMachine() { Log("GetThisMachine\n"); return NULL; }
	virtual void ControlChange(CMachine *pmac, int group, int track, int param, int value) { Log("ControlChange\n"); }		// set value of parameter (group & 16 == don't record)

	// returns pointer to the sequence if there is a pattern playing
	virtual CSequence *GetPlayingSequence(CMachine *pmac) { Log("GetPlayingSequence\n"); return NULL; }

	// gets ptr to raw pattern data for row of a track of a currently playing pattern (or something like that)
	virtual void *GetPlayingRow(CSequence *pseq, int group, int track) { Log("GetPlayingRow\n"); return NULL; }
	virtual int GetStateFlags() { Log("GetStateFlags\n"); return 0; }
	virtual void SetnumOutputChannels(CMachine *pmac, int n) { Log("SetNumOutputChannels\n"); }	// if n=1 Work(), n=2 WorkMonoToStereo()
	virtual void SetEventHandler(CMachine *pmac, BEventType et, EVENT_HANDLER_PTR p, void *param) { Log("SetEventHandler\n"); }
	virtual char const *GetWaveName(int const i) { Log("GetWavename\n"); return ""; }
	virtual void SetInternalWaveName(CMachine *pmac, int const i, char const *name) { Log("SetInternalWaveName\n"); }	// i >= 1, NULL name to clear
	virtual void GetMachineNames(CMachineDataOutput *pout) { Log("GetMachineNames\n"); }		// *pout will get one name per Write()
	virtual CMachine *GetMachine(char const *name) { Log("GetMachine\n"); return NULL; }
	virtual CMachineInfo const *GetMachineInfo(CMachine *pmac) { Log("GetMachineInfo\n"); return NULL; }
	virtual char const *GetMachineName(CMachine *pmac) { Log("GetMachineName\n"); return ""; }
	virtual bool GetInput(int index, float *psamples, int numsamples, bool stereo, float *extrabuffer) { Log("GetInput\n"); return false; }
};


CBuzz2Vst::CBuzz2Vst(CMachineInterface *pBuzzMachine, const CMachineInfo *pBuzzInfo)
//----------------------------------------------------------------------------------
{
	m_pMachineInfo = pBuzzInfo;
	m_pMachineInterface = pBuzzMachine;
	m_pDataInput = new CVSTDataInput(this);

	MemsetZero(m_Effect);
	m_Effect.magic = kBuzzMagic;
	m_Effect.numPrograms = 1;
	m_Effect.numParams = pBuzzInfo->numGlobalParameters;
	m_Effect.numInputs = 2;
	m_Effect.numOutputs = 2;
	m_Effect.flags = 0;		// see constants
	m_Effect.ioRatio = 1.0f;
	m_Effect.object = this;
	m_Effect.uniqueID = pBuzzInfo->Version;
	m_Effect.version = 2;
	// Callbacks
	m_Effect.dispatcher = BuzzDispatcher;
	m_Effect.setParameter = BuzzSetParameter;
	m_Effect.getParameter = BuzzGetParameter;
	m_Effect.process = BuzzProcess;
	m_Effect.processReplacing = NULL;
	// Buzz Info
	m_pMachineInterface->pMasterInfo = &m_MasterInfo;
	m_pMachineInterface->pCB = this;
	m_MasterInfo.BeatsPerMin = 125;
	m_MasterInfo.TicksPerBeat = 6;
	m_MasterInfo.SamplesPerSec = CSoundFile::gdwMixingFreq;
	m_MasterInfo.SamplesPerTick = (int)((60*m_MasterInfo.SamplesPerSec) / (125*6));
	m_MasterInfo.PosInTick = 0;
	m_MasterInfo.TicksPerSec = (float)m_MasterInfo.SamplesPerSec / (float)m_MasterInfo.SamplesPerTick;
	// Dummy Wave Level
	MemsetZero(m_Waveform);
	m_WaveLevel.numSamples = 16;
	m_WaveLevel.pSamples = m_Waveform;
	m_WaveLevel.RootNote = 60;
	m_WaveLevel.SamplesPerSec = 44100;
	m_WaveLevel.LoopStart = 0;
	m_WaveLevel.LoopEnd = 0;
}


CBuzz2Vst::~CBuzz2Vst()
//---------------------
{
	m_Effect.object = NULL;
	if (m_pMachineInfo)
	{
		m_pMachineInfo = NULL;
	}
	if (m_pMachineInterface)
	{
		delete m_pMachineInterface;
		m_pMachineInterface = NULL;
	}
	if (m_pDataInput)
	{
		delete m_pDataInput;
		m_pDataInput = NULL;
	}
}


VstIntPtr CBuzz2Vst::BuzzDispatcher(AEffect *effect, VstInt32 opCode, VstInt32 index, VstIntPtr value, void *ptr, float opt)
//--------------------------------------------------------------------------------------------------------------------------
{
	CBuzz2Vst *pBuzz2Vst = (CBuzz2Vst *)effect->object;
	if (pBuzz2Vst) return pBuzz2Vst->Dispatcher(opCode, index, value, ptr, opt);
	return 0;
}


void CBuzz2Vst::BuzzSetParameter(AEffect *effect, VstInt32 index, float parameter)
//--------------------------------------------------------------------------------
{
	CBuzz2Vst *pBuzz2Vst = (CBuzz2Vst *)effect->object;
	if (pBuzz2Vst) pBuzz2Vst->SetParameter(index, parameter);
}


float CBuzz2Vst::BuzzGetParameter(AEffect *effect, VstInt32 index)
//----------------------------------------------------------------
{
	CBuzz2Vst *pBuzz2Vst = (CBuzz2Vst *)effect->object;
	if (pBuzz2Vst) return pBuzz2Vst->GetParameter(index);
	return 0;
}


void CBuzz2Vst::BuzzProcess(AEffect *effect, float **inputs, float **outputs, VstInt32 sampleframes)
//--------------------------------------------------------------------------------------------------
{
	CBuzz2Vst *pBuzz2Vst = (CBuzz2Vst *)effect->object;
	if (pBuzz2Vst) pBuzz2Vst->Process(inputs, outputs, sampleframes);
}


VstIntPtr CBuzz2Vst::Dispatcher(VstInt32 opCode, VstInt32 index, VstIntPtr value, void *ptr, float opt)
//-----------------------------------------------------------------------------------------------------
{
	if (!m_pMachineInterface) return 0;
	switch(opCode)
	{
	case effOpen:
		Log("Initializing machine...\n");
		m_pMachineInterface->Init(m_pDataInput);
		Log("Done.\n");
		break;

	case effClose:
		m_Effect.object = NULL;
		delete this;
		return 0;

	case effIdentify:
		return 'NvEf';

	case effGetParamName:
		if (index < m_pMachineInfo->numGlobalParameters)
		{
			const CMachineParameter *p = m_pMachineInfo->Parameters[index];
			if ((p) && (ptr) && (p->Name))
			{
				lstrcpyn((char *)ptr, p->Name, 9);
			}
		}
		break;

	case effGetParamDisplay:
		if (index < m_pMachineInfo->numGlobalParameters)
		{
			const CMachineParameter *p = m_pMachineInfo->Parameters[index];
			if (p)
			{
				int value = p->DefValue;
				const char *s = m_pMachineInterface->DescribeValue(index, value);
				if ((s) && (ptr))
				{
					lstrcpyn((char *)ptr, s, 9);
				}
			}
		}
		break;

	// Buzz extensions
	case effBuzzGetNumCommands:
		{
			const char *p = m_pMachineInfo->Commands;
			UINT n = 0;
			while (p)
			{
				if (!*p)
				{
					n++;
					break;
				}
				if (*p == '\n') n++;
				p++;
			}
			return n;
		}

	case effBuzzGetCommandName:
		if (ptr)
		{
			const char *p = m_pMachineInfo->Commands;
			char *s = (char *)ptr;
			UINT n = 0, len = 0;
			while ((p) && (n <= (UINT)index))
			{
				if (!*p)
				{
					n++;
					break;
				}
				if (*p == '\n') n++;
				if ((n == (UINT)index) && (len < 10))
				{
					*s++ = *p;
					len++;
				}
				p++;
			}
			*s = 0;
			return 0;
		}

	case effBuzzExecuteCommand:
		{
			m_pMachineInterface->Command(index);
		}
	}
	return 0;
}


void CBuzz2Vst::SetParameter(VstInt32 index, float parameter)
//-----------------------------------------------------------
{

}


float CBuzz2Vst::GetParameter(VstInt32 index)
//-------------------------------------------
{
	if (index < m_pMachineInfo->numGlobalParameters)
	{
		const CMachineParameter *p = m_pMachineInfo->Parameters[index];
		if (p)
		{
			float fmin = (float)p->MinValue;
			float fmax = (float)p->MaxValue;
			float f = (float)p->DefValue;
			if (p->MinValue < p->MaxValue)
			{
				return (f - fmin) / (fmax - fmin);
			}
		}
	}
	return 0;
}


void X86_Interleave(const float *pinL, const float *pinR, float *pout, int nsamples)
//----------------------------------------------------------------------------------
{
	for (int i=0; i<nsamples; i++)
	{
		pout[i*2] = pinL[i];
		pout[i*2+1] = pinR[i];
	}
}


void X86_AddAndDeinterleave(const float *pin, float *poutL, float *poutR, int nsamples)
//-------------------------------------------------------------------------------------
{
	for (int i=0; i<nsamples; i++)
	{
		poutL[i] += pin[i*2];
		poutR[i] += pin[i*2+1];
	}
}


void CBuzz2Vst::Process(float **inputs, float **outputs, long sampleframes)
//-------------------------------------------------------------------------
{
	float *pinL = inputs[0], *pinR = inputs[1];
	float *poutL = outputs[0], *poutR = outputs[1];
	float Buffer[MIXBUFFERSIZE*2];		// Stereo interleaved

	// Re-interleave the stereo mix
	X86_Interleave(pinL, pinR, Buffer, sampleframes);
	// Process
	m_pMachineInterface->Work(Buffer, sampleframes, WM_READWRITE);
	// De-interleave and add the result
	X86_AddAndDeinterleave(Buffer, poutL, poutR, sampleframes);
}


AEffect *Buzz2Vst(CMachineInterface *pBuzzMachine, const CMachineInfo *pBuzzInfo)
//-------------------------------------------------------------------------------
{
	CBuzz2Vst *p;

	if ((!pBuzzMachine) || (!pBuzzInfo)) return NULL;
	p = new CBuzz2Vst(pBuzzMachine, pBuzzInfo);
	return (p) ? p->GetEffect() : NULL;
}

#endif // ENABLE_BUZZ


//////////////////////////////////////////////////////////////////////////////////////////////////
//
// DirectX Media Object support
//

//============
class CDmo2Vst
//============
{
protected:
	IMediaObject *m_pMediaObject;
	IMediaObjectInPlace *m_pMediaProcess;
	IMediaParamInfo *m_pParamInfo;
	IMediaParams *m_pMediaParams;
	ULONG m_nSamplesPerSec;
	AEffect m_Effect;
	REFERENCE_TIME m_DataTime;
	float *m_pMixBuffer;
	float m_MixBuffer[MIXBUFFERSIZE * 2 + 16];		// 16-bit Stereo interleaved

public:
	CDmo2Vst(IMediaObject *pMO, IMediaObjectInPlace *pMOIP, DWORD uid);
	~CDmo2Vst();
	AEffect *GetEffect() { return &m_Effect; }
	VstIntPtr Dispatcher(VstInt32 opCode, VstInt32 index, VstIntPtr value, void *ptr, float opt);
	void SetParameter(VstInt32 index, float parameter);
	float GetParameter(VstInt32 index);
	void Process(float * const *inputs, float **outputs, long sampleframes);

public:
	static VstIntPtr VSTCALLBACK DmoDispatcher(AEffect *effect, VstInt32 opCode, VstInt32 index, VstIntPtr value, void *ptr, float opt);
	static void VSTCALLBACK DmoSetParameter(AEffect *effect, VstInt32 index, float parameter);
	static float VSTCALLBACK DmoGetParameter(AEffect *effect, VstInt32 index);
	static void VSTCALLBACK DmoProcess(AEffect *effect, float **inputs, float **outputs, VstInt32 sampleframes);

protected:
	void Interleave(const float *pinL, const float *pinR, int nsamples);
	void Deinterleave(float *poutL, float *poutR, int nsamples);
};


CDmo2Vst::CDmo2Vst(IMediaObject *pMO, IMediaObjectInPlace *pMOIP, DWORD uid)
//--------------------------------------------------------------------------
{
	m_pMediaObject = pMO;
	m_pMediaProcess = pMOIP;
	m_pParamInfo = NULL;
	m_pMediaParams = NULL;
	MemsetZero(m_Effect);
	m_Effect.magic = kDmoMagic;
	m_Effect.numPrograms = 0;
	m_Effect.numParams = 0;
	m_Effect.numInputs = 2;
	m_Effect.numOutputs = 2;
	m_Effect.flags = 0;		// see constants
	m_Effect.ioRatio = 1.0f;
	m_Effect.object = this;
	m_Effect.uniqueID = uid;
	m_Effect.version = 2;
	m_nSamplesPerSec = 44100;
	m_DataTime = 0;
	if (FAILED(m_pMediaObject->QueryInterface(IID_IMediaParamInfo, (void **)&m_pParamInfo))) m_pParamInfo = NULL;
	if (m_pParamInfo)
	{
		DWORD dwParamCount = 0;
		m_pParamInfo->GetParamCount(&dwParamCount);
		m_Effect.numParams = dwParamCount;
	}
	if (FAILED(m_pMediaObject->QueryInterface(IID_IMediaParams, (void **)&m_pMediaParams))) m_pMediaParams = NULL;
	m_pMixBuffer = (float *)((((int)m_MixBuffer)+15)&~15);
	// Callbacks
	m_Effect.dispatcher = DmoDispatcher;
	m_Effect.setParameter = DmoSetParameter;
	m_Effect.getParameter = DmoGetParameter;
	m_Effect.process = DmoProcess;
	m_Effect.processReplacing = NULL;
}


CDmo2Vst::~CDmo2Vst()
//-------------------
{
	if (m_pMediaParams)
	{
		m_pMediaParams->Release();
		m_pMediaParams = NULL;
	}
	if (m_pParamInfo)
	{
		m_pParamInfo->Release();
		m_pParamInfo = NULL;
	}
	if (m_pMediaProcess)
	{
		m_pMediaProcess->Release();
		m_pMediaProcess = NULL;
	}
	if (m_pMediaObject)
	{
		m_pMediaObject->Release();
		m_pMediaObject = NULL;
	}
}


VstIntPtr CDmo2Vst::DmoDispatcher(AEffect *effect, VstInt32 opCode, VstInt32 index, VstIntPtr value, void *ptr, float opt)
//------------------------------------------------------------------------------------------------------------------------
{
	if (effect)
	{
		CDmo2Vst *that = (CDmo2Vst *)effect->object;
		if (that) return that->Dispatcher(opCode, index, value, ptr, opt);
	}
	return 0;
}


void CDmo2Vst::DmoSetParameter(AEffect *effect, VstInt32 index, float parameter)
//------------------------------------------------------------------------------
{
	if (effect)
	{
		CDmo2Vst *that = (CDmo2Vst *)effect->object;
		if (that) that->SetParameter(index, parameter);
	}
}


float CDmo2Vst::DmoGetParameter(AEffect *effect, VstInt32 index)
//----------------------------------------------------------
{
	if (effect)
	{
		CDmo2Vst *that = (CDmo2Vst *)effect->object;
		if (that) return that->GetParameter(index);
	}
	return 0;
}


void CDmo2Vst::DmoProcess(AEffect *effect, float **inputs, float **outputs, VstInt32 sampleframes)
//------------------------------------------------------------------------------------------------
{
	if (effect)
	{
		CDmo2Vst *that = (CDmo2Vst *)effect->object;
		if (that) that->Process(inputs, outputs, sampleframes);
	}
}


VstIntPtr CDmo2Vst::Dispatcher(VstInt32 opCode, VstInt32 index, VstIntPtr value, void *ptr, float opt)
//----------------------------------------------------------------------------------------------------
{
	switch(opCode)
	{
	case effOpen:
		break;

	case effClose:
		m_Effect.object = nullptr;
		delete this;
		return 0;

	case effGetParamName:
	case effGetParamLabel:
	case effGetParamDisplay:
		if ((index < m_Effect.numParams) && (m_pParamInfo))
		{
			MP_PARAMINFO mpi;
			LPSTR pszName = (LPSTR)ptr;

			mpi.mpType = MPT_INT;
			mpi.szUnitText[0] = 0;
			mpi.szLabel[0] = 0;
			pszName[0] = 0;
			if (m_pParamInfo->GetParamInfo(index, &mpi) == S_OK)
			{
				if (opCode == effGetParamDisplay)
				{
					MP_DATA md;

					if ((m_pMediaParams) && (m_pMediaParams->GetParam(index, &md) == S_OK))
					{
						switch(mpi.mpType)
						{
						case MPT_FLOAT:
							{
								int nValue = (int)(md * 100.0f + 0.5f);
								bool bNeg = false;
								if (nValue < 0) { bNeg = true; nValue = -nValue; }
								wsprintf(pszName, (bNeg) ? "-%d.%02d" : "%d.%02d", nValue/100, nValue%100);
							}
							break;

						case MPT_BOOL:
							strcpy(pszName, ((int)md) ? "Yes" : "No");
							break;

						case MPT_ENUM:
							{
								WCHAR *text = nullptr;
								m_pParamInfo->GetParamText(index, &text);

								const int nValue = (int)(md * (mpi.mpdMaxValue - mpi.mpdMinValue) + 0.5f);
								// Always skip first two strings (param name, unit name)
								for(int i = 0; i < nValue + 2; i++)
								{
									text += wcslen(text) + 1;
								}
								WideCharToMultiByte(CP_ACP, 0, text, -1, pszName, 32, NULL, NULL);
							}
							break;

						default:
							wsprintf(pszName, "%d", (int)md);
							break;
						}
					}
				} else
				{
					WideCharToMultiByte(CP_ACP, 0, (opCode == effGetParamName) ? mpi.szLabel : mpi.szUnitText, -1, pszName, 32, NULL, NULL);
				}
			}
		}
		break;

	case effSetSampleRate:
		m_nSamplesPerSec = (int)opt;
		break;

	case effMainsChanged:
		if (value)
		{
			DMO_MEDIA_TYPE mt;
			WAVEFORMATEX wfx;

			mt.majortype = MEDIATYPE_Audio;
			mt.subtype = MEDIASUBTYPE_PCM;
			mt.bFixedSizeSamples = TRUE;
			mt.bTemporalCompression = FALSE;
			mt.formattype = FORMAT_WaveFormatEx;
			mt.pUnk = NULL;
			mt.pbFormat = (LPBYTE)&wfx;
			mt.cbFormat = sizeof(WAVEFORMATEX);
			mt.lSampleSize = 2 * sizeof(float);
			wfx.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
			wfx.nChannels = 2;
			wfx.nSamplesPerSec = m_nSamplesPerSec;
			wfx.wBitsPerSample = sizeof(float) * 8;
			wfx.nBlockAlign = wfx.nChannels * (wfx.wBitsPerSample >> 3);
			wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
			wfx.cbSize = 0;
			if ((FAILED(m_pMediaObject->SetInputType(0, &mt, 0)))
			 || (FAILED(m_pMediaObject->SetOutputType(0, &mt, 0))))
			{
			#ifdef DMO_LOG
				Log("DMO: Failed to set I/O media type\n");
			#endif
				return -1;
			}
		} else
		{
			m_pMediaObject->Flush();
			m_pMediaObject->SetInputType(0, NULL, DMO_SET_TYPEF_CLEAR);
			m_pMediaObject->SetOutputType(0, NULL, DMO_SET_TYPEF_CLEAR);
			m_DataTime = 0;
		}
		break;
	}
	return 0;
}


void CDmo2Vst::SetParameter(VstInt32 index, float fValue)
//-------------------------------------------------------
{
	MP_PARAMINFO mpi;

	if ((index < m_Effect.numParams) && (m_pParamInfo) && (m_pMediaParams))
	{
		MemsetZero(mpi);
		if (m_pParamInfo->GetParamInfo(index, &mpi) == S_OK)
		{
			float fMin = mpi.mpdMinValue;
			float fMax = mpi.mpdMaxValue;

			if (mpi.mpType == MPT_BOOL)
			{
				fMin = 0;
				fMax = 1;
				fValue = (fValue > 0.5f) ? 1.0f : 0.0f;
			}
			if (fMax > fMin) fValue *= (fMax - fMin);
			fValue += fMin;
			if (fValue < fMin) fValue = fMin;
			if (fValue > fMax) fValue = fMax;
			if (mpi.mpType != MPT_FLOAT) fValue = (float)(int)fValue;
			m_pMediaParams->SetParam(index, fValue);
		}
	}
}


float CDmo2Vst::GetParameter(VstInt32 index)
//------------------------------------------
{
	if ((index < m_Effect.numParams) && (m_pParamInfo) && (m_pMediaParams))
	{
		MP_PARAMINFO mpi;
		MP_DATA md;

		MemsetZero(mpi);
		md = 0;
		if ((m_pParamInfo->GetParamInfo(index, &mpi) == S_OK)
		 && (m_pMediaParams->GetParam(index, &md) == S_OK))
		{
			float fValue, fMin, fMax, fDefault;

			fValue = md;
			fMin = mpi.mpdMinValue;
			fMax = mpi.mpdMaxValue;
			fDefault = mpi.mpdNeutralValue;
			if (mpi.mpType == MPT_BOOL)
			{
				fMin = 0;
				fMax = 1;
			}
			fValue -= fMin;
			if (fMax > fMin) fValue /= (fMax - fMin);
			return fValue;
		}
	}
	return 0;
}


void CDmo2Vst::Interleave(const float *pinL, const float *pinR, int nsamples)
//---------------------------------------------------------------------------
{
	float *pout = m_pMixBuffer;
	for(int i = nsamples; i != 0; i--)
	{
		*(pout++) = *(pinL++);
		*(pout++) = *(pinR++);
	}
}


void CDmo2Vst::Deinterleave(float *poutL, float *poutR, int nsamples)
//-------------------------------------------------------------------
{
	const float *pin = m_pMixBuffer;
	for(int i = nsamples; i != 0; i--)
	{
		*(poutL++) = *(pin++);
		*(poutR++) = *(pin++);
	}
}


void CDmo2Vst::Process(float * const *inputs, float **outputs, long sampleframes)
//-------------------------------------------------------------------------------
{
	if ((!m_pMixBuffer) || (sampleframes <= 0))
		return;

	Interleave(inputs[0], inputs[1], sampleframes);
	m_pMediaProcess->Process(sampleframes * 2 * sizeof(float), (LPBYTE)m_pMixBuffer, m_DataTime, DMO_INPLACE_NORMAL);
	Deinterleave(outputs[0], outputs[1], sampleframes);

	m_DataTime += _muldiv(sampleframes, 10000000, m_nSamplesPerSec);
}


AEffect *DmoToVst(PVSTPLUGINLIB pLib)
//-----------------------------------
{
	WCHAR w[100];
	CLSID clsid;

	MultiByteToWideChar(CP_ACP, 0, (LPCSTR)pLib->szDllPath, -1, (LPWSTR)w, CountOf(w));
	w[99] = 0;
	if (CLSIDFromString(w, &clsid) == S_OK)
	{
		IMediaObject *pMO = NULL;
		IMediaObjectInPlace *pMOIP = NULL;
		if ((CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, IID_IMediaObject, (VOID **)&pMO) == S_OK) && (pMO))
		{
			if (pMO->QueryInterface(IID_IMediaObjectInPlace, (void **)&pMOIP) != S_OK) pMOIP = NULL;
		} else pMO = NULL;
		if ((pMO) && (pMOIP))
		{
			DWORD dwInputs, dwOutputs;

			dwInputs = dwOutputs = 0;
			pMO->GetStreamCount(&dwInputs, &dwOutputs);
			if (dwInputs == 1 && dwOutputs == 1)
			{
				CDmo2Vst *p = new CDmo2Vst(pMO, pMOIP, clsid.Data1);
				return (p) ? p->GetEffect() : NULL;
			}
		#ifdef DMO_LOG
			Log("%s: Unable to use this DMO\n", pLib->szLibraryName);
		#endif
		}
	#ifdef DMO_LOG
		else Log("%s: Failed to get IMediaObject & IMediaObjectInPlace interfaces\n", pLib->szLibraryName);
	#endif
		if (pMO) pMO->Release();
		if (pMOIP) pMOIP->Release();
	}
	return NULL;
}


#endif // NO_VST

const char* SNDMIXPLUGIN::GetLibraryName()
//----------------------------------------
{
	StringFixer::SetNullTerminator(Info.szLibraryName);
	if(Info.szLibraryName[0]) return Info.szLibraryName;
	else return 0;
}


CString SNDMIXPLUGIN::GetParamName(const UINT index) const
//--------------------------------------------------------
{
	if(pMixPlugin)
	{
		return reinterpret_cast<CVstPlugin *>(pMixPlugin)->GetParamName(index);
	}
	else
	{
		return CString();
	}
}


bool SNDMIXPLUGIN::Bypass(bool bypass)
//------------------------------------
{
	if(pMixPlugin)
	{
		return pMixPlugin->Bypass(bypass);
	} else
	{
		// No plugin loaded, just toggle the flags.
		if(bypass)
		{
			Info.dwInputRouting |= MIXPLUG_INPUTF_BYPASS;
		} else
		{
			Info.dwInputRouting &= ~MIXPLUG_INPUTF_BYPASS;
		}
		return bypass;
	}
}
