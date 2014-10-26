/*
 * view_ins.cpp
 * ------------
 * Purpose: Instrument tab, lower panel.
 * Notes  : (currently none)
 * Authors: Olivier Lapicque
 *          OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"
#include "Mptrack.h"
#include "Mainfrm.h"
#include "InputHandler.h"
#include "Childfrm.h"
#include "Moddoc.h"
#include "globals.h"
#include "ctrl_ins.h"
#include "view_ins.h"
#include "Dlsbank.h"
#include "channelManagerDlg.h"
#include "ScaleEnvPointsDlg.h"
#include "view_ins.h"
#include "../soundlib/MIDIEvents.h"
#include "../common/StringFixer.h"


OPENMPT_NAMESPACE_BEGIN


#define ENV_ZOOM				4.0f
#define ENV_MIN_ZOOM			2.0f
#define ENV_MAX_ZOOM			100.0f
#define ENV_DRAGLOOPSTART		(MAX_ENVPOINTS + 1)
#define ENV_DRAGLOOPEND			(MAX_ENVPOINTS + 2)
#define ENV_DRAGSUSTAINSTART	(MAX_ENVPOINTS + 3)
#define ENV_DRAGSUSTAINEND		(MAX_ENVPOINTS + 4)

// Non-client toolbar
#define ENV_LEFTBAR_CY			29
#define ENV_LEFTBAR_CXSEP		14
#define ENV_LEFTBAR_CXSPC		3
#define ENV_LEFTBAR_CXBTN		24
#define ENV_LEFTBAR_CYBTN		22


const UINT cLeftBarButtons[ENV_LEFTBAR_BUTTONS] =
{
	ID_ENVSEL_VOLUME,
	ID_ENVSEL_PANNING,
	ID_ENVSEL_PITCH,
		ID_SEPARATOR,
	ID_ENVELOPE_VOLUME,
	ID_ENVELOPE_PANNING,
	ID_ENVELOPE_PITCH,
	ID_ENVELOPE_FILTER,
		ID_SEPARATOR,
	ID_ENVELOPE_SETLOOP,
	ID_ENVELOPE_SUSTAIN,
	ID_ENVELOPE_CARRY,
		ID_SEPARATOR,
	ID_INSTRUMENT_SAMPLEMAP,
		ID_SEPARATOR,
	ID_ENVELOPE_VIEWGRID,			//rewbs.envRowGrid
		ID_SEPARATOR,
	ID_ENVELOPE_ZOOM_IN,
	ID_ENVELOPE_ZOOM_OUT,
};


IMPLEMENT_SERIAL(CViewInstrument, CModScrollView, 0)

BEGIN_MESSAGE_MAP(CViewInstrument, CModScrollView)
	//{{AFX_MSG_MAP(CViewInstrument)
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_NCCALCSIZE()
	ON_WM_NCPAINT()
	ON_WM_NCHITTEST()
	ON_WM_MOUSEMOVE()
	ON_WM_NCMOUSEMOVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_RBUTTONDOWN()
	ON_WM_MBUTTONDOWN()
	ON_WM_NCLBUTTONDOWN()
	ON_WM_NCLBUTTONUP()
	ON_WM_NCLBUTTONDBLCLK()
	ON_WM_CHAR()
	ON_WM_KEYUP()
	ON_WM_DROPFILES()
	ON_COMMAND(ID_PREVINSTRUMENT,			OnPrevInstrument)
	ON_COMMAND(ID_NEXTINSTRUMENT,			OnNextInstrument)
	ON_COMMAND(ID_ENVELOPE_SETLOOP,			OnEnvLoopChanged)
	ON_COMMAND(ID_ENVELOPE_SUSTAIN,			OnEnvSustainChanged)
	ON_COMMAND(ID_ENVELOPE_CARRY,			OnEnvCarryChanged)
	ON_COMMAND(ID_ENVELOPE_INSERTPOINT,		OnEnvInsertPoint)
	ON_COMMAND(ID_ENVELOPE_REMOVEPOINT,		OnEnvRemovePoint)
	ON_COMMAND(ID_ENVELOPE_VOLUME,			OnEnvVolChanged)
	ON_COMMAND(ID_ENVELOPE_PANNING,			OnEnvPanChanged)
	ON_COMMAND(ID_ENVELOPE_PITCH,			OnEnvPitchChanged)
	ON_COMMAND(ID_ENVELOPE_FILTER,			OnEnvFilterChanged)
	ON_COMMAND(ID_ENVELOPE_VIEWGRID,		OnEnvToggleGrid) //rewbs.envRowGrid
	ON_COMMAND(ID_ENVELOPE_ZOOM_IN,			OnEnvZoomIn)
	ON_COMMAND(ID_ENVELOPE_ZOOM_OUT,		OnEnvZoomOut)
	ON_COMMAND(ID_ENVSEL_VOLUME,			OnSelectVolumeEnv)
	ON_COMMAND(ID_ENVSEL_PANNING,			OnSelectPanningEnv)
	ON_COMMAND(ID_ENVSEL_PITCH,				OnSelectPitchEnv)
	ON_COMMAND(ID_EDIT_COPY,				OnEditCopy)
	ON_COMMAND(ID_EDIT_PASTE,				OnEditPaste)
	ON_COMMAND(ID_INSTRUMENT_SAMPLEMAP,		OnEditSampleMap)
	ON_MESSAGE(WM_MOD_MIDIMSG,				OnMidiMsg)
	ON_MESSAGE(WM_MOD_KEYCOMMAND,	OnCustomKeyMsg) //rewbs.customKeys
	ON_COMMAND(ID_ENVELOPE_TOGGLERELEASENODE, OnEnvToggleReleasNode)
	//}}AFX_MSG_MAP
	ON_COMMAND(ID_ENVELOPE_SCALEPOINTS, OnEnvelopeScalepoints)
	ON_WM_MOUSEWHEEL()
END_MESSAGE_MAP()


///////////////////////////////////////////////////////////////
// CViewInstrument operations

CViewInstrument::CViewInstrument()
//--------------------------------
{
	m_nInstrument = 1;
	m_nEnv = ENV_VOLUME;
	m_rcClient.bottom = 2;
	m_dwStatus = 0;
	m_nBtnMouseOver = 0xFFFF;
	for(CHANNELINDEX i = 0; i < MAX_CHANNELS; i++)
	{
		m_dwNotifyPos[i] = (uint32)Notification::PosInvalid;
	}
	MemsetZero(m_NcButtonState);

	m_bmpEnvBar.Create(&CMainFrame::GetMainFrame()->m_EnvelopeIcons);

	m_baPlayingNote.reset();
	//rewbs.envRowGrid
	m_bGrid=true;
	m_bGridForceRedraw=false;
	m_GridSpeed = -1;
	m_GridScrollPos = -1;
	//end rewbs.envRowGrid
	m_nDragItem = 1;
	m_fZoom = ENV_ZOOM;
}


void CViewInstrument::OnInitialUpdate()
//-------------------------------------
{
	ModifyStyleEx(0, WS_EX_ACCEPTFILES);
	CModScrollView::OnInitialUpdate();
	UpdateScrollSize();
}


void CViewInstrument::UpdateScrollSize()
//--------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	GetClientRect(&m_rcClient);
	if (m_rcClient.bottom < 2) m_rcClient.bottom = 2;
	if (pModDoc)
	{
		SIZE sizeTotal, sizePage, sizeLine;
		UINT ntickmax = EnvGetTick(EnvGetLastPoint());

		sizeTotal.cx = (int)((ntickmax + 2) * m_fZoom);
		sizeTotal.cy = 1;
		sizeLine.cx = (int)m_fZoom;
		sizeLine.cy = 2;
		sizePage.cx = sizeLine.cx * 4;
		sizePage.cy = sizeLine.cy;
		SetScrollSizes(MM_TEXT, sizeTotal, sizePage, sizeLine);
	}
}


// Set instrument (and moddoc) as modified.
// updateAll: Update all views including this one. Otherwise, only update update other views.
void CViewInstrument::SetModified(DWORD mask, bool updateAll)
//-----------------------------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if(pModDoc == nullptr) return;
	pModDoc->UpdateAllViews(NULL, (m_nInstrument << HINT_SHIFT_INS) | mask, updateAll ? nullptr : this);
	pModDoc->SetModified();
}


BOOL CViewInstrument::SetCurrentInstrument(INSTRUMENTINDEX nIns, enmEnvelopeTypes nEnv)
//-------------------------------------------------------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	Notification::Type type;

	if ((!pModDoc) || (nIns < 1) || (nIns >= MAX_INSTRUMENTS)) return FALSE;
	m_nEnv = nEnv;
	m_nInstrument = nIns;
	switch(m_nEnv)
	{
	case ENV_PANNING:	type = Notification::PanEnv; break;
	case ENV_PITCH:		type = Notification::PitchEnv; break;
	default:			m_nEnv = ENV_VOLUME; type = Notification::VolEnv; break;
	}
	pModDoc->SetNotifications(type, m_nInstrument);
	pModDoc->SetFollowWnd(m_hWnd);
	UpdateScrollSize();
	UpdateNcButtonState();
	InvalidateRect(NULL, FALSE);
	return TRUE;
}


LRESULT CViewInstrument::OnModViewMsg(WPARAM wParam, LPARAM lParam)
//-----------------------------------------------------------------
{
	switch(wParam)
	{
	case VIEWMSG_SETCURRENTINSTRUMENT:
		SetCurrentInstrument(lParam & 0xFFFF, m_nEnv);
		break;

	case VIEWMSG_LOADSTATE:
		if (lParam)
		{
			INSTRUMENTVIEWSTATE *pState = (INSTRUMENTVIEWSTATE *)lParam;
			if (pState->cbStruct == sizeof(INSTRUMENTVIEWSTATE))
			{
				SetCurrentInstrument(m_nInstrument, pState->nEnv);
				m_bGrid = pState->bGrid;
			}
		}
		break;

	case VIEWMSG_SAVESTATE:
		if (lParam)
		{
			INSTRUMENTVIEWSTATE *pState = (INSTRUMENTVIEWSTATE *)lParam;
			pState->cbStruct = sizeof(INSTRUMENTVIEWSTATE);
			pState->nEnv = m_nEnv;
			pState->bGrid = m_bGrid;
		}
		break;

	default:
		return CModScrollView::OnModViewMsg(wParam, lParam);
	}
	return 0;
}


UINT CViewInstrument::EnvGetTick(int nPoint) const
//------------------------------------------------
{
	InstrumentEnvelope *envelope = GetEnvelopePtr();
	if(envelope == nullptr) return 0;
	if((nPoint >= 0) && (nPoint < (int)envelope->nNodes))
		return envelope->Ticks[nPoint];
	else
		return 0;
}


UINT CViewInstrument::EnvGetValue(int nPoint) const
//-------------------------------------------------
{
	InstrumentEnvelope *envelope = GetEnvelopePtr();
	if(envelope == nullptr) return 0;
	if(nPoint >= 0 && nPoint < (int)envelope->nNodes)
		return envelope->Values[nPoint];
	else
		return 0;
}


bool CViewInstrument::EnvSetValue(int nPoint, int nTick, int nValue, bool moveTail)
//---------------------------------------------------------------------------------
{
	InstrumentEnvelope *envelope = GetEnvelopePtr();
	if(envelope == nullptr || nPoint < 0) return false;

	if(nPoint == 0)
	{
		nTick = 0;
		moveTail = false;
	}
	int tickDiff = 0;

	bool ok = false;
	if(nPoint < (int)envelope->nNodes)
	{
		if(nTick >= 0)
		{
			tickDiff = envelope->Ticks[nPoint];
			int mintick = (nPoint) ? envelope->Ticks[nPoint - 1] : 0;
			int maxtick = envelope->Ticks[nPoint + 1];
			if(nPoint + 1 == (int)envelope->nNodes || moveTail) maxtick = Util::MaxValueOfType(maxtick);

			// Can't have multiple points on same tick
			if(nPoint > 0 && mintick < maxtick - 1)
			{
				mintick++;
				if(nPoint + 1 < (int)envelope->nNodes) maxtick--;
			}
			if(nTick < mintick) nTick = mintick;
			if(nTick > maxtick) nTick = maxtick;
			if(nTick != envelope->Ticks[nPoint])
			{
				envelope->Ticks[nPoint] = (uint16)nTick;
				ok = true;
			}
		}
		if(nValue >= 0)
		{
			const int maxVal = (GetDocument()->GetModType() != MOD_TYPE_XM || m_nEnv != ENV_PANNING) ? 64 : 63;
			LimitMax(nValue, maxVal);
			if(nValue != envelope->Values[nPoint])
			{
				envelope->Values[nPoint] = (uint8)nValue;
				ok = true;
			}
		}
	}

	if(ok && moveTail)
	{
		// Move all points after modified point as well.
		tickDiff = envelope->Ticks[nPoint] - tickDiff;
		for(uint32 i = nPoint + 1; i < envelope->nNodes; i++)
		{
			envelope->Ticks[i] = (uint16)(std::max(0, (int)envelope->Ticks[i] + tickDiff));
		}
	}

	return ok;
}


UINT CViewInstrument::EnvGetNumPoints() const
//-------------------------------------------
{
	InstrumentEnvelope *envelope = GetEnvelopePtr();
	if(envelope == nullptr) return 0;
	return envelope->nNodes;
}


UINT CViewInstrument::EnvGetLastPoint() const
//-------------------------------------------
{
	UINT nPoints = EnvGetNumPoints();
	if (nPoints > 0) return nPoints - 1;
	return 0;
}


// Return if an envelope flag is set.
bool CViewInstrument::EnvGetFlag(const EnvelopeFlags dwFlag) const
//----------------------------------------------------------------
{
	InstrumentEnvelope *pEnv = GetEnvelopePtr();
	if(pEnv != nullptr) return pEnv->dwFlags[dwFlag];
	return false;
}


UINT CViewInstrument::EnvGetLoopStart() const
//-------------------------------------------
{
	InstrumentEnvelope *envelope = GetEnvelopePtr();
	if(envelope == nullptr) return 0;
	return envelope->nLoopStart;
}


UINT CViewInstrument::EnvGetLoopEnd() const
//-----------------------------------------
{
	InstrumentEnvelope *envelope = GetEnvelopePtr();
	if(envelope == nullptr) return 0;
	return envelope->nLoopEnd;
}


UINT CViewInstrument::EnvGetSustainStart() const
//----------------------------------------------
{
	InstrumentEnvelope *envelope = GetEnvelopePtr();
	if(envelope == nullptr) return 0;
	return envelope->nSustainStart;
}


UINT CViewInstrument::EnvGetSustainEnd() const
//--------------------------------------------
{
	InstrumentEnvelope *envelope = GetEnvelopePtr();
	if(envelope == nullptr) return 0;
	return envelope->nSustainEnd;
}


bool CViewInstrument::EnvGetVolEnv() const
//----------------------------------------
{
	ModInstrument *pIns = GetInstrumentPtr();
	if (pIns) return pIns->VolEnv.dwFlags[ENV_ENABLED] != 0;
	return false;
}


bool CViewInstrument::EnvGetPanEnv() const
//----------------------------------------
{
	ModInstrument *pIns = GetInstrumentPtr();
	if (pIns) return pIns->PanEnv.dwFlags[ENV_ENABLED] != 0;
	return false;
}


bool CViewInstrument::EnvGetPitchEnv() const
//------------------------------------------
{
	ModInstrument *pIns = GetInstrumentPtr();
	if (pIns) return ((pIns->PitchEnv.dwFlags & (ENV_ENABLED | ENV_FILTER)) == ENV_ENABLED);
	return false;
}


bool CViewInstrument::EnvGetFilterEnv() const
//-------------------------------------------
{
	ModInstrument *pIns = GetInstrumentPtr();
	if(pIns) return ((pIns->PitchEnv.dwFlags & (ENV_ENABLED | ENV_FILTER)) == (ENV_ENABLED | ENV_FILTER));
	return false;
}


bool CViewInstrument::EnvSetLoopStart(int nPoint)
//-----------------------------------------------
{
	InstrumentEnvelope *envelope = GetEnvelopePtr();
	if(envelope == nullptr) return false;
	if(nPoint < 0 || nPoint > (int)EnvGetLastPoint()) return false;

	if (nPoint != envelope->nLoopStart)
	{
		envelope->nLoopStart = (BYTE)nPoint;
		if (envelope->nLoopEnd < nPoint) envelope->nLoopEnd = (BYTE)nPoint;
		return true;
	} else
	{
		return false;
	}
}


bool CViewInstrument::EnvSetLoopEnd(int nPoint)
//---------------------------------------------
{
	InstrumentEnvelope *envelope = GetEnvelopePtr();
	if(envelope == nullptr) return false;
	if(nPoint < 0 || nPoint > (int)EnvGetLastPoint()) return false;

	if (nPoint != envelope->nLoopEnd)
	{
		envelope->nLoopEnd = (BYTE)nPoint;
		if (envelope->nLoopStart > nPoint) envelope->nLoopStart = (BYTE)nPoint;
		return true;
	} else
	{
		return false;
	}
}


bool CViewInstrument::EnvSetSustainStart(int nPoint)
//--------------------------------------------------
{
	InstrumentEnvelope *envelope = GetEnvelopePtr();
	if(envelope == nullptr) return false;
	if(nPoint < 0 || nPoint > (int)EnvGetLastPoint()) return false;

	// We won't do any security checks here as GetEnvelopePtr() does that for us.
	CSoundFile &sndFile = GetDocument()->GetrSoundFile();

	if (nPoint != envelope->nSustainStart)
	{
		envelope->nSustainStart = (BYTE)nPoint;
		if ((envelope->nSustainEnd < nPoint) || (sndFile.GetType() & MOD_TYPE_XM)) envelope->nSustainEnd = (BYTE)nPoint;
		return true;
	} else
	{
		return false;
	}
}


bool CViewInstrument::EnvSetSustainEnd(int nPoint)
//------------------------------------------------
{
	InstrumentEnvelope *envelope = GetEnvelopePtr();
	if(envelope == nullptr) return false;
	if(nPoint < 0 || nPoint > (int)EnvGetLastPoint()) return false;

	// We won't do any security checks here as GetEnvelopePtr() does that for us.
	CSoundFile &sndFile = GetDocument()->GetrSoundFile();

	if (nPoint != envelope->nSustainEnd)
	{
		envelope->nSustainEnd = (BYTE)nPoint;
		if ((envelope->nSustainStart > nPoint) || (sndFile.GetType() & MOD_TYPE_XM)) envelope->nSustainStart = (BYTE)nPoint;
		return true;
	} else
	{
		return false;
	}
}


bool CViewInstrument::EnvToggleReleaseNode(int nPoint)
//----------------------------------------------------
{
	InstrumentEnvelope *envelope = GetEnvelopePtr();
	if(envelope == nullptr) return false;
	if(nPoint < 1 || nPoint > (int)EnvGetLastPoint()) return false;

	// Don't allow release nodes in IT/XM. GetDocument()/... nullptr check is done in GetEnvelopePtr, so no need to check twice.
	if(!GetDocument()->GetrSoundFile().GetModSpecifications().hasReleaseNode)
	{
		if(envelope->nReleaseNode != ENV_RELEASE_NODE_UNSET)
		{
			envelope->nReleaseNode = ENV_RELEASE_NODE_UNSET;
			return true;
		}
		return false;
	}

	if (envelope->nReleaseNode == nPoint)
	{
		envelope->nReleaseNode = ENV_RELEASE_NODE_UNSET;
	} else
	{
		envelope->nReleaseNode = static_cast<BYTE>(nPoint);
	}
	return true;
}

// Enable or disable a flag of the current envelope
bool CViewInstrument::EnvSetFlag(EnvelopeFlags flag, bool enable) const
//---------------------------------------------------------------------
{
	InstrumentEnvelope *envelope = GetEnvelopePtr();
	if(envelope == nullptr || envelope->nNodes == 0) return false;

	bool modified = envelope->dwFlags[flag] != enable;
	envelope->dwFlags.set(flag, enable);
	return modified;
}


bool CViewInstrument::EnvToggleEnv(enmEnvelopeTypes envelope, CSoundFile &sndFile, ModInstrument &ins, bool enable, BYTE defaultValue, EnvelopeFlags extraFlags)
//--------------------------------------------------------------------------------------------------------------------------------------------------------------
{
	InstrumentEnvelope &env = ins.GetEnvelope(envelope);

	const EnvelopeFlags flags = (ENV_ENABLED | extraFlags);

	env.dwFlags.set(flags, enable);
	if(enable && !env.nNodes)
	{
		env.Values[0] = env.Values[1] = defaultValue;
		env.Ticks[0] = 0;
		env.Ticks[1] = 10;
		env.nNodes = 2;
		InvalidateRect(NULL, FALSE);
	}

	CriticalSection cs;

	// Update mixing flags...
	for(CHANNELINDEX nChn = 0; nChn < MAX_CHANNELS; nChn++)
	{
		if(sndFile.m_PlayState.Chn[nChn].pModInstrument == &ins)
		{
			sndFile.m_PlayState.Chn[nChn].GetEnvelope(envelope).flags.set(flags, enable);
		}
	}

	return true;
}


bool CViewInstrument::EnvSetVolEnv(bool bEnable)
//----------------------------------------------
{
	ModInstrument *pIns = GetInstrumentPtr();
	if(pIns == nullptr) return false;
	return EnvToggleEnv(ENV_VOLUME, GetDocument()->GetrSoundFile(), *pIns, bEnable, 64);
}


bool CViewInstrument::EnvSetPanEnv(bool bEnable)
//----------------------------------------------
{
	ModInstrument *pIns = GetInstrumentPtr();
	if(pIns == nullptr) return false;
	return EnvToggleEnv(ENV_PANNING, GetDocument()->GetrSoundFile(), *pIns, bEnable, 32);
}


bool CViewInstrument::EnvSetPitchEnv(bool bEnable)
//------------------------------------------------
{
	ModInstrument *pIns = GetInstrumentPtr();
	if(pIns == nullptr) return false;

	pIns->PitchEnv.dwFlags.reset(ENV_FILTER);
	return EnvToggleEnv(ENV_PITCH, GetDocument()->GetrSoundFile(), *pIns, bEnable, 32);
}


bool CViewInstrument::EnvSetFilterEnv(bool bEnable)
//-------------------------------------------------
{
	ModInstrument *pIns = GetInstrumentPtr();
	if(pIns == nullptr) return false;

	return EnvToggleEnv(ENV_PITCH, GetDocument()->GetrSoundFile(), *pIns, bEnable, 64, ENV_FILTER);
}


int CViewInstrument::TickToScreen(int nTick) const
//------------------------------------------------
{
	return ((int)((nTick + 1) * m_fZoom)) - m_nScrollPosX;
}

int CViewInstrument::PointToScreen(int nPoint) const
//--------------------------------------------------
{
	return TickToScreen(EnvGetTick(nPoint));
}


int CViewInstrument::ScreenToTick(int x) const
//--------------------------------------------
{
	return (int)(((float)m_nScrollPosX + (float)x + 1 - m_fZoom) / m_fZoom);
}


int CViewInstrument::ScreenToValue(int y) const
//---------------------------------------------
{
	if (m_rcClient.bottom < 2) return 0;
	int n = 64 - ((y * 64 + 1) / (m_rcClient.bottom - 1));
	if (n < 0) return 0;
	if (n > 64) return 64;
	return n;
}


int CViewInstrument::ScreenToPoint(int x0, int y0) const
//------------------------------------------------------
{
	int nPoint = -1;
	int ydist = 0xFFFF, xdist = 0xFFFF;
	int maxpoint = EnvGetLastPoint();
	for (int i=0; i<=maxpoint; i++)
	{
		int dx = x0 - PointToScreen(i);
		int dx2 = dx*dx;
		if (dx2 <= xdist)
		{
			int dy = y0 - ValueToScreen(EnvGetValue(i));
			int dy2 = dy*dy;
			if ((dx2 < xdist) || ((dx2 == xdist) && (dy2 < ydist)))
			{
				nPoint = i;
				xdist = dx2;
				ydist = dy2;
			}
		}
	}
	return nPoint;
}


BOOL CViewInstrument::GetNcButtonRect(UINT nBtn, LPRECT lpRect)
//-------------------------------------------------------------
{
	lpRect->left = 4;
	lpRect->top = 3;
	lpRect->bottom = lpRect->top + ENV_LEFTBAR_CYBTN;
	if (nBtn >= ENV_LEFTBAR_BUTTONS) return FALSE;
	for (UINT i=0; i<nBtn; i++)
	{
		if (cLeftBarButtons[i] == ID_SEPARATOR)
		{
			lpRect->left += ENV_LEFTBAR_CXSEP;
		} else
		{
			lpRect->left += ENV_LEFTBAR_CXBTN + ENV_LEFTBAR_CXSPC;
		}
	}
	if (cLeftBarButtons[nBtn] == ID_SEPARATOR)
	{
		lpRect->left += ENV_LEFTBAR_CXSEP/2 - 2;
		lpRect->right = lpRect->left + 2;
		return FALSE;
	} else
	{
		lpRect->right = lpRect->left + ENV_LEFTBAR_CXBTN;
	}
	return TRUE;
}


void CViewInstrument::UpdateNcButtonState()
//-----------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if(!pModDoc) return;
	CSoundFile &sndFile = pModDoc->GetrSoundFile();

	CDC *pDC = NULL;
	for (UINT i=0; i<ENV_LEFTBAR_BUTTONS; i++) if (cLeftBarButtons[i] != ID_SEPARATOR)
	{
		DWORD dwStyle = 0;

		switch(cLeftBarButtons[i])
		{
		case ID_ENVSEL_VOLUME:		if (m_nEnv == ENV_VOLUME) dwStyle |= NCBTNS_CHECKED; break;
		case ID_ENVSEL_PANNING:		if (m_nEnv == ENV_PANNING) dwStyle |= NCBTNS_CHECKED; break;
		case ID_ENVSEL_PITCH:		if (!(sndFile.GetType() & (MOD_TYPE_IT|MOD_TYPE_MPT))) dwStyle |= NCBTNS_DISABLED;
									else if (m_nEnv == ENV_PITCH) dwStyle |= NCBTNS_CHECKED; break;
		case ID_ENVELOPE_SETLOOP:	if (EnvGetLoop()) dwStyle |= NCBTNS_CHECKED; break;
		case ID_ENVELOPE_SUSTAIN:	if (EnvGetSustain()) dwStyle |= NCBTNS_CHECKED; break;
		case ID_ENVELOPE_CARRY:		if (!(sndFile.GetType() & (MOD_TYPE_IT|MOD_TYPE_MPT))) dwStyle |= NCBTNS_DISABLED;
									else if (EnvGetCarry()) dwStyle |= NCBTNS_CHECKED; break;
		case ID_ENVELOPE_VOLUME:	if (EnvGetVolEnv()) dwStyle |= NCBTNS_CHECKED; break;
		case ID_ENVELOPE_PANNING:	if (EnvGetPanEnv()) dwStyle |= NCBTNS_CHECKED; break;
		case ID_ENVELOPE_PITCH:		if (!(sndFile.GetType() & (MOD_TYPE_IT|MOD_TYPE_MPT))) dwStyle |= NCBTNS_DISABLED; else
									if (EnvGetPitchEnv()) dwStyle |= NCBTNS_CHECKED; break;
		case ID_ENVELOPE_FILTER:	if (!(sndFile.GetType() & (MOD_TYPE_IT|MOD_TYPE_MPT))) dwStyle |= NCBTNS_DISABLED; else
									if (EnvGetFilterEnv()) dwStyle |= NCBTNS_CHECKED; break;
		case ID_ENVELOPE_VIEWGRID:	if (m_bGrid) dwStyle |= NCBTNS_CHECKED; break;
		case ID_ENVELOPE_ZOOM_IN:	if (m_fZoom >= ENV_MAX_ZOOM) dwStyle |= NCBTNS_DISABLED; break;
		case ID_ENVELOPE_ZOOM_OUT:	if (m_fZoom <= ENV_MIN_ZOOM) dwStyle |= NCBTNS_DISABLED; break;
		}
		if (m_nBtnMouseOver == i)
		{
			dwStyle |= NCBTNS_MOUSEOVER;
			if (m_dwStatus & INSSTATUS_NCLBTNDOWN) dwStyle |= NCBTNS_PUSHED;
		}
		if (dwStyle != m_NcButtonState[i])
		{
			m_NcButtonState[i] = dwStyle;
			if (!pDC) pDC = GetWindowDC();
			DrawNcButton(pDC, i);
		}
	}
	if (pDC) ReleaseDC(pDC);
}


////////////////////////////////////////////////////////////////////
// CViewInstrument drawing

void CViewInstrument::UpdateView(DWORD dwHintMask, CObject *hint)
//---------------------------------------------------------------
{
	if(hint == this)
	{
		return;
	}
	const INSTRUMENTINDEX updateIns = (dwHintMask >> HINT_SHIFT_INS);
	if((dwHintMask & (HINT_MPTOPTIONS | HINT_MODTYPE))
		|| ((dwHintMask & HINT_ENVELOPE) && (m_nInstrument == updateIns || updateIns == 0))
		|| ((dwHintMask & HINT_SPEEDCHANGE))) //rewbs.envRowGrid
	{
		UpdateScrollSize();
		UpdateNcButtonState();
		InvalidateRect(NULL, FALSE);
	}
}

//rewbs.envRowGrid
void CViewInstrument::DrawGrid(CDC *pDC, UINT speed)
//--------------------------------------------------
{
	bool windowResized = false;

	if (m_dcGrid.GetSafeHdc())
	{
		m_dcGrid.SelectObject(m_pbmpOldGrid);
		m_dcGrid.DeleteDC();
		m_bmpGrid.DeleteObject();
		windowResized = true;
	}


	if (windowResized || m_bGridForceRedraw || (m_nScrollPosX != m_GridScrollPos) || (speed != (UINT)m_GridSpeed))
	{

		m_GridSpeed = speed;
		m_GridScrollPos = m_nScrollPosX;
		m_bGridForceRedraw = false;

		// create a memory based dc for drawing the grid
		m_dcGrid.CreateCompatibleDC(pDC);
		m_bmpGrid.CreateCompatibleBitmap(pDC,  m_rcClient.Width(), m_rcClient.Height());
		m_pbmpOldGrid = m_dcGrid.SelectObject(&m_bmpGrid);

		//do draw
		int width = m_rcClient.Width();
		int nPrevTick = -1;
		int nTick, nRow;
		int nRowsPerBeat = 1, nRowsPerMeasure = 1;
		CModDoc *modDoc = GetDocument();
		if(modDoc != nullptr)
		{
			nRowsPerBeat = modDoc->GetrSoundFile().m_nDefaultRowsPerBeat;
			nRowsPerMeasure = modDoc->GetrSoundFile().m_nDefaultRowsPerMeasure;
		}

		// Paint it black!
		m_dcGrid.FillSolidRect(&m_rcClient, 0);

		for (int x = 3; x < width; x++)
		{
			nTick = ScreenToTick(x);
			if (nTick != nPrevTick && !(nTick%speed))
			{
				nPrevTick = nTick;
				nRow = nTick / speed;

				if (nRowsPerMeasure > 0 && nRow % nRowsPerMeasure == 0)
					m_dcGrid.SelectObject(CMainFrame::penGray80);
				else if (nRowsPerBeat > 0 && nRow % nRowsPerBeat == 0)
					m_dcGrid.SelectObject(CMainFrame::penGray55);
				else
					m_dcGrid.SelectObject(CMainFrame::penGray33);

				m_dcGrid.MoveTo(x+1, 0);
				m_dcGrid.LineTo(x+1, m_rcClient.bottom);

			}
		}
	}

	pDC->BitBlt(m_rcClient.left, m_rcClient.top, m_rcClient.Width(), m_rcClient.Height(), &m_dcGrid, 0, 0, SRCCOPY);
}
//end rewbs.envRowGrid

void CViewInstrument::OnDraw(CDC *pDC)
//------------------------------------
{
	RECT rect;
	int nScrollPos = m_nScrollPosX;
	CModDoc *pModDoc = GetDocument();
	HGDIOBJ oldpen;
	//HDC hdc;
	UINT maxpoint;
	int ymed = (m_rcClient.bottom - 1) / 2;

//rewbs.envRowGrid
	// to avoid flicker, establish a memory dc, draw to it
	// and then BitBlt it to the destination "pDC"

	//check for window resize
	if (m_dcMemMain.GetSafeHdc())
	{
		m_dcMemMain.SelectObject(oldBitmap);
		m_dcMemMain.DeleteDC();
		m_bmpMemMain.DeleteObject();
	}

	m_dcMemMain.CreateCompatibleDC(pDC);
	if (!m_dcMemMain)
		return;
	m_bmpMemMain.CreateCompatibleBitmap(pDC, m_rcClient.right-m_rcClient.left, m_rcClient.bottom-m_rcClient.top);
	oldBitmap = (CBitmap *)m_dcMemMain.SelectObject(&m_bmpMemMain);

//end rewbs.envRowGrid

	if ((!pModDoc) || (!pDC)) return;
	//hdc = pDC->m_hDC;
	oldpen = m_dcMemMain.SelectObject(CMainFrame::penDarkGray);
	if (m_bGrid)
	{
		DrawGrid(&m_dcMemMain, pModDoc->GetrSoundFile().m_PlayState.m_nMusicSpeed);
	} else
	{
		// Paint it black!
		m_dcMemMain.FillSolidRect(&m_rcClient, 0);
	}

	// Middle line (half volume or pitch / panning center)
	m_dcMemMain.MoveTo(0, ymed);
	m_dcMemMain.LineTo(m_rcClient.right, ymed);

	m_dcMemMain.SelectObject(CMainFrame::penDarkGray);
	// Drawing Loop Start/End
	if (EnvGetLoop())
	{
		int x1 = PointToScreen(EnvGetLoopStart()) - (int)(m_fZoom / 2);
		m_dcMemMain.MoveTo(x1, 0);
		m_dcMemMain.LineTo(x1, m_rcClient.bottom);
		int x2 = PointToScreen(EnvGetLoopEnd()) + (int)(m_fZoom / 2);
		m_dcMemMain.MoveTo(x2, 0);
		m_dcMemMain.LineTo(x2, m_rcClient.bottom);
	}
	// Drawing Sustain Start/End
	if (EnvGetSustain())
	{
		m_dcMemMain.SelectObject(CMainFrame::penHalfDarkGray);
		int nspace = m_rcClient.bottom/4;
		int n1 = EnvGetSustainStart();
		int x1 = PointToScreen(n1) - (int)(m_fZoom / 2);
		int y1 = ValueToScreen(EnvGetValue(n1));
		m_dcMemMain.MoveTo(x1, y1 - nspace);
		m_dcMemMain.LineTo(x1, y1+nspace);
		int n2 = EnvGetSustainEnd();
		int x2 = PointToScreen(n2) + (int)(m_fZoom / 2);
		int y2 = ValueToScreen(EnvGetValue(n2));
		m_dcMemMain.MoveTo(x2, y2-nspace);
		m_dcMemMain.LineTo(x2, y2+nspace);
	}
	maxpoint = EnvGetNumPoints();
	// Drawing Envelope
	if (maxpoint)
	{
		maxpoint--;
		m_dcMemMain.SelectObject(CMainFrame::penEnvelope);
		UINT releaseNode = EnvGetReleaseNode();
		for (UINT i=0; i<=maxpoint; i++)
		{
			int x = (int)((EnvGetTick(i) + 1) * m_fZoom) - nScrollPos;
			int y = ValueToScreen(EnvGetValue(i));
			rect.left = x - 3;
			rect.top = y - 3;
			rect.right = x + 4;
			rect.bottom = y + 4;
			if (i)
			{
				m_dcMemMain.LineTo(x, y);
			} else
			{
				m_dcMemMain.MoveTo(x, y);
			}
			if (i == releaseNode)
			{
				m_dcMemMain.FrameRect(&rect, CBrush::FromHandle(CMainFrame::brushHighLightRed));
				m_dcMemMain.SelectObject(CMainFrame::penEnvelopeHighlight);
			} else if (i == m_nDragItem - 1)
			{
				// currently selected env point
				m_dcMemMain.FrameRect(&rect, CBrush::FromHandle(CMainFrame::brushYellow));
			} else
			{
				m_dcMemMain.FrameRect(&rect, CBrush::FromHandle(CMainFrame::brushWhite));
			}

		}
	}
	DrawPositionMarks();
	if (oldpen) m_dcMemMain.SelectObject(oldpen);

	//rewbs.envRowGrid
	pDC->BitBlt(m_rcClient.left, m_rcClient.top, m_rcClient.Width(), m_rcClient.Height(), &m_dcMemMain, 0, 0, SRCCOPY);

// -> CODE#0015
// -> DESC="channels management dlg"
	CMainFrame * pMainFrm = CMainFrame::GetMainFrame();
	BOOL activeDoc = pMainFrm ? pMainFrm->GetActiveDoc() == GetDocument() : FALSE;

	if(activeDoc && CChannelManagerDlg::sharedInstance(FALSE) && CChannelManagerDlg::sharedInstance()->IsDisplayed())
		CChannelManagerDlg::sharedInstance()->SetDocument((void*)this);

// -! NEW_FEATURE#0015
}


uint8 CViewInstrument::EnvGetReleaseNode()
//----------------------------------------
{
	InstrumentEnvelope *envelope = GetEnvelopePtr();
	if(envelope == nullptr) return ENV_RELEASE_NODE_UNSET;
	return envelope->nReleaseNode;
}


uint16 CViewInstrument::EnvGetReleaseNodeValue()
//----------------------------------------------
{
	InstrumentEnvelope *envelope = GetEnvelopePtr();
	if(envelope == nullptr) return 0;
	return envelope->Values[EnvGetReleaseNode()];
}


uint16 CViewInstrument::EnvGetReleaseNodeTick()
//---------------------------------------------
{
	InstrumentEnvelope *envelope = GetEnvelopePtr();
	if(envelope == nullptr) return 0;
	return envelope->Ticks[EnvGetReleaseNode()];
}


bool CViewInstrument::EnvRemovePoint(UINT nPoint)
//---------------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if ((pModDoc) && (nPoint <= EnvGetLastPoint()))
	{
		ModInstrument *pIns = pModDoc->GetrSoundFile().Instruments[m_nInstrument];
		if (pIns)
		{
			InstrumentEnvelope *envelope = GetEnvelopePtr();
			if(envelope == nullptr || envelope->nNodes == 0) return false;

			envelope->nNodes--;
			for (UINT i = nPoint; i < envelope->nNodes; i++)
			{
				envelope->Ticks[i] = envelope->Ticks[i + 1];
				envelope->Values[i] = envelope->Values[i + 1];
			}
			if (nPoint >= envelope->nNodes) nPoint = envelope->nNodes - 1;
			if (envelope->nLoopStart > nPoint) envelope->nLoopStart--;
			if (envelope->nLoopEnd > nPoint) envelope->nLoopEnd--;
			if (envelope->nSustainStart > nPoint) envelope->nSustainStart--;
			if (envelope->nSustainEnd > nPoint) envelope->nSustainEnd--;
			if (envelope->nReleaseNode>nPoint && envelope->nReleaseNode != ENV_RELEASE_NODE_UNSET) envelope->nReleaseNode--;
			envelope->Ticks[0] = 0;

			if(envelope->nNodes <= 1)
			{
				// if only one node is left, just disable the envelope completely
				envelope->nNodes = envelope->nLoopStart = envelope->nLoopEnd = envelope->nSustainStart = envelope->nSustainEnd = 0;
				envelope->dwFlags.reset();
				envelope->nReleaseNode = ENV_RELEASE_NODE_UNSET;
			}

			SetModified(HINT_ENVELOPE, true);
			return true;
		}
	}
	return false;
}


// Insert point. Returns 0 if error occured, else point ID + 1.
UINT CViewInstrument::EnvInsertPoint(int nTick, int nValue)
//---------------------------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if (pModDoc)
	{
		CSoundFile &sndFile = pModDoc->GetrSoundFile();
		ModInstrument *pIns = sndFile.Instruments[m_nInstrument];
		if (pIns)
		{
			if(nTick < 0) return 0;
			nValue = Clamp(nValue, 0, 64);

			InstrumentEnvelope *envelope = GetEnvelopePtr();
			if(envelope == nullptr) return 0;

			if(std::binary_search(envelope->Ticks, envelope->Ticks + envelope->nNodes, nTick))
			{
				// Don't want to insert a node at the same position as another node.
				return 0;
			}


			uint8 defaultValue;

			switch(m_nEnv)
			{
			case ENV_VOLUME:
				defaultValue = 64;
				break;
			case ENV_PANNING:
				defaultValue = 32;
				break;
			case ENV_PITCH:
				defaultValue = (pIns->PitchEnv.dwFlags & ENV_FILTER) ? 64 : 32;
				break;
			default:
				return 0;
			}

			if (envelope->nNodes < sndFile.GetModSpecifications().envelopePointsMax)
			{
				if (!envelope->nNodes)
				{
					envelope->Ticks[0] = 0;
					envelope->Values[0] = defaultValue;
					envelope->nNodes = 1;
					envelope->dwFlags.set(ENV_ENABLED);
				}
				uint32 i = 0;
				for (i = 0; i < envelope->nNodes; i++) if (nTick <= envelope->Ticks[i]) break;
				for (uint32 j = envelope->nNodes; j > i; j--)
				{
					envelope->Ticks[j] = envelope->Ticks[j - 1];
					envelope->Values[j] = envelope->Values[j - 1];
				}
				envelope->Ticks[i] = (WORD)nTick;
				envelope->Values[i] = (BYTE)nValue;
				envelope->nNodes++;
				if (envelope->nLoopStart >= i) envelope->nLoopStart++;
				if (envelope->nLoopEnd >= i) envelope->nLoopEnd++;
				if (envelope->nSustainStart >= i) envelope->nSustainStart++;
				if (envelope->nSustainEnd >= i) envelope->nSustainEnd++;
				if (envelope->nReleaseNode >= i && envelope->nReleaseNode != ENV_RELEASE_NODE_UNSET) envelope->nReleaseNode++;

				SetModified(HINT_ENVELOPE, true);
				return i + 1;
			}
		}
	}
	return 0;
}



void CViewInstrument::DrawPositionMarks()
//---------------------------------------
{
	CRect rect;
	for(UINT i = 0; i < MAX_CHANNELS; i++) if (m_dwNotifyPos[i] != Notification::PosInvalid)
	{
		rect.top = -2;
		rect.left = TickToScreen(m_dwNotifyPos[i]);
		rect.right = rect.left + 1;
		rect.bottom = m_rcClient.bottom + 1;
		InvertRect(m_dcMemMain.m_hDC, &rect);
	}
}


LRESULT CViewInstrument::OnPlayerNotify(Notification *pnotify)
//------------------------------------------------------------
{
	Notification::Type type;
	CModDoc *pModDoc = GetDocument();
	if ((!pnotify) || (!pModDoc)) return 0;
	switch(m_nEnv)
	{
	case ENV_PANNING:	type = Notification::PanEnv; break;
	case ENV_PITCH:		type = Notification::PitchEnv; break;
	default:			type = Notification::VolEnv; break;
	}
	if (pnotify->type[Notification::Stop])
	{
		bool invalidate = false;
		for(CHANNELINDEX i = 0; i < MAX_CHANNELS; i++)
		{
			if(m_dwNotifyPos[i] != (uint32)Notification::PosInvalid)
			{
				m_dwNotifyPos[i] = (uint32)Notification::PosInvalid;
				invalidate = true;
			}
		}
		if(invalidate)
		{
			InvalidateEnvelope();
		}
		m_baPlayingNote.reset();
	} else if(pnotify->type[type] && pnotify->item == m_nInstrument)
	{
		BOOL bUpdate = FALSE;
		for (CHANNELINDEX i = 0; i < MAX_CHANNELS; i++)
		{
			uint32 newpos = (uint32)pnotify->pos[i];
			if (m_dwNotifyPos[i] != newpos)
			{
				bUpdate = TRUE;
				break;
			}
		}
		if (bUpdate)
		{
			HDC hdc = ::GetDC(m_hWnd);
			DrawPositionMarks();
			for (CHANNELINDEX j = 0; j < MAX_CHANNELS; j++)
			{
				//DWORD newpos = (pSndFile->m_SongFlags[SONG_PAUSED]) ? pnotify->dwPos[j] : 0;
				uint32 newpos = (uint32)pnotify->pos[j];
				m_dwNotifyPos[j] = newpos;
			}
			DrawPositionMarks();
			BitBlt(hdc, m_rcClient.left, m_rcClient.top, m_rcClient.Width(), m_rcClient.Height(), m_dcMemMain.GetSafeHdc(), 0, 0, SRCCOPY);
			::ReleaseDC(m_hWnd, hdc);
		}
	}
	return 0;
}


void CViewInstrument::DrawNcButton(CDC *pDC, UINT nBtn)
//-----------------------------------------------------
{
	CRect rect;
	COLORREF crHi = GetSysColor(COLOR_3DHILIGHT);
	COLORREF crDk = GetSysColor(COLOR_3DSHADOW);
	COLORREF crFc = GetSysColor(COLOR_3DFACE);
	COLORREF c1, c2;

	if (GetNcButtonRect(nBtn, &rect))
	{
		DWORD dwStyle = m_NcButtonState[nBtn];
		COLORREF c3, c4;
		int xofs = 0, yofs = 0, nImage = 0;

		c1 = c2 = c3 = c4 = crFc;
		if (!(TrackerSettings::Instance().m_dwPatternSetup & PATTERN_FLATBUTTONS))
		{
			c1 = c3 = crHi;
			c2 = crDk;
			c4 = RGB(0,0,0);
		}
		if (dwStyle & (NCBTNS_PUSHED|NCBTNS_CHECKED))
		{
			c1 = crDk;
			c2 = crHi;
			if (!(TrackerSettings::Instance().m_dwPatternSetup & PATTERN_FLATBUTTONS))
			{
				c4 = crHi;
				c3 = (dwStyle & NCBTNS_PUSHED) ? RGB(0,0,0) : crDk;
			}
			xofs = yofs = 1;
		} else
		if ((dwStyle & NCBTNS_MOUSEOVER) && (TrackerSettings::Instance().m_dwPatternSetup & PATTERN_FLATBUTTONS))
		{
			c1 = crHi;
			c2 = crDk;
		}
		switch(cLeftBarButtons[nBtn])
		{
		case ID_ENVSEL_VOLUME:		nImage = IIMAGE_VOLENV; break;
		case ID_ENVSEL_PANNING:		nImage = IIMAGE_PANENV; break;
		case ID_ENVSEL_PITCH:		nImage = (dwStyle & NCBTNS_DISABLED) ? IIMAGE_NOPITCHENV : IIMAGE_PITCHENV; break;
		case ID_ENVELOPE_SETLOOP:	nImage = IIMAGE_LOOP; break;
		case ID_ENVELOPE_SUSTAIN:	nImage = IIMAGE_SUSTAIN; break;
		case ID_ENVELOPE_CARRY:		nImage = (dwStyle & NCBTNS_DISABLED) ? IIMAGE_NOCARRY : IIMAGE_CARRY; break;
		case ID_ENVELOPE_VOLUME:	nImage = IIMAGE_VOLSWITCH; break;
		case ID_ENVELOPE_PANNING:	nImage = IIMAGE_PANSWITCH; break;
		case ID_ENVELOPE_PITCH:		nImage = (dwStyle & NCBTNS_DISABLED) ? IIMAGE_NOPITCHSWITCH : IIMAGE_PITCHSWITCH; break;
		case ID_ENVELOPE_FILTER:	nImage = (dwStyle & NCBTNS_DISABLED) ? IIMAGE_NOFILTERSWITCH : IIMAGE_FILTERSWITCH; break;
		case ID_INSTRUMENT_SAMPLEMAP: nImage = IIMAGE_SAMPLEMAP; break;
		case ID_ENVELOPE_VIEWGRID:	nImage = IIMAGE_GRID; break;
		case ID_ENVELOPE_ZOOM_IN:	nImage = (dwStyle & NCBTNS_DISABLED) ? IIMAGE_NOZOOMIN : IIMAGE_ZOOMIN; break;
		case ID_ENVELOPE_ZOOM_OUT:	nImage = (dwStyle & NCBTNS_DISABLED) ? IIMAGE_NOZOOMOUT : IIMAGE_ZOOMOUT; break;
		}
		pDC->Draw3dRect(rect.left-1, rect.top-1, ENV_LEFTBAR_CXBTN+2, ENV_LEFTBAR_CYBTN+2, c3, c4);
		pDC->Draw3dRect(rect.left, rect.top, ENV_LEFTBAR_CXBTN, ENV_LEFTBAR_CYBTN, c1, c2);
		rect.DeflateRect(1, 1);
		pDC->FillSolidRect(&rect, crFc);
		rect.left += xofs;
		rect.top += yofs;
		if (dwStyle & NCBTNS_CHECKED) m_bmpEnvBar.Draw(pDC, IIMAGE_CHECKED, rect.TopLeft(), ILD_NORMAL);
		m_bmpEnvBar.Draw(pDC, nImage, rect.TopLeft(), ILD_NORMAL);
	} else
	{
		c1 = c2 = crFc;
		if (TrackerSettings::Instance().m_dwPatternSetup & PATTERN_FLATBUTTONS)
		{
			c1 = crDk;
			c2 = crHi;
		}
		pDC->Draw3dRect(rect.left, rect.top, 2, ENV_LEFTBAR_CYBTN, c1, c2);
	}
}


void CViewInstrument::OnNcPaint()
//-------------------------------
{
	RECT rect;

	CModScrollView::OnNcPaint();
	GetWindowRect(&rect);
	// Assumes there is no other non-client items
	rect.bottom = ENV_LEFTBAR_CY;
	rect.right -= rect.left;
	rect.left = 0;
	rect.top = 0;
	if ((rect.left < rect.right) && (rect.top < rect.bottom))
	{
		CDC *pDC = GetWindowDC();
		HDC hdc = pDC->m_hDC;
		HGDIOBJ oldpen = SelectObject(hdc, (HGDIOBJ)CMainFrame::penDarkGray);
		rect.bottom--;
		MoveToEx(hdc, rect.left, rect.bottom, NULL);
		LineTo(hdc, rect.right, rect.bottom);
		if (rect.top < rect.bottom) FillRect(hdc, &rect, CMainFrame::brushGray);
		if (rect.top + 2 < rect.bottom)
		{
			for (UINT i=0; i<ENV_LEFTBAR_BUTTONS; i++)
			{
				DrawNcButton(pDC, i);
			}
		}
		if (oldpen) SelectObject(hdc, oldpen);
		ReleaseDC(pDC);
	}
}


////////////////////////////////////////////////////////////////////
// CViewInstrument messages


void CViewInstrument::OnSize(UINT nType, int cx, int cy)
//------------------------------------------------------
{
	CModScrollView::OnSize(nType, cx, cy);
	if (((nType == SIZE_RESTORED) || (nType == SIZE_MAXIMIZED)) && (cx > 0) && (cy > 0))
	{
		UpdateScrollSize();
	}
}


void CViewInstrument::OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp)
//---------------------------------------------------------------------------------
{
	CModScrollView::OnNcCalcSize(bCalcValidRects, lpncsp);
	if (lpncsp)
	{
		lpncsp->rgrc[0].top += ENV_LEFTBAR_CY;
		if (lpncsp->rgrc[0].bottom < lpncsp->rgrc[0].top) lpncsp->rgrc[0].top = lpncsp->rgrc[0].bottom;
	}
}


void CViewInstrument::OnNcMouseMove(UINT nHitTest, CPoint point)
//--------------------------------------------------------------
{
	CRect rect, rcWnd;
	UINT nBtnSel = 0xFFFF;

	GetWindowRect(&rcWnd);
	for (UINT i=0; i<ENV_LEFTBAR_BUTTONS; i++)
	{
		if ((!(m_NcButtonState[i] & NCBTNS_DISABLED)) && (GetNcButtonRect(i, &rect)))
		{
			rect.OffsetRect(rcWnd.left, rcWnd.top);
			if (rect.PtInRect(point))
			{
				nBtnSel = i;
				break;
			}
		}
	}
	if (nBtnSel != m_nBtnMouseOver)
	{
		CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
		if (pMainFrm)
		{
			CString strText = "";
			if ((nBtnSel < ENV_LEFTBAR_BUTTONS) && (cLeftBarButtons[nBtnSel] != ID_SEPARATOR))
			{
				strText.LoadString(cLeftBarButtons[nBtnSel]);
			}
			pMainFrm->SetHelpText(strText);
		}
		m_nBtnMouseOver = nBtnSel;
		UpdateNcButtonState();
	}
	CModScrollView::OnNcMouseMove(nHitTest, point);
}


void CViewInstrument::OnNcLButtonDown(UINT uFlags, CPoint point)
//--------------------------------------------------------------
{
	if (m_nBtnMouseOver < ENV_LEFTBAR_BUTTONS)
	{
		m_dwStatus |= INSSTATUS_NCLBTNDOWN;
		if (cLeftBarButtons[m_nBtnMouseOver] != ID_SEPARATOR)
		{
			PostMessage(WM_COMMAND, cLeftBarButtons[m_nBtnMouseOver]);
			UpdateNcButtonState();
		}
	}
	CModScrollView::OnNcLButtonDown(uFlags, point);
}


void CViewInstrument::OnNcLButtonUp(UINT uFlags, CPoint point)
//------------------------------------------------------------
{
	if (m_dwStatus & INSSTATUS_NCLBTNDOWN)
	{
		m_dwStatus &= ~INSSTATUS_NCLBTNDOWN;
		UpdateNcButtonState();
	}
	CModScrollView::OnNcLButtonUp(uFlags, point);
}


void CViewInstrument::OnNcLButtonDblClk(UINT uFlags, CPoint point)
//----------------------------------------------------------------
{
	OnNcLButtonDown(uFlags, point);
}


#if _MFC_VER > 0x0710
LRESULT CViewInstrument::OnNcHitTest(CPoint point)
#else
UINT CViewInstrument::OnNcHitTest(CPoint point)
#endif
//---------------------------------------------
{
	CRect rect;
	GetWindowRect(&rect);
	rect.bottom = rect.top + ENV_LEFTBAR_CY;
	if (rect.PtInRect(point))
	{
		return HTBORDER;
	}
	return CModScrollView::OnNcHitTest(point);
}


void CViewInstrument::OnMouseMove(UINT, CPoint pt)
//------------------------------------------------
{
	ModInstrument *pIns = GetInstrumentPtr();
	InstrumentEnvelope *pEnv = GetEnvelopePtr();
	if (pIns == nullptr || pEnv == nullptr) return;

	bool bSplitCursor = false;
	CHAR s[256];

	if ((m_nBtnMouseOver < ENV_LEFTBAR_BUTTONS) || (m_dwStatus & INSSTATUS_NCLBTNDOWN))
	{
		m_dwStatus &= ~INSSTATUS_NCLBTNDOWN;
		m_nBtnMouseOver = 0xFFFF;
		UpdateNcButtonState();
		CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
		if (pMainFrm) pMainFrm->SetHelpText("");
	}
	int nTick = ScreenToTick(pt.x);
	int nVal = ScreenToValue(pt.y);
	nVal = Clamp(nVal, ENVELOPE_MIN, ENVELOPE_MAX);
	if (nTick < 0) nTick = 0;
	if (nTick <= EnvGetReleaseNodeTick() + 1 || EnvGetReleaseNode() == ENV_RELEASE_NODE_UNSET)
	{
		// ticks before release node (or no release node)
		const int displayVal = (m_nEnv != ENV_VOLUME && !(m_nEnv == ENV_PITCH && pIns->PitchEnv.dwFlags[ENV_FILTER])) ? nVal - 32 : nVal;
		if(m_nEnv != ENV_PANNING)
			wsprintf(s, "Tick %d, [%d]", nTick, displayVal);
		else	// panning envelope: display right/center/left chars
			wsprintf(s, "Tick %d, [%d %c]", nTick, abs(displayVal), displayVal > 0 ? 'R' : (displayVal < 0 ? 'L' : 'C'));
	} else
	{
		// ticks after release node
		int displayVal = (nVal - EnvGetReleaseNodeValue()) * 2;
		displayVal = (m_nEnv != ENV_VOLUME) ? displayVal - 32 : displayVal;
		wsprintf(s, "Tick %d, [Rel%c%d]",  nTick, displayVal > 0 ? '+' : '-', abs(displayVal));
	}
	UpdateIndicator(s);

	if ((m_dwStatus & INSSTATUS_DRAGGING) && (m_nDragItem))
	{
		bool bChanged = false;
		if (pt.x >= m_rcClient.right - 2) nTick++;
		if (IsDragItemEnvPoint())
		{
			// Ctrl pressed -> move tail of envelope
			bChanged = EnvSetValue(m_nDragItem - 1, nTick, nVal, CMainFrame::GetMainFrame()->GetInputHandler()->CtrlPressed());
		} else
		{
			int nPoint = ScreenToPoint(pt.x, pt.y);
			if (nPoint >= 0) switch(m_nDragItem)
			{
			case ENV_DRAGLOOPSTART:
				bChanged = EnvSetLoopStart(nPoint);
				bSplitCursor = true;
				break;
			case ENV_DRAGLOOPEND:
				bChanged = EnvSetLoopEnd(nPoint);
				bSplitCursor = true;
				break;
			case ENV_DRAGSUSTAINSTART:
				bChanged = EnvSetSustainStart(nPoint);
				bSplitCursor = true;
				break;
			case ENV_DRAGSUSTAINEND:
				bChanged = EnvSetSustainEnd(nPoint);
				bSplitCursor = true;
				break;
			}
		}
		if (bChanged)
		{
			if (pt.x <= 0)
			{
				UpdateScrollSize();
				OnScrollBy(CSize(pt.x - (int)m_fZoom, 0), TRUE);
			}
			if (pt.x >= m_rcClient.right-1)
			{
				UpdateScrollSize();
				OnScrollBy(CSize((int)m_fZoom + pt.x - m_rcClient.right, 0), TRUE);
			}
			CModDoc *pModDoc = GetDocument();
			if(pModDoc)
			{
				SetModified(HINT_ENVELOPE, true);
			}
			UpdateWindow(); //rewbs: TODO - optimisation here so we don't redraw whole view.
		}
	} else
	{
		CRect rect;
		if (EnvGetSustain())
		{
			int nspace = m_rcClient.bottom/4;
			rect.top = ValueToScreen(EnvGetValue(EnvGetSustainStart())) - nspace;
			rect.bottom = rect.top + nspace * 2;
			rect.right = PointToScreen(EnvGetSustainStart()) + 1;
			rect.left = rect.right - (int)(m_fZoom * 2);
			if (rect.PtInRect(pt))
			{
				bSplitCursor = true; // ENV_DRAGSUSTAINSTART;
			} else
			{
				rect.top = ValueToScreen(EnvGetValue(EnvGetSustainEnd())) - nspace;
				rect.bottom = rect.top + nspace * 2;
				rect.left = PointToScreen(EnvGetSustainEnd()) - 1;
				rect.right = rect.left + (int)(m_fZoom *2);
				if (rect.PtInRect(pt)) bSplitCursor = true; // ENV_DRAGSUSTAINEND;
			}
		}
		if (EnvGetLoop())
		{
			rect.top = m_rcClient.top;
			rect.bottom = m_rcClient.bottom;
			rect.right = PointToScreen(EnvGetLoopStart()) + 1;
			rect.left = rect.right - (int)(m_fZoom * 2);
			if (rect.PtInRect(pt))
			{
				bSplitCursor = true; // ENV_DRAGLOOPSTART;
			} else
			{
				rect.left = PointToScreen(EnvGetLoopEnd()) - 1;
				rect.right = rect.left + (int)(m_fZoom * 2);
				if (rect.PtInRect(pt)) bSplitCursor = true; // ENV_DRAGLOOPEND;
			}
		}
	}
	// Update the mouse cursor
	if (bSplitCursor)
	{
		if (!(m_dwStatus & INSSTATUS_SPLITCURSOR))
		{
			m_dwStatus |= INSSTATUS_SPLITCURSOR;
			if (!(m_dwStatus & INSSTATUS_DRAGGING)) SetCapture();
			SetCursor(CMainFrame::curVSplit);
		}
	} else
	{
		if (m_dwStatus & INSSTATUS_SPLITCURSOR)
		{
			m_dwStatus &= ~INSSTATUS_SPLITCURSOR;
			SetCursor(CMainFrame::curArrow);
			if (!(m_dwStatus & INSSTATUS_DRAGGING))	ReleaseCapture();
		}
	}
}


void CViewInstrument::OnLButtonDown(UINT, CPoint pt)
//--------------------------------------------------
{
	if (!(m_dwStatus & INSSTATUS_DRAGGING))
	{
		CRect rect;
		// Look if dragging a point
		UINT maxpoint = EnvGetLastPoint();
		m_nDragItem = 0;
		for (UINT i = 0; i <= maxpoint; i++)
		{
			int x = PointToScreen(i);
			int y = ValueToScreen(EnvGetValue(i));
			rect.SetRect(x-6, y-6, x+7, y+7);
			if (rect.PtInRect(pt))
			{
				m_nDragItem = i + 1;
				break;
			}
		}
		if ((!m_nDragItem) && (EnvGetSustain()))
		{
			int nspace = m_rcClient.bottom/4;
			rect.top = ValueToScreen(EnvGetValue(EnvGetSustainStart())) - nspace;
			rect.bottom = rect.top + nspace * 2;
			rect.right = PointToScreen(EnvGetSustainStart()) + 1;
			rect.left = rect.right - (int)(m_fZoom * 2);
			if (rect.PtInRect(pt))
			{
				m_nDragItem = ENV_DRAGSUSTAINSTART;
			} else
			{
				rect.top = ValueToScreen(EnvGetValue(EnvGetSustainEnd())) - nspace;
				rect.bottom = rect.top + nspace * 2;
				rect.left = PointToScreen(EnvGetSustainEnd()) - 1;
				rect.right = rect.left + (int)(m_fZoom * 2);
				if (rect.PtInRect(pt)) m_nDragItem = ENV_DRAGSUSTAINEND;
			}
		}
		if ((!m_nDragItem) && (EnvGetLoop()))
		{
			rect.top = m_rcClient.top;
			rect.bottom = m_rcClient.bottom;
			rect.right = PointToScreen(EnvGetLoopStart()) + 1;
			rect.left = rect.right - (int)(m_fZoom * 2);
			if (rect.PtInRect(pt))
			{
				m_nDragItem = ENV_DRAGLOOPSTART;
			} else
			{
				rect.left = PointToScreen(EnvGetLoopEnd()) - 1;
				rect.right = rect.left + (int)(m_fZoom * 2);
				if (rect.PtInRect(pt)) m_nDragItem = ENV_DRAGLOOPEND;
			}
		}

		if (m_nDragItem)
		{
			SetCapture();
			m_dwStatus |= INSSTATUS_DRAGGING;
			// refresh active node colour
			InvalidateRect(NULL, FALSE);
		} else
		{
			// Shift-Click: Insert envelope point here
			if(CMainFrame::GetMainFrame()->GetInputHandler()->ShiftPressed())
			{
				m_nDragItem = EnvInsertPoint(ScreenToTick(pt.x), ScreenToValue(pt.y)); // returns point ID + 1 if successful, else 0.
				if(m_nDragItem > 0)
				{
					// Drag point if successful
					SetCapture();
					m_dwStatus |= INSSTATUS_DRAGGING;
				}
			}
		}
	}
}


void CViewInstrument::OnLButtonUp(UINT, CPoint)
//---------------------------------------------
{
	if (m_dwStatus & INSSTATUS_SPLITCURSOR)
	{
		m_dwStatus &= ~INSSTATUS_SPLITCURSOR;
		SetCursor(CMainFrame::curArrow);
	}
	if (m_dwStatus & INSSTATUS_DRAGGING)
	{
		m_dwStatus &= ~INSSTATUS_DRAGGING;
		ReleaseCapture();
	}
}


void CViewInstrument::OnRButtonDown(UINT, CPoint pt)
//--------------------------------------------------
{
	const CModDoc *pModDoc = GetDocument();
	if(!pModDoc) return;
	const CSoundFile &sndFile = GetDocument()->GetrSoundFile();

	CMenu Menu;
	if (m_dwStatus & INSSTATUS_DRAGGING) return;
	if ((pModDoc) && (Menu.LoadMenu(IDR_ENVELOPES)))
	{
		CMenu* pSubMenu = Menu.GetSubMenu(0);
		if (pSubMenu != NULL)
		{
			m_nDragItem = ScreenToPoint(pt.x, pt.y) + 1;
			const uint32 maxPoint = (sndFile.GetType() == MOD_TYPE_XM) ? 11 : 24;
			const uint32 lastpoint = EnvGetLastPoint();
			const bool forceRelease = !sndFile.GetModSpecifications().hasReleaseNode && (EnvGetReleaseNode() != ENV_RELEASE_NODE_UNSET);
			pSubMenu->EnableMenuItem(ID_ENVELOPE_INSERTPOINT, (lastpoint < maxPoint) ? MF_ENABLED : MF_GRAYED);
			pSubMenu->EnableMenuItem(ID_ENVELOPE_REMOVEPOINT, ((m_nDragItem) && (lastpoint > 0)) ? MF_ENABLED : MF_GRAYED);
			pSubMenu->EnableMenuItem(ID_ENVELOPE_CARRY, (sndFile.GetType() & (MOD_TYPE_IT|MOD_TYPE_MPT)) ? MF_ENABLED : MF_GRAYED);
			pSubMenu->EnableMenuItem(ID_ENVELOPE_TOGGLERELEASENODE, ((sndFile.GetModSpecifications().hasReleaseNode && m_nEnv == ENV_VOLUME) || forceRelease) ? MF_ENABLED : MF_GRAYED);
			pSubMenu->CheckMenuItem(ID_ENVELOPE_SETLOOP, (EnvGetLoop()) ? MF_CHECKED : MF_UNCHECKED);
			pSubMenu->CheckMenuItem(ID_ENVELOPE_SUSTAIN, (EnvGetSustain()) ? MF_CHECKED : MF_UNCHECKED);
			pSubMenu->CheckMenuItem(ID_ENVELOPE_CARRY, (EnvGetCarry()) ? MF_CHECKED : MF_UNCHECKED);
			pSubMenu->CheckMenuItem(ID_ENVELOPE_TOGGLERELEASENODE, (EnvGetReleaseNode() == m_nDragItem - 1) ? MF_CHECKED : MF_UNCHECKED);
			m_ptMenu = pt;
			ClientToScreen(&pt);
			pSubMenu->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON,pt.x,pt.y,this);
		}
	}
}

void CViewInstrument::OnMButtonDown(UINT, CPoint pt)
//--------------------------------------------------
{
	// Middle mouse button: Remove envelope point
	if(EnvGetLastPoint() == 0) return;
	m_nDragItem = ScreenToPoint(pt.x, pt.y) + 1;
	EnvRemovePoint(m_nDragItem - 1);
}


void CViewInstrument::OnPrevInstrument()
//--------------------------------------
{
	SendCtrlMessage(CTRLMSG_INS_PREVINSTRUMENT);
}


void CViewInstrument::OnNextInstrument()
//--------------------------------------
{
	SendCtrlMessage(CTRLMSG_INS_NEXTINSTRUMENT);
}


void CViewInstrument::OnEditSampleMap()
//-------------------------------------
{
	SendCtrlMessage(CTRLMSG_INS_SAMPLEMAP);
}


void CViewInstrument::OnSelectVolumeEnv()
//---------------------------------------
{
	if (m_nEnv != ENV_VOLUME) SetCurrentInstrument(m_nInstrument, ENV_VOLUME);
}


void CViewInstrument::OnSelectPanningEnv()
//----------------------------------------
{
	if (m_nEnv != ENV_PANNING) SetCurrentInstrument(m_nInstrument, ENV_PANNING);
}


void CViewInstrument::OnSelectPitchEnv()
//--------------------------------------
{
	if (m_nEnv != ENV_PITCH) SetCurrentInstrument(m_nInstrument, ENV_PITCH);
}


void CViewInstrument::OnEnvLoopChanged()
//--------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if ((pModDoc) && (EnvSetLoop(!EnvGetLoop())))
	{
		InstrumentEnvelope *pEnv = GetEnvelopePtr();
		if(EnvGetLoop() && pEnv != nullptr && pEnv->nLoopEnd == 0)
		{
			// Enabled loop => set loop points if no loop has been specified yet.
			pEnv->nLoopStart = 0;
			pEnv->nLoopEnd = static_cast<uint8>(pEnv->nNodes - 1);
		}
		SetModified(HINT_ENVELOPE, true);
	}
}


void CViewInstrument::OnEnvSustainChanged()
//-----------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if ((pModDoc) && (EnvSetSustain(!EnvGetSustain())))
	{
		InstrumentEnvelope *pEnv = GetEnvelopePtr();
		if(EnvGetSustain() && pEnv != nullptr && pEnv->nSustainStart == pEnv->nSustainEnd && IsDragItemEnvPoint())
		{
			// Enabled sustain loop => set sustain loop points if no sustain loop has been specified yet.
			pEnv->nSustainStart = pEnv->nSustainEnd = static_cast<uint8>(m_nDragItem - 1);
		}
		SetModified(HINT_ENVELOPE, true);
	}
}


void CViewInstrument::OnEnvCarryChanged()
//---------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if ((pModDoc) && (EnvSetCarry(!EnvGetCarry())))
	{
		SetModified(HINT_ENVELOPE, false);
	}
}

void CViewInstrument::OnEnvToggleReleasNode()
//-------------------------------------------
{
	if(IsDragItemEnvPoint() && EnvToggleReleaseNode(m_nDragItem - 1))
	{
		SetModified(HINT_ENVELOPE, true);
	}
}


void CViewInstrument::OnEnvVolChanged()
//-------------------------------------
{
	if (EnvSetVolEnv(!EnvGetVolEnv()))
	{
		SetModified(HINT_ENVELOPE, false);
	}
}


void CViewInstrument::OnEnvPanChanged()
//-------------------------------------
{
	if (EnvSetPanEnv(!EnvGetPanEnv()))
	{
		SetModified(HINT_ENVELOPE, false);
	}
}


void CViewInstrument::OnEnvPitchChanged()
//---------------------------------------
{
	if (EnvSetPitchEnv(!EnvGetPitchEnv()))
	{
		SetModified(HINT_ENVELOPE, false);
	}
}


void CViewInstrument::OnEnvFilterChanged()
//----------------------------------------
{
	if (EnvSetFilterEnv(!EnvGetFilterEnv()))
	{
		SetModified(HINT_ENVELOPE, false);
	}
}

//rewbs.envRowGrid
void CViewInstrument::OnEnvToggleGrid()
//-------------------------------------
{
	m_bGrid = !m_bGrid;
	if (m_bGrid)
		m_bGridForceRedraw = true;
	CModDoc *pModDoc = GetDocument();
	if (pModDoc)
		pModDoc->UpdateAllViews(NULL, (m_nInstrument << HINT_SHIFT_INS) | HINT_ENVELOPE);

}
//end rewbs.envRowGrid

void CViewInstrument::OnEnvRemovePoint()
//--------------------------------------
{
	if(m_nDragItem > 0)
	{
		EnvRemovePoint(m_nDragItem - 1);
	}
}

void CViewInstrument::OnEnvInsertPoint()
//--------------------------------------
{
	const int tick = ScreenToTick(m_ptMenu.x), value = ScreenToValue(m_ptMenu.y);
	if(!EnvInsertPoint(tick, value))
	{
		// Couldn't insert point, maybe because there's already a point at this tick
		// => Try next tick
		EnvInsertPoint(tick + 1, value);
	}
}


void CViewInstrument::OnEditCopy()
//--------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if (pModDoc) pModDoc->CopyEnvelope(m_nInstrument, m_nEnv);
}


void CViewInstrument::OnEditPaste()
//---------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if (pModDoc) pModDoc->PasteEnvelope(m_nInstrument, m_nEnv);
}

static DWORD nLastNotePlayed = 0;
static DWORD nLastScanCode = 0;


void CViewInstrument::PlayNote(ModCommand::NOTE note)
//---------------------------------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	CModDoc *pModDoc = GetDocument();
	if ((pModDoc) && (pMainFrm) && (note > 0) && (note<128))
	{
		CHAR s[64];
		if (note >= NOTE_MIN_SPECIAL)
		{
			pModDoc->NoteOff(0, (note == NOTE_NOTECUT), m_nInstrument);
			pMainFrm->SetInfoText("");
		} else
		if (m_nInstrument && !m_baPlayingNote[note])
		{
			//rewbs.instViewNNA
		/*	CSoundFile *pSoundFile = pModDoc->GetSoundFile();
			if (pSoundFile && m_baPlayingNote>0 && m_nPlayingChannel>=0)
			{
				ModChannel *pChn = &(pSoundFile->Chn[m_nPlayingChannel]); //Get pointer to channel playing last note.
				if (pChn->pHeader)	//is it valid?
				{
					DWORD tempflags = pChn->dwFlags;
					pChn->dwFlags = 0;
					pModDoc->GetSoundFile()->CheckNNA(m_nPlayingChannel, m_nInstrument, note, FALSE); //if so, apply NNA
					pChn->dwFlags = tempflags;
				}
			}
		*/
			ModInstrument *pIns = pModDoc->GetrSoundFile().Instruments[m_nInstrument];
			if ((!pIns) || (!pIns->Keyboard[note - NOTE_MIN] && !pIns->nMixPlug)) return;
			m_baPlayingNote[note] = true;											//rewbs.instViewNNA
			pModDoc->PlayNote(note, m_nInstrument, 0, false); //rewbs.instViewNNA
			s[0] = 0;
			if ((note) && (note <= NOTE_MAX))
			{
				const std::string temp = pModDoc->GetrSoundFile().GetNoteName(note, m_nInstrument);
				mpt::String::Copy(s, temp.c_str());
			}
			pMainFrm->SetInfoText(s);
		}
	}
}

void CViewInstrument::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
//-----------------------------------------------------------------
{
	CModScrollView::OnChar(nChar, nRepCnt, nFlags);
}


void CViewInstrument::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags)
//------------------------------------------------------------------
{
	CModScrollView::OnKeyUp(nChar, nRepCnt, nFlags);
}


// Drop files from Windows
void CViewInstrument::OnDropFiles(HDROP hDropInfo)
//------------------------------------------------
{
	const UINT nFiles = ::DragQueryFileW(hDropInfo, (UINT)-1, NULL, 0);
	CMainFrame::GetMainFrame()->SetForegroundWindow();
	for(UINT f = 0; f < nFiles; f++)
	{
		WCHAR fileName[MAX_PATH];
		if(::DragQueryFileW(hDropInfo, f, fileName, CountOf(fileName)))
		{
			const mpt::PathString file = mpt::PathString::FromNative(fileName);
			if(SendCtrlMessage(CTRLMSG_INS_OPENFILE, (LPARAM)&file) && f < nFiles - 1)
			{
				// Insert more instrument slots
				SendCtrlMessage(IDC_INSTRUMENT_NEW);
			}
		}
	}
	::DragFinish(hDropInfo);
}


BOOL CViewInstrument::OnDragonDrop(BOOL bDoDrop, const DRAGONDROP *lpDropInfo)
//----------------------------------------------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	BOOL bCanDrop = FALSE;
	BOOL bUpdate;

	if ((!lpDropInfo) || (!pModDoc)) return FALSE;
	CSoundFile &sndFile = pModDoc->GetrSoundFile();
	switch(lpDropInfo->dwDropType)
	{
	case DRAGONDROP_INSTRUMENT:
		if (lpDropInfo->pModDoc == pModDoc)
		{
			bCanDrop = ((lpDropInfo->dwDropItem)
					 && (lpDropInfo->dwDropItem <= sndFile.m_nInstruments)
					 && (lpDropInfo->pModDoc == pModDoc));
		} else
		{
			bCanDrop = ((lpDropInfo->dwDropItem)
					 && ((lpDropInfo->lDropParam) || (lpDropInfo->pModDoc)));
		}
		break;

	case DRAGONDROP_DLS:
		bCanDrop = ((lpDropInfo->dwDropItem < CTrackApp::gpDLSBanks.size())
				 && (CTrackApp::gpDLSBanks[lpDropInfo->dwDropItem]));
		break;

	case DRAGONDROP_SOUNDFILE:
	case DRAGONDROP_MIDIINSTR:
		bCanDrop = !lpDropInfo->GetPath().empty();
		break;
	}
	if ((!bCanDrop) || (!bDoDrop)) return bCanDrop;
	if ((!sndFile.GetNumInstruments()) && sndFile.GetModSpecifications().instrumentsMax > 0)
	{
		SendCtrlMessage(CTRLMSG_INS_NEWINSTRUMENT);
	}
	if ((!m_nInstrument) || (m_nInstrument > sndFile.GetNumInstruments())) return FALSE;
	// Do the drop
	bUpdate = FALSE;
	BeginWaitCursor();
	switch(lpDropInfo->dwDropType)
	{
	case DRAGONDROP_INSTRUMENT:
		if (lpDropInfo->pModDoc == pModDoc)
		{
			SendCtrlMessage(CTRLMSG_SETCURRENTINSTRUMENT, lpDropInfo->dwDropItem);
		} else
		{
			SendCtrlMessage(CTRLMSG_INS_SONGDROP, (LPARAM)lpDropInfo);
		}
		break;

	case DRAGONDROP_MIDIINSTR:
		if (CDLSBank::IsDLSBank(lpDropInfo->GetPath()))
		{
			CDLSBank dlsbank;
			if (dlsbank.Open(lpDropInfo->GetPath()))
			{
				DLSINSTRUMENT *pDlsIns;
				UINT nIns = 0, nRgn = 0xFF;
				// Drums
				if (lpDropInfo->dwDropItem & 0x80)
				{
					UINT key = lpDropInfo->dwDropItem & 0x7F;
					pDlsIns = dlsbank.FindInstrument(TRUE, 0xFFFF, 0xFF, key, &nIns);
					if (pDlsIns) nRgn = dlsbank.GetRegionFromKey(nIns, key);
				} else
				// Melodic
				{
					pDlsIns = dlsbank.FindInstrument(FALSE, 0xFFFF, lpDropInfo->dwDropItem, 60, &nIns);
					if (pDlsIns) nRgn = dlsbank.GetRegionFromKey(nIns, 60);
				}
				bCanDrop = FALSE;
				if (pDlsIns)
				{
					CriticalSection cs;
					bCanDrop = dlsbank.ExtractInstrument(sndFile, m_nInstrument, nIns, nRgn);
				}
				bUpdate = TRUE;
				break;
			}
		}
		// Instrument file -> fall through
		MPT_FALLTHROUGH;
	case DRAGONDROP_SOUNDFILE:
		SendCtrlMessage(CTRLMSG_INS_OPENFILE, lpDropInfo->lDropParam);
		break;

	case DRAGONDROP_DLS:
		{
			CDLSBank *pDLSBank = CTrackApp::gpDLSBanks[lpDropInfo->dwDropItem];
			UINT nIns = lpDropInfo->lDropParam & 0x7FFF;
			UINT nRgn;
			// Drums:	(0x80000000) | (Region << 16) | (Instrument)
			if (lpDropInfo->lDropParam & 0x80000000)
			{
				nRgn = (lpDropInfo->lDropParam & 0xFF0000) >> 16;
			} else
			// Melodic: (Instrument)
			{
				nRgn = pDLSBank->GetRegionFromKey(nIns, 60);
			}

			CriticalSection cs;

			bCanDrop = pDLSBank->ExtractInstrument(sndFile, m_nInstrument, nIns, nRgn);
			bUpdate = TRUE;
		}
		break;
	}
	if (bUpdate)
	{
		SetModified(HINT_INSTRUMENT | HINT_ENVELOPE | HINT_INSNAMES, true);
	}
	CMDIChildWnd *pMDIFrame = (CMDIChildWnd *)GetParentFrame();
	if (pMDIFrame)
	{
		pMDIFrame->MDIActivate();
		pMDIFrame->SetActiveView(this);
		SetFocus();
	}
	EndWaitCursor();
	return bCanDrop;
}


LRESULT CViewInstrument::OnMidiMsg(WPARAM midiData, LPARAM)
//---------------------------------------------------------
{
	CModDoc *modDoc = GetDocument();
	if(modDoc != nullptr)
	{
		modDoc->ProcessMIDI(static_cast<uint32>(midiData), m_nInstrument, modDoc->GetrSoundFile().GetInstrumentPlugin(m_nInstrument), kCtxViewInstruments);
		return 1;
	}
	return 0;
}


BOOL CViewInstrument::PreTranslateMessage(MSG *pMsg)
//--------------------------------------------------
{
	if (pMsg)
	{
		//We handle keypresses before Windows has a chance to handle them (for alt etc..)
		if((pMsg->message == WM_SYSKEYUP)   || (pMsg->message == WM_KEYUP) ||
			(pMsg->message == WM_SYSKEYDOWN) || (pMsg->message == WM_KEYDOWN))
		{
			CInputHandler* ih = (CMainFrame::GetMainFrame())->GetInputHandler();

			//Translate message manually
			UINT nChar = pMsg->wParam;
			UINT nRepCnt = LOWORD(pMsg->lParam);
			UINT nFlags = HIWORD(pMsg->lParam);
			KeyEventType kT = ih->GetKeyEventType(nFlags);
			InputTargetContext ctx = (InputTargetContext)(kCtxViewInstruments);

			if(ih->KeyEvent(ctx, nChar, nRepCnt, nFlags, kT) != kcNull)
				return true; // Mapped to a command, no need to pass message on.
		}

	}

	return CModScrollView::PreTranslateMessage(pMsg);
}

LRESULT CViewInstrument::OnCustomKeyMsg(WPARAM wParam, LPARAM)
//------------------------------------------------------------
{
	if (wParam == kcNull)
		return NULL;

	CModDoc *pModDoc = GetDocument();
	if(!pModDoc) return NULL;

	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();

	switch(wParam)
	{
		case kcPrevInstrument:	OnPrevInstrument(); return wParam;
		case kcNextInstrument:	OnNextInstrument(); return wParam;
		case kcEditCopy:		OnEditCopy(); return wParam;
		case kcEditPaste:		OnEditPaste(); return wParam;
		case kcNoteOff:			PlayNote(NOTE_KEYOFF); return wParam;
		case kcNoteCut:			PlayNote(NOTE_NOTECUT); return wParam;
		case kcInstrumentLoad:	SendCtrlMessage(IDC_INSTRUMENT_OPEN); return wParam;
		case kcInstrumentSave:	SendCtrlMessage(IDC_INSTRUMENT_SAVEAS); return wParam;
		case kcInstrumentNew:	SendCtrlMessage(IDC_INSTRUMENT_NEW); return wParam;

		// envelope editor
		case kcInstrumentEnvelopeZoomIn:				OnEnvZoomIn(); return wParam;
		case kcInstrumentEnvelopeZoomOut:				OnEnvZoomOut(); return wParam;
		case kcInstrumentEnvelopePointPrev:				EnvKbdSelectPrevPoint(); return wParam;
		case kcInstrumentEnvelopePointNext:				EnvKbdSelectNextPoint(); return wParam;
		case kcInstrumentEnvelopePointMoveLeft:			EnvKbdMovePointLeft(); return wParam;
		case kcInstrumentEnvelopePointMoveRight:		EnvKbdMovePointRight(); return wParam;
		case kcInstrumentEnvelopePointMoveUp:			EnvKbdMovePointUp(1); return wParam;
		case kcInstrumentEnvelopePointMoveDown:			EnvKbdMovePointDown(1); return wParam;
		case kcInstrumentEnvelopePointMoveUp8:			EnvKbdMovePointUp(8); return wParam;
		case kcInstrumentEnvelopePointMoveDown8:		EnvKbdMovePointDown(8); return wParam;
		case kcInstrumentEnvelopePointInsert:			EnvKbdInsertPoint(); return wParam;
		case kcInstrumentEnvelopePointRemove:			EnvKbdRemovePoint(); return wParam;
		case kcInstrumentEnvelopeSetLoopStart:			EnvKbdSetLoopStart(); return wParam;
		case kcInstrumentEnvelopeSetLoopEnd:			EnvKbdSetLoopEnd(); return wParam;
		case kcInstrumentEnvelopeSetSustainLoopStart:	EnvKbdSetSustainStart(); return wParam;
		case kcInstrumentEnvelopeSetSustainLoopEnd:		EnvKbdSetSustainEnd(); return wParam;
		case kcInstrumentEnvelopeToggleReleaseNode:		EnvKbdToggleReleaseNode(); return wParam;
	}
	if(wParam >= kcInstrumentStartNotes && wParam <= kcInstrumentEndNotes)
	{
		PlayNote(static_cast<ModCommand::NOTE>(wParam - kcInstrumentStartNotes + 1 + pMainFrm->GetBaseOctave() * 12));
		return wParam;
	}
	if(wParam >= kcInstrumentStartNoteStops && wParam <= kcInstrumentEndNoteStops)
	{
		ModCommand::NOTE note = static_cast<ModCommand::NOTE>(wParam - kcInstrumentStartNoteStops + 1 + pMainFrm->GetBaseOctave() * 12);
		if(ModCommand::IsNote(note)) m_baPlayingNote[note] = false;
		pModDoc->NoteOff(note, false, m_nInstrument);
		return wParam;
	}

	return NULL;
}
void CViewInstrument::OnEnvelopeScalepoints()
//--------------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if(pModDoc == nullptr)
		return;
	const CSoundFile &sndFile = pModDoc->GetrSoundFile();

	if(m_nInstrument >= 1
		&& m_nInstrument <= sndFile.GetNumInstruments()
		&& sndFile.Instruments[m_nInstrument])
	{
		// "Center" y value of the envelope. For panning and pitch, this is 32, for volume and filter it is 0 (minimum).
		int nOffset = ((m_nEnv != ENV_VOLUME) && !GetEnvelopePtr()->dwFlags[ENV_FILTER]) ? 32 : 0;

		CScaleEnvPointsDlg dlg(this, *GetEnvelopePtr(), nOffset);
		if(dlg.DoModal() == IDOK)
		{
			SetModified(HINT_ENVELOPE, true);
		}
	}
}


void CViewInstrument::EnvSetZoom(float fNewZoom)
//----------------------------------------------
{
	m_fZoom = fNewZoom;
	Limit(m_fZoom, ENV_MIN_ZOOM, ENV_MAX_ZOOM);
	InvalidateRect(NULL, FALSE);
	UpdateScrollSize();
	UpdateNcButtonState();
}


////////////////////////////////////////
//  Envelope Editor - Keyboard actions

void CViewInstrument::EnvKbdSelectPrevPoint()
//-------------------------------------------
{
	InstrumentEnvelope *pEnv = GetEnvelopePtr();
	if(pEnv == nullptr) return;
	if(m_nDragItem <= 1 || m_nDragItem > pEnv->nNodes)
		m_nDragItem = pEnv->nNodes;
	else
		m_nDragItem--;
	InvalidateRect(NULL, FALSE);
}


void CViewInstrument::EnvKbdSelectNextPoint()
//-------------------------------------------
{
	InstrumentEnvelope *pEnv = GetEnvelopePtr();
	if(pEnv == nullptr) return;
	if(m_nDragItem >= pEnv->nNodes)
		m_nDragItem = 1;
	else
		m_nDragItem++;
	InvalidateRect(NULL, FALSE);
}


void CViewInstrument::EnvKbdMovePointLeft()
//-----------------------------------------
{
	InstrumentEnvelope *pEnv = GetEnvelopePtr();
	if(pEnv == nullptr || !IsDragItemEnvPoint()) return;
	if(m_nDragItem == 1 || !CanMovePoint(m_nDragItem - 1, -1))
		return;
	pEnv->Ticks[m_nDragItem - 1]--;

	SetModified(HINT_ENVELOPE, true);
}


void CViewInstrument::EnvKbdMovePointRight()
//------------------------------------------
{
	InstrumentEnvelope *pEnv = GetEnvelopePtr();
	if(pEnv == nullptr || !IsDragItemEnvPoint()) return;
	if(m_nDragItem == 1 || !CanMovePoint(m_nDragItem - 1, 1))
		return;
	pEnv->Ticks[m_nDragItem - 1]++;

	SetModified(HINT_ENVELOPE, true);
}


void CViewInstrument::EnvKbdMovePointUp(BYTE stepsize)
//----------------------------------------------------
{
	InstrumentEnvelope *pEnv = GetEnvelopePtr();
	if(pEnv == nullptr || !IsDragItemEnvPoint()) return;
	if(pEnv->Values[m_nDragItem - 1] <= ENVELOPE_MAX - stepsize)
		pEnv->Values[m_nDragItem - 1] += stepsize;
	else
		pEnv->Values[m_nDragItem - 1] = ENVELOPE_MAX;

	SetModified(HINT_ENVELOPE, true);
}


void CViewInstrument::EnvKbdMovePointDown(BYTE stepsize)
//------------------------------------------------------
{
	InstrumentEnvelope *pEnv = GetEnvelopePtr();
	if(pEnv == nullptr || !IsDragItemEnvPoint()) return;
	if(pEnv->Values[m_nDragItem - 1] >= ENVELOPE_MIN + stepsize)
		pEnv->Values[m_nDragItem - 1] -= stepsize;
	else
		pEnv->Values[m_nDragItem - 1] = ENVELOPE_MIN;

	SetModified(HINT_ENVELOPE, true);
}

void CViewInstrument::EnvKbdInsertPoint()
//---------------------------------------
{
	InstrumentEnvelope *pEnv = GetEnvelopePtr();
	if(pEnv == nullptr) return;
	if(!IsDragItemEnvPoint()) m_nDragItem = pEnv->nNodes;
	WORD newTick = pEnv->Ticks[pEnv->nNodes - 1] + 4;	// if last point is selected: add point after last point
	BYTE newVal = pEnv->Values[pEnv->nNodes - 1];
	// if some other point is selected: interpolate between this and next point (if there's room between them)
	if(m_nDragItem < pEnv->nNodes && (pEnv->Ticks[m_nDragItem] - pEnv->Ticks[m_nDragItem - 1] > 1))
	{
		newTick = (pEnv->Ticks[m_nDragItem - 1] + pEnv->Ticks[m_nDragItem]) / 2;
		newVal = (pEnv->Values[m_nDragItem - 1] + pEnv->Values[m_nDragItem]) / 2;
	}

	UINT newPoint = EnvInsertPoint(newTick, newVal);
	if(newPoint > 0) m_nDragItem = newPoint;
}


void CViewInstrument::EnvKbdRemovePoint()
//---------------------------------------
{
	InstrumentEnvelope *pEnv = GetEnvelopePtr();
	if(pEnv == nullptr || !IsDragItemEnvPoint() || pEnv->nNodes == 0) return;
	if(m_nDragItem > pEnv->nNodes) m_nDragItem = pEnv->nNodes;
	EnvRemovePoint(m_nDragItem - 1);
}


void CViewInstrument::EnvKbdSetLoopStart()
//----------------------------------------
{
	InstrumentEnvelope *pEnv = GetEnvelopePtr();
	if(pEnv == nullptr || !IsDragItemEnvPoint()) return;
	if(!EnvGetLoop())
		EnvSetLoopStart(0);
	EnvSetLoopStart(m_nDragItem - 1);
	GetDocument()->UpdateAllViews(NULL, (m_nInstrument << HINT_SHIFT_INS) | HINT_ENVELOPE, NULL);	// sanity checks are done in GetEnvelopePtr() already
}


void CViewInstrument::EnvKbdSetLoopEnd()
//--------------------------------------
{
	InstrumentEnvelope *pEnv = GetEnvelopePtr();
	if(pEnv == nullptr || !IsDragItemEnvPoint()) return;
	if(!EnvGetLoop())
	{
		EnvSetLoop(true);
		EnvSetLoopStart(0);
	}
	EnvSetLoopEnd(m_nDragItem - 1);
	GetDocument()->UpdateAllViews(NULL, (m_nInstrument << HINT_SHIFT_INS) | HINT_ENVELOPE, NULL);	// sanity checks are done in GetEnvelopePtr() already
}


void CViewInstrument::EnvKbdSetSustainStart()
//-------------------------------------------
{
	InstrumentEnvelope *pEnv = GetEnvelopePtr();
	if(pEnv == nullptr || !IsDragItemEnvPoint()) return;
	if(!EnvGetSustain())
		EnvSetSustain(true);
	EnvSetSustainStart(m_nDragItem - 1);
	GetDocument()->UpdateAllViews(NULL, (m_nInstrument << HINT_SHIFT_INS) | HINT_ENVELOPE, NULL);	// sanity checks are done in GetEnvelopePtr() already
}


void CViewInstrument::EnvKbdSetSustainEnd()
//-----------------------------------------
{
	InstrumentEnvelope *pEnv = GetEnvelopePtr();
	if(pEnv == nullptr || !IsDragItemEnvPoint()) return;
	if(!EnvGetSustain())
	{
		EnvSetSustain(true);
		EnvSetSustainStart(0);
	}
	EnvSetSustainEnd(m_nDragItem - 1);
	GetDocument()->UpdateAllViews(NULL, (m_nInstrument << HINT_SHIFT_INS) | HINT_ENVELOPE, NULL);	// sanity checks are done in GetEnvelopePtr() already
}


void CViewInstrument::EnvKbdToggleReleaseNode()
//---------------------------------------------
{
	InstrumentEnvelope *pEnv = GetEnvelopePtr();
	if(pEnv == nullptr || !IsDragItemEnvPoint()) return;
	if(EnvToggleReleaseNode(m_nDragItem - 1))
	{
		SetModified(HINT_ENVELOPE, true);
	}
}


// Get a pointer to the currently active instrument.
ModInstrument *CViewInstrument::GetInstrumentPtr() const
//------------------------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if(pModDoc == nullptr) return nullptr;
	return pModDoc->GetrSoundFile().Instruments[m_nInstrument];
}


// Get a pointer to the currently selected envelope.
// This function also implicitely validates the moddoc and soundfile pointers.
InstrumentEnvelope *CViewInstrument::GetEnvelopePtr() const
//---------------------------------------------------------
{
	// First do some standard checks...
	ModInstrument *pIns = GetInstrumentPtr();
	if(pIns == nullptr) return nullptr;

	return &pIns->GetEnvelope(m_nEnv);
}


bool CViewInstrument::CanMovePoint(UINT envPoint, int step)
//---------------------------------------------------------
{
	InstrumentEnvelope *pEnv = GetEnvelopePtr();
	if(pEnv == nullptr) return false;

	// Can't move first point
	if(envPoint == 0)
	{
		return false;
	}
	// Can't move left of previous point
	if((step < 0) && (pEnv->Ticks[envPoint] - pEnv->Ticks[envPoint - 1] <= -step))
	{
		return false;
	}
	// Can't move right of next point
	if((step > 0) && (envPoint < pEnv->nNodes - 1) && (pEnv->Ticks[envPoint + 1] - pEnv->Ticks[envPoint] <= step))
	{
		return false;
	}
	return true;
}


BOOL CViewInstrument::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
//----------------------------------------------------------------------
{
	// Ctrl + mouse wheel: envelope zoom.
	if (nFlags == MK_CONTROL)
	{
		// Speed up zoom scrolling by some factor (might need some tuning).
		const float speedUpFactor = std::max(1.0f, m_fZoom * 7.0f / ENV_MAX_ZOOM);
		EnvSetZoom(m_fZoom + speedUpFactor * (zDelta / WHEEL_DELTA));
	}

	return CModScrollView::OnMouseWheel(nFlags, zDelta, pt);
}


OPENMPT_NAMESPACE_END
