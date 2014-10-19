/*
 * view_smp.cpp
 * ------------
 * Purpose: Sample tab, lower panel.
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
#include "ctrl_smp.h"
#include "Dlsbank.h"
#include "channelManagerDlg.h"
#include "view_smp.h"
#include "../soundlib/MIDIEvents.h"
#include "SampleEditorDialogs.h"
#include "../soundlib/WAVTools.h"
#include "../soundlib/FileReader.h"
#include "../soundlib/SampleFormatConverters.h"

#define new DEBUG_NEW


OPENMPT_NAMESPACE_BEGIN


// Non-client toolbar
#define SMP_LEFTBAR_CY			29
#define SMP_LEFTBAR_CXSEP		14
#define SMP_LEFTBAR_CXSPC		3
#define SMP_LEFTBAR_CXBTN		24
#define SMP_LEFTBAR_CYBTN		22

#define MIN_ZOOM	-6
#define MAX_ZOOM	8

// Defines the minimum length for selection for which
// trimming will be done. This is the minimum value for
// selection difference, so the minimum length of result
// of trimming is nTrimLengthMin + 1.
#define MIN_TRIM_LENGTH			4

const UINT cLeftBarButtons[SMP_LEFTBAR_BUTTONS] =
{
	ID_SAMPLE_ZOOMUP,
	ID_SAMPLE_ZOOMDOWN,
		ID_SEPARATOR,
	ID_SAMPLE_DRAW,
	ID_SAMPLE_ADDSILENCE,
		ID_SEPARATOR,
	ID_SAMPLE_GRID,
		ID_SEPARATOR,
};


IMPLEMENT_SERIAL(CViewSample, CModScrollView, 0)

BEGIN_MESSAGE_MAP(CViewSample, CModScrollView)
	//{{AFX_MSG_MAP(CViewSample)
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_NCCALCSIZE()
	ON_WM_NCHITTEST()
	ON_WM_NCPAINT()
	ON_WM_MOUSEMOVE()
	ON_WM_NCMOUSEMOVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_NCLBUTTONDOWN()
	ON_WM_NCLBUTTONUP()
	ON_WM_NCLBUTTONDBLCLK()
	ON_WM_RBUTTONDOWN()
	ON_WM_CHAR()
	ON_WM_DROPFILES()
	ON_WM_MOUSEWHEEL()
	ON_COMMAND(ID_EDIT_UNDO,				OnEditUndo)
	ON_COMMAND(ID_EDIT_REDO,				OnEditRedo)
	ON_COMMAND(ID_EDIT_SELECT_ALL,			OnEditSelectAll)
	ON_COMMAND(ID_EDIT_CUT,					OnEditCut)
	ON_COMMAND(ID_EDIT_COPY,				OnEditCopy)
	ON_COMMAND(ID_EDIT_PASTE,				OnEditPaste)
	ON_COMMAND(ID_SAMPLE_SETLOOP,			OnSetLoop)
	ON_COMMAND(ID_SAMPLE_SETSUSTAINLOOP,	OnSetSustainLoop)
	ON_COMMAND(ID_SAMPLE_8BITCONVERT,		On8BitConvert)
	ON_COMMAND(ID_SAMPLE_16BITCONVERT,		On16BitConvert)
	ON_COMMAND(ID_SAMPLE_MONOCONVERT,		OnMonoConvertMix)
	ON_COMMAND(ID_SAMPLE_MONOCONVERT_LEFT,	OnMonoConvertLeft)
	ON_COMMAND(ID_SAMPLE_MONOCONVERT_RIGHT,	OnMonoConvertRight)
	ON_COMMAND(ID_SAMPLE_MONOCONVERT_SPLIT,	OnMonoConvertSplit)
	ON_COMMAND(ID_SAMPLE_TRIM,				OnSampleTrim)
	ON_COMMAND(ID_PREVINSTRUMENT,			OnPrevInstrument)
	ON_COMMAND(ID_NEXTINSTRUMENT,			OnNextInstrument)
	ON_COMMAND(ID_SAMPLE_ZOOMONSEL,			OnZoomOnSel)
	ON_COMMAND(ID_SAMPLE_SETLOOPSTART,		OnSetLoopStart)
	ON_COMMAND(ID_SAMPLE_SETLOOPEND,		OnSetLoopEnd)
	ON_COMMAND(ID_SAMPLE_SETSUSTAINSTART,	OnSetSustainStart)
	ON_COMMAND(ID_SAMPLE_SETSUSTAINEND,		OnSetSustainEnd)
	ON_COMMAND(ID_SAMPLE_ZOOMUP,			OnZoomUp)
	ON_COMMAND(ID_SAMPLE_ZOOMDOWN,			OnZoomDown)
	ON_COMMAND(ID_SAMPLE_DRAW,				OnDrawingToggle)
	ON_COMMAND(ID_SAMPLE_ADDSILENCE,		OnAddSilence)
	ON_COMMAND(ID_SAMPLE_GRID,				OnChangeGridSize)
	ON_COMMAND(ID_SAMPLE_QUICKFADE,			OnQuickFade)
	ON_UPDATE_COMMAND_UI(ID_EDIT_UNDO,		OnUpdateUndo)
	ON_UPDATE_COMMAND_UI(ID_EDIT_REDO,		OnUpdateRedo)
	ON_MESSAGE(WM_MOD_MIDIMSG,				OnMidiMsg)
	ON_MESSAGE(WM_MOD_KEYCOMMAND,			OnCustomKeyMsg) //rewbs.customKeys
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


///////////////////////////////////////////////////////////////
// CViewSample operations

CViewSample::CViewSample()
//------------------------
{
	m_nGridSegments = 0;
	m_nSample = 1;
	m_nZoom = 0;
	m_nBtnMouseOver = 0xFFFF;
	for(CHANNELINDEX i = 0; i < MAX_CHANNELS; i++)
	{
		m_dwNotifyPos[i] = Notification::PosInvalid;
	}
	MemsetZero(m_NcButtonState);
	m_bmpEnvBar.Create(&CMainFrame::GetMainFrame()->m_SampleIcons);

	m_lastDrawPoint.SetPoint(-1, -1);
	noteChannel.assign(NOTE_MAX - NOTE_MIN + 1, CHANNELINDEX_INVALID);
	offScreenDC = nullptr;
	offScreenBitmap = nullptr;
}


CViewSample::~CViewSample()
//-------------------------
{
	DeleteObject(offScreenBitmap);
	DeleteDC(offScreenDC);
}


void CViewSample::OnInitialUpdate()
//---------------------------------
{
	m_dwBeginSel = m_dwEndSel = 0;
	m_dwStatus.reset(SMPSTATUS_DRAWING);
	ModifyStyleEx(0, WS_EX_ACCEPTFILES);
	CModScrollView::OnInitialUpdate();
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	if (pMainFrm)
	{
		pMainFrm->SetInfoText("");
		pMainFrm->SetXInfoText("");
	}
	UpdateScrollSize();
}


// updateAll: Update all views including this one. Otherwise, only update update other views.
void CViewSample::SetModified(DWORD mask, bool updateAll)
//-------------------------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	pModDoc->SetModified();
	pModDoc->UpdateAllViews(nullptr, mask | (m_nSample << HINT_SHIFT_SMP), updateAll ? nullptr : this);
}


void CViewSample::UpdateScrollSize(int newZoom, bool forceRefresh, SmpLength centeredSample)
//------------------------------------------------------------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if(pModDoc == nullptr || (newZoom == m_nZoom && !forceRefresh))
	{
		return;
	}

	const int oldZoom = m_nZoom;
	m_nZoom = newZoom;

	GetClientRect(&m_rcClient);
	const CSoundFile &sndFile = pModDoc->GetrSoundFile();
	SIZE sizePage, sizeLine;
	SmpLength dwLen = 0;

	if ((m_nSample > 0) && (m_nSample <= sndFile.GetNumSamples()))
	{
		const ModSample &sample = sndFile.GetSample(m_nSample);
		if (sample.pSample != nullptr) dwLen = sample.nLength;
	}
	// Compute scroll size in pixels
	if (newZoom == 0)		// Fit to display
		m_sizeTotal.cx = m_rcClient.Width();
	else if(newZoom == 1)	// 1:1
		m_sizeTotal.cx = dwLen;
	else if(newZoom > 1)	// Zoom out
		m_sizeTotal.cx = (dwLen + (1 << (newZoom - 1)) - 1) >> (newZoom - 1);
	else					// Zoom in - here, we don't compute the real number of visible pixels so that the scroll bar doesn't grow unnecessarily long. The scrolling code in OnScrollBy() compensates for this.
		m_sizeTotal.cx = dwLen + m_rcClient.Width() - (m_rcClient.Width() >> (-newZoom - 1));

	m_sizeTotal.cy = 1;
	sizeLine.cx = (m_rcClient.right / 16) + 1;
	if(newZoom < 0)
		sizeLine.cx >>= (-newZoom - 1);
	sizeLine.cy = 1;
	sizePage.cx = sizeLine.cx * 4;
	sizePage.cy = 1;

	SetScrollSizes(MM_TEXT, m_sizeTotal, sizePage, sizeLine);

	if(oldZoom != newZoom) // After zoom change, keep the view position.
	{
		if(centeredSample != SmpLength(-1))
		{
			// Center given sample in the view
			int scrollToSample = centeredSample >> (std::max(1, newZoom) - 1);
			scrollToSample -= (m_rcClient.Width() / 2) >> (-std::min(-1, newZoom) - 1);

			Limit(scrollToSample, 0, GetScrollLimit(SB_HORZ));
			SetScrollPos(SB_HORZ, scrollToSample);
		} else
		{
			const SmpLength nOldPos = ScrollPosToSamplePos(oldZoom);
			const float fPosFraction = (dwLen > 0) ? static_cast<float>(nOldPos) / dwLen : 0;
			SetScrollPos(SB_HORZ, static_cast<int>(fPosFraction * GetScrollLimit(SB_HORZ)));
		}
	}
}


BOOL CViewSample::OnScrollBy(CSize sizeScroll, BOOL bDoScroll)
//------------------------------------------------------------
{
	int xOrig, x;

	// don't scroll if there is no valid scroll range (ie. no scroll bar)
	CScrollBar* pBar;
	DWORD dwStyle = GetStyle();
	pBar = GetScrollBarCtrl(SB_HORZ);
	if ((pBar != NULL && !pBar->IsWindowEnabled()) ||
		(pBar == NULL && !(dwStyle & WS_HSCROLL)))
	{
		// horizontal scroll bar not enabled
		sizeScroll.cx = 0;
	}

	// adjust current x position
	xOrig = x = GetScrollPos(SB_HORZ);
	int xMax = GetScrollLimit(SB_HORZ);
	x += sizeScroll.cx;
	if (x < 0)
		x = 0;
	else if (x > xMax)
		x = xMax;

	// did anything change?
	if (x == xOrig)
		return FALSE;

	if (bDoScroll)
	{
		// do scroll and update scroll positions
		int scrollBy = -(x - xOrig);
		// Don't allow to scroll into the middle of a sampling point
		if(m_nZoom < 0)
		{
			scrollBy *= (1 << (-m_nZoom - 1));
		}

		ScrollWindow(scrollBy, 0);
		if (x != xOrig)
			SetScrollPos(SB_HORZ, x);
	}
	return TRUE;
}


BOOL CViewSample::SetCurrentSample(SAMPLEINDEX nSmp)
//--------------------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	CSoundFile *pSndFile;

	if (!pModDoc) return FALSE;
	pSndFile = pModDoc->GetSoundFile();
	if ((nSmp < 1) || (nSmp > pSndFile->m_nSamples)) return FALSE;
	pModDoc->SetNotifications(Notification::Sample, nSmp);
	pModDoc->SetFollowWnd(m_hWnd);
	if (nSmp == m_nSample) return FALSE;
	m_dwBeginSel = m_dwEndSel = 0;
	m_dwStatus.reset(SMPSTATUS_DRAWING);
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	if (pMainFrm) pMainFrm->SetInfoText("");
	m_nSample = nSmp;
	for(CHANNELINDEX i = 0; i < MAX_CHANNELS; i++)
	{
		m_dwNotifyPos[i] = Notification::PosInvalid;
	}
	UpdateScrollSize();
	UpdateNcButtonState();
	InvalidateRect(NULL, FALSE);
	return TRUE;
}


BOOL CViewSample::SetZoom(int nZoom, SmpLength centeredSample)
//------------------------------------------------------------
{

	if (nZoom == m_nZoom)
		return TRUE;
	if (nZoom > MAX_ZOOM)
		return FALSE;

	UpdateScrollSize(nZoom, true, centeredSample);
	InvalidateRect(NULL, FALSE);
	return TRUE;
}


void CViewSample::SetCurSel(SmpLength nBegin, SmpLength nEnd)
//-----------------------------------------------------------
{
	CSoundFile *pSndFile = (GetDocument()) ? GetDocument()->GetSoundFile() : nullptr;
	if(pSndFile == nullptr)
		return;

	const ModSample &sample = pSndFile->GetSample(m_nSample);

	// Snap to grid
	if(m_nGridSegments > 0 && m_nGridSegments < sample.nLength)
	{
		const float sampsPerSegment = (float)(sample.nLength / m_nGridSegments);
		nBegin = (SmpLength)(Util::Round((float)(nBegin / sampsPerSegment)) * sampsPerSegment);
		nEnd = (SmpLength)(Util::Round((float)(nEnd / sampsPerSegment)) * sampsPerSegment);
	}

	if (nBegin > nEnd)
	{
		std::swap(nBegin, nEnd);
	}

	if ((nBegin != m_dwBeginSel) || (nEnd != m_dwEndSel))
	{
		RECT rect;
		SmpLength dMin = m_dwBeginSel, dMax = m_dwEndSel;
		if (m_dwBeginSel >= m_dwEndSel)
		{
			dMin = nBegin;
			dMax = nEnd;
		}
		if ((nBegin == dMin) && (dMax != nEnd))
		{
			dMin = dMax;
			if (nEnd < dMin) dMin = nEnd;
			if (nEnd > dMax) dMax = nEnd;
		} else if ((nEnd == dMax) && (dMin != nBegin))
		{
			dMax = dMin;
			if (nBegin < dMin) dMin = nBegin;
			if (nBegin > dMax) dMax = nBegin;
		} else
		{
			if (nBegin < dMin) dMin = nBegin;
			if (nEnd > dMax) dMax = nEnd;
		}
		m_dwBeginSel = nBegin;
		m_dwEndSel = nEnd;
		rect.top = m_rcClient.top;
		rect.bottom = m_rcClient.bottom;
		rect.left = SampleToScreen(dMin);
		rect.right = SampleToScreen(dMax) + 1;
		if (rect.left < 0) rect.left = 0;
		if (rect.right > m_rcClient.right) rect.right = m_rcClient.right;
		if (rect.right > rect.left) InvalidateRect(&rect, FALSE);
		CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
		if(pMainFrm)
		{
			std::string s;
			if(m_dwEndSel > m_dwBeginSel)
			{
				const SmpLength selLength = m_dwEndSel - m_dwBeginSel;
				s = mpt::String::Print("[%1,%2] (%3 sample%4, ", m_dwBeginSel, m_dwEndSel, selLength, (selLength == 1) ? "" : "s");

				uint32 lSampleRate = pSndFile->GetSample(m_nSample).nC5Speed;
				if(pSndFile->GetType() & (MOD_TYPE_MOD|MOD_TYPE_XM))
				{
					lSampleRate = ModSample::TransposeToFrequency(pSndFile->GetSample(m_nSample).RelativeTone, pSndFile->GetSample(m_nSample).nFineTune);
				}
				if (!lSampleRate) lSampleRate = 8363;
				uint64 msec = (uint64(selLength) * 1000) / lSampleRate;
				if(msec < 1000)
					s += mpt::String::Print("%1ms)", msec);
				else
					s += mpt::String::Print("%1.%2s)", msec / 1000, mpt::fmt::dec0<2>((msec / 10) % 100));
			}
			pMainFrm->SetInfoText(s.c_str());
		}
	}
}


int32 CViewSample::SampleToScreen(SmpLength pos) const
//----------------------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if ((pModDoc) && (m_nSample <= pModDoc->GetNumSamples()))
	{
		SmpLength nLen = pModDoc->GetrSoundFile().GetSample(m_nSample).nLength;
		if (!nLen) return 0;

		if(m_nZoom > 0)
			return (pos >> (m_nZoom - 1)) - m_nScrollPosX;
		else if(m_nZoom < 0)
			return (pos - m_nScrollPosX) << (-m_nZoom - 1);
		else
			return Util::muldiv(pos, m_sizeTotal.cx, nLen);
	}
	return 0;
}


SmpLength CViewSample::ScreenToSample(int32 x) const
//--------------------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	SmpLength n = 0;

	if ((pModDoc) && (m_nSample <= pModDoc->GetNumSamples()))
	{
		SmpLength nLen = pModDoc->GetrSoundFile().GetSample(m_nSample).nLength;
		if (!nLen) return 0;

		if(m_nZoom > 0)
			n = (m_nScrollPosX + x) << (m_nZoom - 1);
		else if(m_nZoom < 0)
			n = m_nScrollPosX + (x >> (-m_nZoom - 1));
		else
		{
			if (x < 0) x = 0;
			if (m_sizeTotal.cx) n = Util::muldiv(x, nLen, m_sizeTotal.cx);
		}
		if (n < 0) n = 0;
		LimitMax(n, nLen);
	}
	return n;
}


void CViewSample::InvalidateSample()
//----------------------------------
{
	InvalidateRect(NULL, FALSE);
}


LRESULT CViewSample::OnModViewMsg(WPARAM wParam, LPARAM lParam)
//-------------------------------------------------------------
{
	switch(wParam)
	{
	case VIEWMSG_SETCURRENTSAMPLE:
		SetZoom(static_cast<int>(lParam) >> 16);
		SetCurrentSample(lParam & 0xFFFF);
		break;

	case VIEWMSG_LOADSTATE:
		if (lParam)
		{
			SAMPLEVIEWSTATE *pState = (SAMPLEVIEWSTATE *)lParam;
			if (pState->nSample == m_nSample)
			{
				SetCurSel(pState->dwBeginSel, pState->dwEndSel);
				SetScrollPos(SB_HORZ, pState->dwScrollPos);
				UpdateScrollSize();
				InvalidateSample();
			}
		}
		break;

	case VIEWMSG_SAVESTATE:
		if (lParam)
		{
			SAMPLEVIEWSTATE *pState = (SAMPLEVIEWSTATE *)lParam;
			pState->dwScrollPos = m_nScrollPosX;
			pState->dwBeginSel = m_dwBeginSel;
			pState->dwEndSel = m_dwEndSel;
			pState->nSample = m_nSample;
		}
		break;

	default:
		return CModScrollView::OnModViewMsg(wParam, lParam);
	}
	return 0;
}


///////////////////////////////////////////////////////////////
// CViewSample drawing

void CViewSample::UpdateView(DWORD dwHintMask, CObject *hint)
//-----------------------------------------------------------
{
	if(hint == this)
	{
		return;
	}
	const SAMPLEINDEX updateSmp = (dwHintMask >> HINT_SHIFT_SMP);
	if((dwHintMask & (HINT_MPTOPTIONS | HINT_MODTYPE))
		|| ((dwHintMask & HINT_SAMPLEDATA) && (m_nSample == updateSmp || updateSmp == 0)))
	{
		UpdateScrollSize();
		UpdateNcButtonState();
		InvalidateSample();
	}

	// sample drawing
	if(dwHintMask & HINT_SAMPLEINFO)
	{
		m_dwStatus.reset(SMPSTATUS_DRAWING);
		UpdateNcButtonState();
	}
}

#define YCVT(n, bits)		(ymed - (((n) * yrange) >> (bits)))


// Draw one channel of sample data, 1:1 ratio or higher (zoomed in)
void CViewSample::DrawSampleData1(HDC hdc, int ymed, int cx, int cy, SmpLength len, int uFlags, const void *pSampleData)
//----------------------------------------------------------------------------------------------------------------------
{
	int smplsize;
	int yrange = cy/2;
	const int8 *psample = static_cast<const int8 *>(pSampleData);
	int y0 = 0;

	smplsize = (uFlags & CHN_16BIT) ? 2 : 1;
	if (uFlags & CHN_STEREO) smplsize *= 2;
	if (uFlags & CHN_16BIT)
	{
		y0 = YCVT(*((signed short *)(psample-smplsize)),15);
	} else
	{
		y0 = YCVT(*(psample-smplsize),7);
	}
	::MoveToEx(hdc, -1, y0, NULL);

	SmpLength numDrawSamples, loopDiv = 0;
	int loopShift = 0;
	if (m_nZoom == 1)
	{
		// Linear 1:1 scale
		numDrawSamples = cx;
	} else if(m_nZoom < 0)
	{
		// 2:1, 4:1, etc... zoom
		loopShift = (-m_nZoom - 1);
		// Round up
		numDrawSamples = (cx + (1 << loopShift) - 1) >> loopShift;
	} else
	{
		// Stretch to screen
		ASSERT(!m_nZoom);
		numDrawSamples = len;
		loopDiv = numDrawSamples;
	}
	LimitMax(numDrawSamples, len);

	if (uFlags & CHN_16BIT)
	{
		// 16-Bit
		for (SmpLength n = 0; n <= numDrawSamples; n++)
		{
			int x = loopDiv ? ((n * cx) / loopDiv) : (n << loopShift);
			int y = *(const int16 *)psample;
			::LineTo(hdc, x, YCVT(y,15));
			psample += smplsize;
		}
	} else
	{
		// 8-bit
		for (SmpLength n = 0; n <= numDrawSamples; n++)
		{
			int x = loopDiv ? ((n * cx) / loopDiv) : (n << loopShift);
			int y = *psample;
			::LineTo(hdc, x, YCVT(y,7));
			psample += smplsize;
		}
	}
}


#if defined(ENABLE_X86_AMD) || defined(ENABLE_SSE)

#include <mmintrin.h>

// AMD MMX/SSE implementation for min/max finder, packs 4*int16 in a 64-bit MMX register.
// scanlen = How many samples to process on this channel
static void amdmmxext_or_sse_findminmax16(const void *p, SmpLength scanlen, int channels, int &smin, int &smax)
//-------------------------------------------------------------------------------------------------------------
{
	scanlen *= channels;
	__m64 minVal = _mm_cvtsi32_si64(smin);
	__m64 maxVal = _mm_cvtsi32_si64(smax);

	// Put minimum / maximum in 4 packed int16 values
	minVal = _mm_unpacklo_pi16(minVal, minVal);
	maxVal = _mm_unpacklo_pi16(maxVal, maxVal);
	minVal = _mm_unpacklo_pi32(minVal, minVal);
	maxVal = _mm_unpacklo_pi32(maxVal, maxVal);

	SmpLength scanlen4 = scanlen / 4;
	if(scanlen4)
	{
		const __m64 *v = static_cast<const __m64 *>(p);
		p = static_cast<const __m64 *>(p) + scanlen4;

		while(scanlen4--)
		{
			__m64 curVals = *(v++);
			minVal = _mm_min_pi16(minVal, curVals);
			maxVal = _mm_max_pi16(maxVal, curVals);
		}

		// Now we have 4 minima and maxima each, in case of stereo they are interleaved L/R values.
		// Move the upper 2 values to the lower half and compute the minima/maxima of that.
		__m64 minVal2 = _mm_unpackhi_pi32(minVal, minVal);
		__m64 maxVal2 = _mm_unpackhi_pi32(maxVal, maxVal);
		minVal = _mm_min_pi16(minVal, minVal2);
		maxVal = _mm_max_pi16(maxVal, maxVal2);

		if(channels < 2)
		{
			// Mono: Compute the minima/maxima of the both remaining values
			minVal2 = _mm_sra_pi32(minVal, _mm_cvtsi32_si64(16));
			maxVal2 = _mm_sra_pi32(maxVal, _mm_cvtsi32_si64(16));
			minVal = _mm_min_pi16(minVal, minVal2);
			maxVal = _mm_max_pi16(maxVal, maxVal2);
		}

		ASSERT(p == v);
	}

	const int16 *p16 = static_cast<const int16 *>(p);
	while(scanlen & 3)
	{
		scanlen -= channels;
		__m64 curVals = _mm_cvtsi32_si64(*p16);
		p16 += channels;
		minVal = _mm_min_pi16(minVal, curVals);
		maxVal = _mm_max_pi16(maxVal, curVals);
	}

	smin = static_cast<int16>(_mm_cvtsi64_si32(minVal));
	smax = static_cast<int16>(_mm_cvtsi64_si32(maxVal));

	_mm_empty();
}


// AMD MMX/SSE implementation for min/max finder, packs 8*int8 in a 64-bit MMX register.
// scanlen = How many samples to process on this channel
static void amdmmxext_or_sse_findminmax8(const void *p, SmpLength scanlen, int channels, int &smin, int &smax)
//------------------------------------------------------------------------------------------------------------
{
	scanlen *= channels;

	__m64 minVal = _mm_cvtsi32_si64(smin);
	__m64 maxVal = _mm_cvtsi32_si64(smax);

	// For signed <-> unsigned conversion
	__m64 xorVal = _mm_cvtsi32_si64(0x80808080);
	xorVal = _mm_unpacklo_pi32(xorVal, xorVal);

	// Put minimum / maximum in 8 packed uint8 values
	minVal = _mm_unpacklo_pi8(minVal, minVal);
	maxVal = _mm_unpacklo_pi8(maxVal, maxVal);
	minVal = _mm_unpacklo_pi16(minVal, minVal);
	maxVal = _mm_unpacklo_pi16(maxVal, maxVal);
	minVal = _mm_unpacklo_pi32(minVal, minVal);
	maxVal = _mm_unpacklo_pi32(maxVal, maxVal);
	minVal = _mm_xor_si64(minVal, xorVal);
	maxVal = _mm_xor_si64(maxVal, xorVal);

	SmpLength scanlen8 = scanlen / 8;
	if(scanlen8)
	{
		const __m64 *v = static_cast<const __m64 *>(p);
		p = static_cast<const __m64 *>(p) + scanlen8;

		while(scanlen8--)
		{
			__m64 curVals = _mm_xor_si64(*(v++), xorVal);
			minVal = _mm_min_pu8(minVal, curVals);
			maxVal = _mm_max_pu8(maxVal, curVals);
		}

		// Now we have 8 minima and maxima each, in case of stereo they are interleaved L/R values.
		// Move the upper 4 values to the lower half and compute the minima/maxima of that.
		__m64 minVal2 = _mm_unpackhi_pi32(minVal, minVal);
		__m64 maxVal2 = _mm_unpackhi_pi32(maxVal, maxVal);
		minVal = _mm_min_pu8(minVal, minVal2);
		maxVal = _mm_max_pu8(maxVal, maxVal2);

		// Now we have 4 minima and maxima each, in case of stereo they are interleaved L/R values.
		// Move the upper 2 values to the lower half and compute the minima/maxima of that.
		minVal2 = _mm_srl_pi32(minVal, _mm_cvtsi32_si64(16));
		maxVal2 = _mm_srl_pi32(maxVal, _mm_cvtsi32_si64(16));
		minVal = _mm_min_pu8(minVal, minVal2);
		maxVal = _mm_max_pu8(maxVal, maxVal2);

		if(channels < 2)
		{
			// Mono: Compute the minima/maxima of the both remaining values
			minVal2 = _mm_srl_pi32(minVal, _mm_cvtsi32_si64(8));
			maxVal2 = _mm_srl_pi32(maxVal, _mm_cvtsi32_si64(8));
			minVal = _mm_min_pu8(minVal, minVal2);
			maxVal = _mm_max_pu8(maxVal, maxVal2);
		}

		ASSERT(p == v);
	}

	const int8 *p8 = static_cast<const int8 *>(p);
	while(scanlen & 7)
	{
		scanlen -= channels;
		__m64 curVals = _mm_xor_si64(_mm_cvtsi32_si64(*p8), xorVal);
		p8 += channels;
		minVal = _mm_min_pu8(minVal, curVals);
		maxVal = _mm_max_pu8(maxVal, curVals);
	}

	minVal = _mm_xor_si64(minVal, xorVal);
	maxVal = _mm_xor_si64(maxVal, xorVal);
	smin = static_cast<int8>(_mm_cvtsi64_si32(minVal));
	smax = static_cast<int8>(_mm_cvtsi64_si32(maxVal));

	_mm_empty();
}

#elif defined(ENABLE_SSE2)

#include <emmintrin.h>

// SSE2 implementation for min/max finder, packs 8*int16 in a 128-bit XMM register.
// scanlen = How many samples to process on this channel
static void sse2_findminmax16(const void *p, SmpLength scanlen, int channels, int &smin, int &smax)
//-------------------------------------------------------------------------------------------------
{
	scanlen *= channels;

	// Put minimum / maximum in 8 packed int16 values
	__m128i minVal = _mm_set1_epi16(static_cast<int16>(smin));
	__m128i maxVal = _mm_set1_epi16(static_cast<int16>(smax));

	SmpLength scanlen8 = scanlen / 8;
	if(scanlen8)
	{
		const __m128i *v = static_cast<const __m128i *>(p);
		p = static_cast<const __m128i *>(p) + scanlen8;

		while(scanlen8--)
		{
			__m128i curVals = _mm_loadu_si128(v++);
			minVal = _mm_min_epi16(minVal, curVals);
			maxVal = _mm_max_epi16(maxVal, curVals);
		}

		// Now we have 8 minima and maxima each, in case of stereo they are interleaved L/R values.
		// Move the upper 4 values to the lower half and compute the minima/maxima of that.
		__m128i minVal2 = _mm_unpackhi_epi64(minVal, minVal);
		__m128i maxVal2 = _mm_unpackhi_epi64(maxVal, maxVal);
		minVal = _mm_min_epi16(minVal, minVal2);
		maxVal = _mm_max_epi16(maxVal, maxVal2);

		// Now we have 4 minima and maxima each, in case of stereo they are interleaved L/R values.
		// Move the upper 2 values to the lower half and compute the minima/maxima of that.
		minVal2 = _mm_shuffle_epi32(minVal, _MM_SHUFFLE(1, 1, 1, 1));
		maxVal2 = _mm_shuffle_epi32(maxVal, _MM_SHUFFLE(1, 1, 1, 1));
		minVal = _mm_min_epi16(minVal, minVal2);
		maxVal = _mm_max_epi16(maxVal, maxVal2);

		if(channels < 2)
		{
			// Mono: Compute the minima/maxima of the both remaining values
			minVal2 = _mm_shufflelo_epi16(minVal, _MM_SHUFFLE(1, 1, 1, 1));
			maxVal2 = _mm_shufflelo_epi16(maxVal, _MM_SHUFFLE(1, 1, 1, 1));
			minVal = _mm_min_epi16(minVal, minVal2);
			maxVal = _mm_max_epi16(maxVal, maxVal2);
		}
	}

	const int16 *p16 = static_cast<const int16 *>(p);
	while(scanlen & 7)
	{
		scanlen -= channels;
		__m128i curVals = _mm_set1_epi16(*p16);
		p16 += channels;
		minVal = _mm_min_epi16(minVal, curVals);
		maxVal = _mm_max_epi16(maxVal, curVals);
	}

	smin = static_cast<int16>(_mm_cvtsi128_si32(minVal));
	smax = static_cast<int16>(_mm_cvtsi128_si32(maxVal));
}


// SSE2 implementation for min/max finder, packs 16*int8 in a 128-bit XMM register.
// scanlen = How many samples to process on this channel
static void sse2_findminmax8(const void *p, SmpLength scanlen, int channels, int &smin, int &smax)
//------------------------------------------------------------------------------------------------
{
	scanlen *= channels;

	// Put minimum / maximum in 16 packed int8 values
	__m128i minVal = _mm_set1_epi8(static_cast<int8>(smin ^ 0x80u));
	__m128i maxVal = _mm_set1_epi8(static_cast<int8>(smax ^ 0x80u));

	// For signed <-> unsigned conversion (_mm_min_epi8/_mm_max_epi8 is SSE4)
	__m128i xorVal = _mm_set1_epi8(0x80u);

	SmpLength scanlen16 = scanlen / 16;
	if(scanlen16)
	{
		const __m128i *v = static_cast<const __m128i *>(p);
		p = static_cast<const __m128i *>(p) + scanlen16;

		while(scanlen16--)
		{
			__m128i curVals = _mm_loadu_si128(v++);
			curVals = _mm_xor_si128(curVals, xorVal);
			minVal = _mm_min_epu8(minVal, curVals);
			maxVal = _mm_max_epu8(maxVal, curVals);
		}

		// Now we have 16 minima and maxima each, in case of stereo they are interleaved L/R values.
		// Move the upper 8 values to the lower half and compute the minima/maxima of that.
		__m128i minVal2 = _mm_unpackhi_epi64(minVal, minVal);
		__m128i maxVal2 = _mm_unpackhi_epi64(maxVal, maxVal);
		minVal = _mm_min_epu8(minVal, minVal2);
		maxVal = _mm_max_epu8(maxVal, maxVal2);

		// Now we have 8 minima and maxima each, in case of stereo they are interleaved L/R values.
		// Move the upper 4 values to the lower half and compute the minima/maxima of that.
		minVal2 = _mm_shuffle_epi32(minVal, _MM_SHUFFLE(1, 1, 1, 1));
		maxVal2 = _mm_shuffle_epi32(maxVal, _MM_SHUFFLE(1, 1, 1, 1));
		minVal = _mm_min_epu8(minVal, minVal2);
		maxVal = _mm_max_epu8(maxVal, maxVal2);

		// Now we have 4 minima and maxima each, in case of stereo they are interleaved L/R values.
		// Move the upper 2 values to the lower half and compute the minima/maxima of that.
		minVal2 = _mm_srai_epi32(minVal, 16);
		maxVal2 = _mm_srai_epi32(maxVal, 16);
		minVal = _mm_min_epu8(minVal, minVal2);
		maxVal = _mm_max_epu8(maxVal, maxVal2);

		if(channels < 2)
		{
			// Mono: Compute the minima/maxima of the both remaining values
			minVal2 = _mm_srai_epi16(minVal, 8);
			maxVal2 = _mm_srai_epi16(maxVal, 8);
			minVal = _mm_min_epu8(minVal, minVal2);
			maxVal = _mm_max_epu8(maxVal, maxVal2);
		}
	}

	const int8 *p8 = static_cast<const int8 *>(p);
	while(scanlen & 15)
	{
		scanlen -= channels;
		__m128i curVals = _mm_set1_epi8((*p8) ^ 0x80u);
		p8 += channels;
		minVal = _mm_min_epu8(minVal, curVals);
		maxVal = _mm_max_epu8(maxVal, curVals);
	}

	smin = static_cast<int8>(_mm_cvtsi128_si32(minVal) ^ 0x80u);
	smax = static_cast<int8>(_mm_cvtsi128_si32(maxVal) ^ 0x80u);
}


#endif // defined(ENABLE_SSE)


// Draw one channel of zoomed-out sample data
void CViewSample::DrawSampleData2(HDC hdc, int ymed, int cx, int cy, SmpLength len, int uFlags, const void *pSampleData)
//----------------------------------------------------------------------------------------------------------------------
{
	int oldsmin, oldsmax;
	int yrange = cy/2;
	const int8 *psample = static_cast<const int8 *>(pSampleData);
	int32 y0 = 0, xmax;
	SmpLength poshi;
	uint64 posincr, posfrac;	// Increments have 16-bit fractional part

	if (len <= 0) return;
	const int numChannels = (uFlags & CHN_STEREO) ? 2 : 1;
	const int smplsize = ((uFlags & CHN_16BIT) ? 2 : 1) * numChannels;

	if (uFlags & CHN_16BIT)
	{
		y0 = YCVT(*((const int16 *)(psample-smplsize)), 15);
	} else
	{
		y0 = YCVT(*(psample-smplsize), 7);
	}
	oldsmin = oldsmax = y0;
	if (m_nZoom > 0)
	{
		xmax = len>>(m_nZoom-1);
		if (xmax > cx) xmax = cx;
		posincr = (uint64(1) << (m_nZoom-1+16));
	} else
	{
		xmax = cx;
		//posincr = Util::muldiv(len, 0x10000, cx);
		posincr = uint64(len) * uint64(0x10000) / uint64(cx);
	}
	::MoveToEx(hdc, 0, ymed, NULL);
	posfrac = 0;
	poshi = 0;
	for (int x=0; x<xmax; x++)
	{
		//int smin, smax, scanlen;
		int smin, smax;
		SmpLength scanlen;

		posfrac += posincr;
		scanlen = static_cast<int32>((posfrac+0xffff) >> 16);
		if (poshi >= len) poshi = len-1;
		if (poshi + scanlen > len) scanlen = len-poshi;
		if (scanlen < 1) scanlen = 1;
		// 16-bit
		if (uFlags & CHN_16BIT)
		{
			signed short *p = (signed short *)(psample + poshi*smplsize);
			smin = 32767;
			smax = -32768;
#if defined(ENABLE_X86_AMD) || defined(ENABLE_SSE)
			if(GetProcSupport() & (PROCSUPPORT_AMD_MMXEXT|PROCSUPPORT_SSE))
			{
				amdmmxext_or_sse_findminmax16(p, scanlen, numChannels, smin, smax);
			} else
#elif defined(ENABLE_SSE2)
			if(GetProcSupport() & PROCSUPPORT_SSE2)
			{
				sse2_findminmax16(p, scanlen, numChannels, smin, smax);
			} else
#endif
			{
				for (SmpLength i = 0; i < scanlen; i++)
				{
					int s = *p;
					if (s < smin) smin = s;
					if (s > smax) smax = s;
					p += numChannels;
				}
			}
			smin = YCVT(smin,15);
			smax = YCVT(smax,15);
		} else
		// 8-bit
		{
			const int8 *p = psample + poshi * smplsize;
			smin = 127;
			smax = -128;
#if defined(ENABLE_X86_AMD) || defined(ENABLE_SSE)
			if(GetProcSupport() & (PROCSUPPORT_AMD_MMXEXT|PROCSUPPORT_SSE))
			{
				amdmmxext_or_sse_findminmax8(p, scanlen, numChannels, smin, smax);
			} else
#elif defined(ENABLE_SSE2)
			if(GetProcSupport() & PROCSUPPORT_SSE2)
			{
				sse2_findminmax8(p, scanlen, numChannels, smin, smax);
			} else
#endif
			{
				for (SmpLength i = 0; i < scanlen; i++)
				{

					int s = *p;
					if (s < smin) smin = s;
					if (s > smax) smax = s;
					p += numChannels;
				}
			}
			smin = YCVT(smin,7);
			smax = YCVT(smax,7);
		}
		if (smin > oldsmax)
		{
			::MoveToEx(hdc, x-1, oldsmax - 1, NULL);
			::LineTo(hdc, x, smin);
		}
		if (smax < oldsmin)
		{
			::MoveToEx(hdc, x-1, oldsmin, NULL);
			::LineTo(hdc, x, smax);
		}
		::MoveToEx(hdc, x, smax-1, NULL);
		::LineTo(hdc, x, smin);
		oldsmin = smin;
		oldsmax = smax;
		poshi += static_cast<int32>(posfrac>>16);
		posfrac &= 0xffff;
	}
}


void CViewSample::OnDraw(CDC *pDC)
//--------------------------------
{
	CRect rcClient = m_rcClient, rect, rc;
	CModDoc *pModDoc = GetDocument();
	HGDIOBJ oldpen;

	UINT nSmpScrollPos = ScrollPosToSamplePos();

	if ((!pModDoc) || (!pDC)) return;

	const CSoundFile &sndFile = pModDoc->GetrSoundFile();
	const ModSample &sample = sndFile.GetSample((m_nSample <= sndFile.GetNumSamples()) ? m_nSample : 0);
	
	// Create off-screen image
	if(offScreenDC == nullptr)
	{
		offScreenDC = CreateCompatibleDC(pDC->m_hDC);
		offScreenBitmap = CreateCompatibleBitmap(pDC->m_hDC, m_rcClient.Width(), m_rcClient.Height());
		SelectObject(offScreenDC, offScreenBitmap);
	}

	offScreenDC = offScreenDC;
	oldpen = ::SelectObject(offScreenDC, CMainFrame::penBlack);
	rect = rcClient;
	if ((rcClient.bottom > rcClient.top) && (rcClient.right > rcClient.left))
	{
		int ymed = (rect.top + rect.bottom) / 2;
		int yrange = (rect.bottom - rect.top) / 2;

		// Erase background
		if ((m_dwBeginSel < m_dwEndSel) && (m_dwEndSel > nSmpScrollPos))
		{
			rc = rect;
			if (m_dwBeginSel > nSmpScrollPos)
			{
				rc.right = SampleToScreen(m_dwBeginSel);
				if (rc.right > rcClient.right) rc.right = rcClient.right;
				if (rc.right > rc.left) ::FillRect(offScreenDC, &rc, CMainFrame::brushBlack);
				rc.left = rc.right;
			}
			if (rc.left < 0) rc.left = 0;
			rc.right = SampleToScreen(m_dwEndSel) + 1;
			if (rc.right > rcClient.right) rc.right = rcClient.right;
			if (rc.right > rc.left) ::FillRect(offScreenDC, &rc, CMainFrame::brushWhite);
			rc.left = rc.right;
			if (rc.left < 0) rc.left = 0;
			rc.right = rcClient.right;
			if (rc.right > rc.left) ::FillRect(offScreenDC, &rc, CMainFrame::brushBlack);
		} else
		{
			::FillRect(offScreenDC, &rcClient, CMainFrame::brushBlack);
		}
		::SelectObject(offScreenDC, CMainFrame::penDarkGray);
		if (sample.uFlags[CHN_STEREO])
		{
			::MoveToEx(offScreenDC, 0, ymed-yrange/2, NULL);
			::LineTo(offScreenDC, rcClient.right, ymed-yrange/2);
			::MoveToEx(offScreenDC, 0, ymed+yrange/2, NULL);
			::LineTo(offScreenDC, rcClient.right, ymed+yrange/2);
		} else
		{
			::MoveToEx(offScreenDC, 0, ymed, NULL);
			::LineTo(offScreenDC, rcClient.right, ymed);
		}
		// Drawing sample
		if ((sample.pSample) && (yrange) && (sample.nLength > 1) && (rect.right > 1))
		{
			// Loop Start/End
			if ((sample.nLoopEnd > nSmpScrollPos) && (sample.nLoopEnd > sample.nLoopStart))
			{
				int xl = SampleToScreen(sample.nLoopStart);
				if ((xl >= 0) && (xl < rcClient.right))
				{
					::MoveToEx(offScreenDC, xl, rect.top, NULL);
					::LineTo(offScreenDC, xl, rect.bottom);
				}
				xl = SampleToScreen(sample.nLoopEnd);
				if ((xl >= 0) && (xl < rcClient.right))
				{
					::MoveToEx(offScreenDC, xl, rect.top, NULL);
					::LineTo(offScreenDC, xl, rect.bottom);
				}
			}
			// Sustain Loop Start/End
			if ((sample.nSustainEnd > nSmpScrollPos) && (sample.nSustainEnd > sample.nSustainStart))
			{
				::SelectObject(offScreenDC, CMainFrame::penHalfDarkGray);
				int xl = SampleToScreen(sample.nSustainStart);
				if ((xl >= 0) && (xl < rcClient.right))
				{
					::MoveToEx(offScreenDC, xl, rect.top, NULL);
					::LineTo(offScreenDC, xl, rect.bottom);
				}
				xl = SampleToScreen(sample.nSustainEnd);
				if ((xl >= 0) && (xl < rcClient.right))
				{
					::MoveToEx(offScreenDC, xl, rect.top, NULL);
					::LineTo(offScreenDC, xl, rect.bottom);
				}
			}
			// Drawing Sample Data
			::SelectObject(offScreenDC, CMainFrame::penSample);
			int smplsize = sample.GetBytesPerSample();
			if (m_nZoom == 1 || m_nZoom < 0 || ((!m_nZoom) && (sample.nLength <= (SmpLength)rect.Width())))
			{
				// Draw sample data in 1:1 ratio or higher (zoom in)
				SmpLength len = sample.nLength - nSmpScrollPos;
				signed char *psample = ((signed char *)sample.pSample) + nSmpScrollPos * smplsize;
				if (sample.uFlags[CHN_STEREO])
				{
					DrawSampleData1(offScreenDC, ymed-yrange/2, rect.right, yrange, len, sample.uFlags, psample);
					DrawSampleData1(offScreenDC, ymed+yrange/2, rect.right, yrange, len, sample.uFlags, psample+smplsize/2);
				} else
				{
					DrawSampleData1(offScreenDC, ymed, rect.right, yrange*2, len, sample.uFlags, psample);
				}
			} else
			{
				// Draw zoomed-out saple data
				SmpLength len = sample.nLength;
				int xscroll = 0;
				if (m_nZoom > 0)
				{
					xscroll = nSmpScrollPos;
					len -= nSmpScrollPos;
				}
				signed char *psample = ((signed char *)sample.pSample) + xscroll * smplsize;
				if (sample.uFlags[CHN_STEREO])
				{
					DrawSampleData2(offScreenDC, ymed-yrange/2, rect.right, yrange, len, sample.uFlags, psample);
					DrawSampleData2(offScreenDC, ymed+yrange/2, rect.right, yrange, len, sample.uFlags, psample+smplsize/2);
				} else
				{
					DrawSampleData2(offScreenDC, ymed, rect.right, yrange*2, len, sample.uFlags, psample);
				}
			}
		}
	}

	if(m_nGridSegments && m_nGridSegments < sample.nLength)
	{
		// Draw sample grid
		::SelectObject(offScreenDC, CMainFrame::penHalfDarkGray);
		for(SmpLength i = 1; i < m_nGridSegments; i++)
		{
			int screenPos = SampleToScreen(sample.nLength * i / m_nGridSegments);
			if(screenPos >= rect.left && screenPos <= rect.right)
			{
				::MoveToEx(offScreenDC, screenPos, rect.top, nullptr);
				::LineTo(offScreenDC, screenPos, rect.bottom);
			}
		}
	}

	DrawPositionMarks();

	BitBlt(pDC->m_hDC, m_rcClient.left, m_rcClient.top, m_rcClient.Width(), m_rcClient.Height(), offScreenDC, 0, 0, SRCCOPY);

	if (oldpen) ::SelectObject(offScreenDC, oldpen);

	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	bool activeDoc = pMainFrm ? pMainFrm->GetActiveDoc() == GetDocument() : false;

	if(activeDoc && CChannelManagerDlg::sharedInstance(FALSE) && CChannelManagerDlg::sharedInstance()->IsDisplayed())
		CChannelManagerDlg::sharedInstance()->SetDocument((void*)this);
}


void CViewSample::DrawPositionMarks()
//-----------------------------------
{
	if(GetDocument()->GetrSoundFile().GetSample(m_nSample).pSample == nullptr)
	{
		return;
	}
	CRect rect;
	for(CHANNELINDEX i = 0; i < MAX_CHANNELS; i++) if (m_dwNotifyPos[i] != Notification::PosInvalid)
	{
		rect.top = -2;
		rect.left = SampleToScreen(m_dwNotifyPos[i]);
		rect.right = rect.left + 1;
		rect.bottom = m_rcClient.bottom + 1;
		if ((rect.right >= 0) && (rect.right < m_rcClient.right)) InvertRect(offScreenDC, &rect);
	}
}


LRESULT CViewSample::OnPlayerNotify(Notification *pnotify)
//--------------------------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if ((!pnotify) || (!pModDoc)) return 0;
	if (pnotify->type[Notification::Stop])
	{
		bool invalidate = false;
		for(CHANNELINDEX i = 0; i < MAX_CHANNELS; i++)
		{
			if(m_dwNotifyPos[i] != Notification::PosInvalid)
			{
				m_dwNotifyPos[i] = Notification::PosInvalid;
				invalidate = true;
			}
		}
		if(invalidate)
		{
			InvalidateRect(NULL, FALSE);
		}
	} else if (pnotify->type[Notification::Sample] && pnotify->item == m_nSample)
	{
		bool doUpdate = false;
		for(CHANNELINDEX i = 0; i < MAX_CHANNELS; i++)
		{
			if (m_dwNotifyPos[i] != pnotify->pos[i])
			{
				doUpdate = true;
				break;
			}
		}
		if (doUpdate)
		{
			HDC hdc = ::GetDC(m_hWnd);
			DrawPositionMarks();	// Erase old marks...
			for(CHANNELINDEX i = 0; i < MAX_CHANNELS; i++)
			{
				m_dwNotifyPos[i] = pnotify->pos[i];
			}
			DrawPositionMarks();	// ...and draw new ones
			BitBlt(hdc, m_rcClient.left, m_rcClient.top, m_rcClient.Width(), m_rcClient.Height(), offScreenDC, 0, 0, SRCCOPY);
			::ReleaseDC(m_hWnd, hdc);
		}
	}
	return 0;
}


BOOL CViewSample::GetNcButtonRect(UINT nBtn, LPRECT lpRect)
//---------------------------------------------------------
{
	lpRect->left = 4;
	lpRect->top = 3;
	lpRect->bottom = lpRect->top + SMP_LEFTBAR_CYBTN;
	if (nBtn >= SMP_LEFTBAR_BUTTONS) return FALSE;
	for (UINT i=0; i<nBtn; i++)
	{
		if (cLeftBarButtons[i] == ID_SEPARATOR)
		{
			lpRect->left += SMP_LEFTBAR_CXSEP;
		} else
		{
			lpRect->left += SMP_LEFTBAR_CXBTN + SMP_LEFTBAR_CXSPC;
		}
	}
	if (cLeftBarButtons[nBtn] == ID_SEPARATOR)
	{
		lpRect->left += SMP_LEFTBAR_CXSEP/2 - 2;
		lpRect->right = lpRect->left + 2;
		return FALSE;
	} else
	{
		lpRect->right = lpRect->left + SMP_LEFTBAR_CXBTN;
	}
	return TRUE;
}


void CViewSample::DrawNcButton(CDC *pDC, UINT nBtn)
//-------------------------------------------------
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
		case ID_SAMPLE_ZOOMUP:		nImage = SIMAGE_ZOOMUP; break;
		case ID_SAMPLE_ZOOMDOWN:	nImage = SIMAGE_ZOOMDOWN; break;
		case ID_SAMPLE_DRAW:		nImage = SIMAGE_DRAW; break;
		case ID_SAMPLE_ADDSILENCE:	nImage = SIMAGE_RESIZE; break;
		case ID_SAMPLE_GRID:		nImage = SIMAGE_GRID; break;
		}
		pDC->Draw3dRect(rect.left-1, rect.top-1, SMP_LEFTBAR_CXBTN+2, SMP_LEFTBAR_CYBTN+2, c3, c4);
		pDC->Draw3dRect(rect.left, rect.top, SMP_LEFTBAR_CXBTN, SMP_LEFTBAR_CYBTN, c1, c2);
		rect.DeflateRect(1, 1);
		pDC->FillSolidRect(&rect, crFc);
		rect.left += xofs;
		rect.top += yofs;
		if (dwStyle & NCBTNS_CHECKED) m_bmpEnvBar.Draw(pDC, SIMAGE_CHECKED, rect.TopLeft(), ILD_NORMAL);
		m_bmpEnvBar.Draw(pDC, nImage, rect.TopLeft(), ILD_NORMAL);
	} else
	{
		c1 = c2 = crFc;
		if (TrackerSettings::Instance().m_dwPatternSetup & PATTERN_FLATBUTTONS)
		{
			c1 = crDk;
			c2 = crHi;
		}
		pDC->Draw3dRect(rect.left, rect.top, 2, SMP_LEFTBAR_CYBTN, c1, c2);
	}
}


void CViewSample::OnNcPaint()
//---------------------------
{
	RECT rect;

	CModScrollView::OnNcPaint();
	GetWindowRect(&rect);
	// Assumes there is no other non-client items
	rect.bottom = SMP_LEFTBAR_CY;
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
			for (UINT i=0; i<SMP_LEFTBAR_BUTTONS; i++)
			{
				DrawNcButton(pDC, i);
			}
		}
		if (oldpen) SelectObject(hdc, oldpen);
		ReleaseDC(pDC);
	}
}


void CViewSample::UpdateNcButtonState()
//-------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	CSoundFile *pSndFile;
	CDC *pDC = NULL;

	if (!pModDoc) return;
	pSndFile = pModDoc->GetSoundFile();
	for (UINT i=0; i<SMP_LEFTBAR_BUTTONS; i++) if (cLeftBarButtons[i] != ID_SEPARATOR)
	{
		DWORD dwStyle = 0;

		if (m_nBtnMouseOver == i)
		{
			dwStyle |= NCBTNS_MOUSEOVER;
			if(m_dwStatus[SMPSTATUS_NCLBTNDOWN]) dwStyle |= NCBTNS_PUSHED;
		}

		switch(cLeftBarButtons[i])
		{
			case ID_SAMPLE_DRAW:
				if(m_dwStatus[SMPSTATUS_DRAWING]) dwStyle |= NCBTNS_CHECKED;
				if(m_nSample > pSndFile->GetNumSamples())
				{
					dwStyle |= NCBTNS_DISABLED;
				}
				break;
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


///////////////////////////////////////////////////////////////
// CViewSample messages

void CViewSample::OnSize(UINT nType, int cx, int cy)
//--------------------------------------------------
{
	CModScrollView::OnSize(nType, cx, cy);

	DeleteObject(offScreenBitmap);
	DeleteDC(offScreenDC);
	offScreenBitmap = nullptr;
	offScreenDC = nullptr;

	if (((nType == SIZE_RESTORED) || (nType == SIZE_MAXIMIZED)) && (cx > 0) && (cy > 0))
	{
		UpdateScrollSize();
	}
}


void CViewSample::OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp)
//-----------------------------------------------------------------------------
{
	CModScrollView::OnNcCalcSize(bCalcValidRects, lpncsp);
	if (lpncsp)
	{
		lpncsp->rgrc[0].top += SMP_LEFTBAR_CY;
		if (lpncsp->rgrc[0].bottom < lpncsp->rgrc[0].top) lpncsp->rgrc[0].top = lpncsp->rgrc[0].bottom;
	}
}


void CViewSample::ScrollToPosition(int x)    // logical coordinates
//---------------------------------------
{
	CPoint pt;
	// now in device coordinates - limit if out of range
	int xMax = GetScrollLimit(SB_HORZ);
	pt.x = x;
	pt.y = 0;
	if (pt.x < 0)
		pt.x = 0;
	else if (pt.x > xMax)
		pt.x = xMax;
	ScrollToDevicePosition(pt);
}


template<class T, class uT>
T CViewSample::GetSampleValueFromPoint(const ModSample &smp, const CPoint &point) const
//-------------------------------------------------------------------------------------
{
	STATIC_ASSERT(sizeof(T) == sizeof(uT) && sizeof(T) <= 2);
	const int channelHeight = m_rcClient.Height() / smp.GetNumChannels();
	int yPos = point.y - m_drawChannel * channelHeight - m_rcClient.top;

	int value = std::numeric_limits<T>::max() - std::numeric_limits<uT>::max() * yPos / channelHeight;
	Limit(value, std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
	return static_cast<T>(value);
}


template<class T, class uT>
void CViewSample::SetInitialDrawPoint(ModSample &smp, const CPoint &point)
//------------------------------------------------------------------------
{
	m_drawChannel = (point.y - m_rcClient.top) * smp.GetNumChannels() / m_rcClient.Height();
	Limit(m_drawChannel, 0, (int)smp.GetNumChannels() - 1);

	T *data = static_cast<T *>(smp.pSample) + m_drawChannel;
	data[m_dwEndDrag * smp.GetNumChannels()] = GetSampleValueFromPoint<T, uT>(smp, point);
}


template<class T, class uT>
void CViewSample::SetSampleData(ModSample &smp, const CPoint &point, const SmpLength old)
//---------------------------------------------------------------------------------------
{
	T *data = static_cast<T *>(smp.pSample) + m_drawChannel + old * smp.GetNumChannels();
	const int oldvalue = *data;
	const int value = GetSampleValueFromPoint<T, uT>(smp, point);
	const int inc = (m_dwEndDrag > old ? 1 : -1);
	const int ptrInc = inc * smp.GetNumChannels();

	for(SmpLength i = old; i != m_dwEndDrag; i += inc, data += ptrInc)
	{
		*data = static_cast<T>((float)oldvalue + (value - oldvalue) * ((float)i - old) / ((float)m_dwEndDrag - old));
	}
	*data = static_cast<T>(value);
}


void CViewSample::OnMouseMove(UINT, CPoint point)
//-----------------------------------------------
{
	CHAR s[64];
	CModDoc *pModDoc = GetDocument();

	if(m_nBtnMouseOver < SMP_LEFTBAR_BUTTONS || m_dwStatus[SMPSTATUS_NCLBTNDOWN])
	{
		m_dwStatus.reset(SMPSTATUS_NCLBTNDOWN);
		m_nBtnMouseOver = 0xFFFF;
		UpdateNcButtonState();
		CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
		if (pMainFrm) pMainFrm->SetHelpText("");
	}
	if (!pModDoc) return;
	CSoundFile &sndFile = pModDoc->GetrSoundFile();
	if (m_rcClient.PtInRect(point))
	{
		const SmpLength x = ScreenToSample(point.x);
		wsprintf(s, "Cursor: %u", x);
		UpdateIndicator(s);
		CMainFrame *pMainFrm = CMainFrame::GetMainFrame();

		if (pMainFrm && m_dwEndSel <= m_dwBeginSel)
		{
			// Show cursor position as offset effect if no selection is made.
			if(m_nSample > 0 && m_nSample <= sndFile.GetNumSamples() && x < sndFile.GetSample(m_nSample).nLength)
			{
				const SmpLength xLow = (x / 0x100) % 0x100;
				const SmpLength xHigh = x / 0x10000;

				const char cOffsetChar = sndFile.GetModSpecifications().GetEffectLetter(CMD_OFFSET);
				const bool bHasHighOffset = (sndFile.TypeIsS3M_IT_MPT() || (sndFile.GetType() == MOD_TYPE_XM));
				const char cHighOffsetChar = sndFile.GetModSpecifications().GetEffectLetter(static_cast<ModCommand::COMMAND>(sndFile.TypeIsS3M_IT_MPT() ? CMD_S3MCMDEX : CMD_XFINEPORTAUPDOWN));

				if(xHigh == 0)
					wsprintf(s, "Offset: %c%02X", cOffsetChar, xLow);
				else if(bHasHighOffset && xHigh < 0x10)
					wsprintf(s, "Offset: %c%02X, %cA%X", cOffsetChar, xLow, cHighOffsetChar, xHigh);
				else
					wsprintf(s, "Beyond offset range");
				pMainFrm->SetInfoText(s);
			}
			else
			{
				pMainFrm->SetInfoText("");
			}
		}
	} else UpdateIndicator(NULL);
	if(m_dwStatus[SMPSTATUS_MOUSEDRAG])
	{
		const SmpLength len = sndFile.GetSample(m_nSample).nLength;
		if (!len) return;
		SmpLength old = m_dwEndDrag;
		if (m_nZoom)
		{
			if (point.x < 0)
			{
				CPoint pt;
				pt.x = point.x;
				pt.y = 0;
				if (OnScrollBy(pt))
				{
					UpdateWindow();
				}
				point.x = 0;
			}
			if (point.x > m_rcClient.right)
			{
				CPoint pt;
				pt.x = point.x - m_rcClient.right;
				pt.y = 0;
				if (OnScrollBy(pt))
				{
					UpdateWindow();
				}
				point.x = m_rcClient.right;
			}
		}
		m_dwEndDrag = ScreenToSample(point.x);
		if(m_dwStatus[SMPSTATUS_DRAWING])
		{
			if(m_dwEndDrag < len)
			{
				// Shift = draw horizontal lines
				if(CMainFrame::GetInputHandler()->ShiftPressed())
				{
					if(m_lastDrawPoint.y != -1)
						point.y = m_lastDrawPoint.y;
					m_lastDrawPoint = point;
				} else
				{
					m_lastDrawPoint.SetPoint(-1, -1);
				}

				if(sndFile.GetSample(m_nSample).GetElementarySampleSize() == 2)
					SetSampleData<int16, uint16>(sndFile.GetSample(m_nSample), point, old);
				else if(sndFile.GetSample(m_nSample).GetElementarySampleSize() == 1)
					SetSampleData<int8, uint8>(sndFile.GetSample(m_nSample), point, old);

				sndFile.GetSample(m_nSample).PrecomputeLoops(sndFile, false);

				InvalidateSample();
				SetModified(HINT_SAMPLEDATA, false);
			}
		}
		else if (old != m_dwEndDrag)
		{
			SetCurSel(m_dwBeginDrag, m_dwEndDrag);
			UpdateWindow();
		}
	}
}


void CViewSample::OnLButtonDown(UINT, CPoint point)
//-------------------------------------------------
{
	CModDoc *pModDoc = GetDocument();

	if(m_dwStatus[SMPSTATUS_MOUSEDRAG] || (!pModDoc)) return;
	CSoundFile &sndFile = pModDoc->GetrSoundFile();
	ModSample &sample = sndFile.GetSample(m_nSample);

	if (!sample.nLength)
		return;

	m_dwStatus.set(SMPSTATUS_MOUSEDRAG);
	SetFocus();
	SetCapture();
	bool oldsel = (m_dwBeginSel != m_dwEndSel);

	// shift + click = update selection
	if(!m_dwStatus[SMPSTATUS_DRAWING] && CMainFrame::GetInputHandler()->ShiftPressed())
	{
		oldsel = true;
		m_dwEndDrag = ScreenToSample(point.x);
		SetCurSel(m_dwBeginDrag, m_dwEndDrag);
	} else
	{
		m_dwBeginDrag = ScreenToSample(point.x);
		if (m_dwBeginDrag >= sample.nLength) m_dwBeginDrag = sample.nLength - 1;
		m_dwEndDrag = m_dwBeginDrag;
	}
	if (oldsel) SetCurSel(m_dwBeginDrag, m_dwEndDrag);
	// set initial point for sample drawing
	if (m_dwStatus[SMPSTATUS_DRAWING])
	{
		m_lastDrawPoint = point;
		pModDoc->GetSampleUndo().PrepareUndo(m_nSample, sundo_replace, "Draw Sample");
		if(sample.GetElementarySampleSize() == 2)
			SetInitialDrawPoint<int16, uint16>(sample, point);
		else if(sample.GetElementarySampleSize() == 1)
			SetInitialDrawPoint<int8, uint8>(sample, point);

		sndFile.GetSample(m_nSample).PrecomputeLoops(sndFile, false);

		InvalidateSample();
		SetModified(HINT_SAMPLEDATA, false);
	} else
	{
		// ctrl + click = play from cursor pos
		if(CMainFrame::GetInputHandler()->CtrlPressed())
			PlayNote(NOTE_MIDDLEC, ScreenToSample(point.x));
	}
}


void CViewSample::OnLButtonUp(UINT, CPoint)
//-----------------------------------------
{
	if(m_dwStatus[SMPSTATUS_MOUSEDRAG])
	{
		m_dwStatus.reset(SMPSTATUS_MOUSEDRAG);
		ReleaseCapture();
	}
	m_lastDrawPoint.SetPoint(-1, -1);
}


void CViewSample::OnLButtonDblClk(UINT, CPoint)
//---------------------------------------------
{
	CModDoc *pModDoc = GetDocument();

	if (pModDoc)
	{
		CSoundFile *pSndFile = pModDoc->GetSoundFile();
		SmpLength len = pSndFile->GetSample(m_nSample).nLength;
		if (len && !m_dwStatus[SMPSTATUS_DRAWING]) SetCurSel(0, len);
	}
}


void CViewSample::OnRButtonDown(UINT, CPoint pt)
//----------------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if (pModDoc)
	{
		CSoundFile *pSndFile = pModDoc->GetSoundFile();
		const ModSample &sample = pSndFile->GetSample(m_nSample);
		HMENU hMenu = ::CreatePopupMenu();
		CInputHandler* ih = (CMainFrame::GetMainFrame())->GetInputHandler();
		if (!hMenu)	return;
		if (sample.nLength)
		{
			if (m_dwEndSel >= m_dwBeginSel + 4)
			{
				::AppendMenu(hMenu, MF_STRING | (CanZoomSelection() ? 0 : MF_GRAYED), ID_SAMPLE_ZOOMONSEL, _T("Zoom\t") + ih->GetKeyTextFromCommand(kcSampleZoomSelection));
				::AppendMenu(hMenu, MF_STRING, ID_SAMPLE_SETLOOP, _T("Set As Loop"));
				if (pSndFile->GetType() & (MOD_TYPE_IT|MOD_TYPE_MPT))
					::AppendMenu(hMenu, MF_STRING, ID_SAMPLE_SETSUSTAINLOOP, _T("Set As Sustain Loop"));
				::AppendMenu(hMenu, MF_SEPARATOR, 0, "");
			} else
			{
				CHAR s[256];
				SmpLength dwPos = ScreenToSample(pt.x);
				if (dwPos <= sample.nLength)
				{
					//Set loop points
					wsprintf(s, _T("Set &Loop Start to:\t%u"), dwPos);
					::AppendMenu(hMenu, MF_STRING | (dwPos + 4 <= sample.nLoopEnd ? 0 : MF_GRAYED),
						ID_SAMPLE_SETLOOPSTART, s);
					wsprintf(s, _T("Set &Loop End to:\t%u"), dwPos);
					::AppendMenu(hMenu, MF_STRING | (dwPos >= sample.nLoopStart + 4 ? 0 : MF_GRAYED),
						ID_SAMPLE_SETLOOPEND, s);

					if (pSndFile->GetType() & (MOD_TYPE_IT|MOD_TYPE_MPT))
					{
						//Set sustain loop points
						::AppendMenu(hMenu, MF_SEPARATOR, 0, "");
						wsprintf(s, _T("Set &Sustain Start to:\t%u"), dwPos);
						::AppendMenu(hMenu, MF_STRING | (dwPos + 4 <= sample.nSustainEnd ? 0 : MF_GRAYED),
							ID_SAMPLE_SETSUSTAINSTART, s);
						wsprintf(s, _T("Set &Sustain End to:\t%u"), dwPos);
						::AppendMenu(hMenu, MF_STRING | (dwPos >= sample.nSustainStart + 4 ? 0 : MF_GRAYED),
							ID_SAMPLE_SETSUSTAINEND, s);
					}
					::AppendMenu(hMenu, MF_SEPARATOR, 0, _T(""));
					m_dwMenuParam = dwPos;
				}
			}

			if(sample.GetElementarySampleSize() > 1) ::AppendMenu(hMenu, MF_STRING, ID_SAMPLE_8BITCONVERT, "Convert to &8-bit\t" + ih->GetKeyTextFromCommand(kcSample8Bit));
			else ::AppendMenu(hMenu, MF_STRING, ID_SAMPLE_16BITCONVERT, "Convert to &16-bit\t" + ih->GetKeyTextFromCommand(kcSample8Bit));
			if(sample.GetNumChannels() > 1)
			{
				HMENU hMonoMenu = ::CreatePopupMenu();
				::AppendMenu(hMonoMenu, MF_STRING, ID_SAMPLE_MONOCONVERT, _T("&Mix Channels\t") + ih->GetKeyTextFromCommand(kcSampleMonoMix));
				::AppendMenu(hMonoMenu, MF_STRING, ID_SAMPLE_MONOCONVERT_LEFT, _T("&Left Channel\t") + ih->GetKeyTextFromCommand(kcSampleMonoLeft));
				::AppendMenu(hMonoMenu, MF_STRING, ID_SAMPLE_MONOCONVERT_RIGHT, _T("&Right Channel\t") + ih->GetKeyTextFromCommand(kcSampleMonoRight));
				::AppendMenu(hMonoMenu, MF_STRING, ID_SAMPLE_MONOCONVERT_SPLIT, _T("&Split Sample\t") + ih->GetKeyTextFromCommand(kcSampleMonoSplit));
				::AppendMenu(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hMonoMenu), _T("Convert to &Mono"));
			}

			// "Trim" menu item is responding differently if there's no selection,
			// but a loop present: "trim around loop point"! (jojo in topic 2258)
			std::string sTrimMenuText = "Tr&im";
			bool bIsGrayed = ( (m_dwEndSel<=m_dwBeginSel) || (m_dwEndSel - m_dwBeginSel < MIN_TRIM_LENGTH)
								|| (m_dwEndSel - m_dwBeginSel == sample.nLength)
							  );

			if ((m_dwBeginSel == m_dwEndSel) && (sample.nLoopStart < sample.nLoopEnd))
			{
				// no selection => use loop points
				sTrimMenuText += " around loop points";
				// Check whether trim menu item can be enabled (loop not too short or long for trimming).
				if( (sample.nLoopEnd <= sample.nLength) &&
					(sample.nLoopEnd - sample.nLoopStart >= MIN_TRIM_LENGTH) &&
					(sample.nLoopEnd - sample.nLoopStart < sample.nLength) )
					bIsGrayed = false;
			}

			sTrimMenuText += "\t" + ih->GetKeyTextFromCommand(kcSampleTrim);

			::AppendMenu(hMenu, MF_STRING|(bIsGrayed) ? MF_GRAYED : 0, ID_SAMPLE_TRIM, sTrimMenuText.c_str());
			if((m_dwBeginSel == 0 && m_dwEndSel != 0) || (m_dwBeginSel < sample.nLength && m_dwEndSel == sample.nLength))
			{
				::AppendMenu(hMenu, MF_STRING, ID_SAMPLE_QUICKFADE, _T("Quick &Fade\t") + ih->GetKeyTextFromCommand(kcSampleQuickFade));
			}
			::AppendMenu(hMenu, MF_STRING, ID_EDIT_CUT, _T("Cu&t\t") + ih->GetKeyTextFromCommand(kcEditCut));
			::AppendMenu(hMenu, MF_STRING, ID_EDIT_COPY, _T("&Copy\t") + ih->GetKeyTextFromCommand(kcEditCopy));
		}
		::AppendMenu(hMenu, MF_STRING | (IsClipboardFormatAvailable(CF_WAVE) ? 0 : MF_GRAYED), ID_EDIT_PASTE, "&Paste\t" + ih->GetKeyTextFromCommand(kcEditPaste));
		::AppendMenu(hMenu, MF_STRING | (pModDoc->GetSampleUndo().CanUndo(m_nSample) ? 0 : MF_GRAYED), ID_EDIT_UNDO, "&Undo " + CString(pModDoc->GetSampleUndo().GetUndoName(m_nSample)) + "\t" + ih->GetKeyTextFromCommand(kcEditUndo));
		::AppendMenu(hMenu, MF_STRING | (pModDoc->GetSampleUndo().CanRedo(m_nSample) ? 0 : MF_GRAYED), ID_EDIT_REDO, "&Redo " + CString(pModDoc->GetSampleUndo().GetRedoName(m_nSample)) + "\t" + ih->GetKeyTextFromCommand(kcEditRedo));
		ClientToScreen(&pt);
		::TrackPopupMenu(hMenu, TPM_LEFTALIGN|TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, NULL);
		::DestroyMenu(hMenu);
	}
}


void CViewSample::OnNcMouseMove(UINT nHitTest, CPoint point)
//----------------------------------------------------------
{
	CRect rect, rcWnd;
	UINT nBtnSel = 0xFFFF;

	GetWindowRect(&rcWnd);
	for (UINT i=0; i<SMP_LEFTBAR_BUTTONS; i++)
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
			if ((nBtnSel < SMP_LEFTBAR_BUTTONS) && (cLeftBarButtons[nBtnSel] != ID_SEPARATOR))
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


void CViewSample::OnNcLButtonDown(UINT uFlags, CPoint point)
//----------------------------------------------------------
{
	if (m_nBtnMouseOver < SMP_LEFTBAR_BUTTONS)
	{
		m_dwStatus.set(SMPSTATUS_NCLBTNDOWN);
		if (cLeftBarButtons[m_nBtnMouseOver] != ID_SEPARATOR)
		{
			PostMessage(WM_COMMAND, cLeftBarButtons[m_nBtnMouseOver]);
			UpdateNcButtonState();
		}
	}
	CModScrollView::OnNcLButtonDown(uFlags, point);
}


void CViewSample::OnNcLButtonUp(UINT uFlags, CPoint point)
//--------------------------------------------------------
{
	if(m_dwStatus[SMPSTATUS_NCLBTNDOWN])
	{
		m_dwStatus.reset(SMPSTATUS_NCLBTNDOWN);
		UpdateNcButtonState();
	}
	CModScrollView::OnNcLButtonUp(uFlags, point);
}


void CViewSample::OnNcLButtonDblClk(UINT uFlags, CPoint point)
//------------------------------------------------------------
{
	OnNcLButtonDown(uFlags, point);
}


#if _MFC_VER > 0x0710
LRESULT CViewSample::OnNcHitTest(CPoint point)
#else
UINT CViewSample::OnNcHitTest(CPoint point)
#endif
//-----------------------------------------
{
	CRect rect;
	GetWindowRect(&rect);
	rect.bottom = rect.top + SMP_LEFTBAR_CY;
	if (rect.PtInRect(point))
	{
		return HTBORDER;
	}
	return CModScrollView::OnNcHitTest(point);
}


void CViewSample::OnPrevInstrument()
//----------------------------------
{
	SendCtrlMessage(CTRLMSG_SMP_PREVINSTRUMENT);
}


void CViewSample::OnNextInstrument()
//----------------------------------
{
	SendCtrlMessage(CTRLMSG_SMP_NEXTINSTRUMENT);
}


void CViewSample::OnSetLoop()
//---------------------------
{
	CModDoc *pModDoc = GetDocument();
	if (pModDoc)
	{
		CSoundFile &sndFile = pModDoc->GetrSoundFile();
		ModSample &sample = sndFile.GetSample(m_nSample);
		if ((m_dwEndSel > m_dwBeginSel + 15) && (m_dwEndSel <= sample.nLength))
		{
			if ((sample.nLoopStart != m_dwBeginSel) || (sample.nLoopEnd != m_dwEndSel))
			{
				sample.SetLoop(m_dwBeginSel, m_dwEndSel, true, sample.uFlags[CHN_PINGPONGLOOP], sndFile);
				SetModified(HINT_SAMPLEINFO | HINT_SAMPLEDATA, true);
			}
		}
	}
}


void CViewSample::OnSetSustainLoop()
//----------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if (pModDoc)
	{
		CSoundFile &sndFile = pModDoc->GetrSoundFile();
		ModSample &sample = sndFile.GetSample(m_nSample);
		if ((m_dwEndSel > m_dwBeginSel + 15) && (m_dwEndSel <= sample.nLength))
		{
			if ((sample.nSustainStart != m_dwBeginSel) || (sample.nSustainEnd != m_dwEndSel))
			{
				sample.SetSustainLoop(m_dwBeginSel, m_dwEndSel, true, sample.uFlags[CHN_PINGPONGSUSTAIN], sndFile);
				SetModified(HINT_SAMPLEINFO | HINT_SAMPLEDATA, true);
			}
		}
	}
}


void CViewSample::OnEditSelectAll()
//---------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if (pModDoc)
	{
		SmpLength len = pModDoc->GetrSoundFile().GetSample(m_nSample).nLength;
		if (len) SetCurSel(0, len);
	}
}


// Update loop points after deleting a sample selection
void CViewSample::AdjustLoopPoints(SmpLength &loopStart, SmpLength &loopEnd, SmpLength length) const
//--------------------------------------------------------------------------------------------------
{
	Util::DeleteRange(m_dwBeginSel, m_dwEndSel - 1, loopStart, loopEnd);
	LimitMax(loopEnd, length);
	if(loopStart + 4 >= loopEnd)
	{
		loopStart = loopEnd = 0;
	}
}


void CViewSample::OnEditDelete()
//------------------------------
{
	CModDoc *pModDoc = GetDocument();
	DWORD dwUpdateFlags = HINT_SAMPLEINFO | HINT_SAMPLEDATA;

	if (!pModDoc) return;
	CSoundFile &sndFile = pModDoc->GetrSoundFile();
	ModSample &sample = sndFile.GetSample(m_nSample);
	if ((!sample.pSample) || (!sample.nLength)) return;
	if (m_dwEndSel > sample.nLength) m_dwEndSel = sample.nLength;
	if ((m_dwBeginSel >= m_dwEndSel)
	 || (m_dwEndSel - m_dwBeginSel + 4 >= sample.nLength))
	{
		if (Reporting::Confirm("Remove this sample?", "Remove Sample", true) != cnfYes) return;
		pModDoc->GetSampleUndo().PrepareUndo(m_nSample, sundo_replace, "Delete Sample");

		sndFile.DestroySampleThreadsafe(m_nSample);

		dwUpdateFlags |= HINT_SMPNAMES;
	} else
	{
		pModDoc->GetSampleUndo().PrepareUndo(m_nSample, sundo_delete, "Delete Selection", m_dwBeginSel, m_dwEndSel);

		CriticalSection cs;

		const SmpLength selStart = m_dwBeginSel * sample.GetBytesPerSample();
		const SmpLength selEnd = m_dwEndSel * sample.GetBytesPerSample();
		const SmpLength smpEnd = sample.nLength * sample.GetBytesPerSample();
		sample.nLength -= (m_dwEndSel - m_dwBeginSel);

		int8 *p = static_cast<int8 *>(sample.pSample);
		memmove(p + selStart, p + selEnd, smpEnd - selEnd);

		// adjust loop points
		AdjustLoopPoints(sample.nLoopStart, sample.nLoopEnd, sample.nLength);
		AdjustLoopPoints(sample.nSustainStart, sample.nSustainEnd, sample.nLength);

		if(sample.nLoopEnd == 0)
		{
			sample.uFlags.reset(CHN_LOOP | CHN_PINGPONGLOOP);
		}

		if(sample.nSustainEnd == 0)
		{
			sample.uFlags.reset(CHN_SUSTAINLOOP | CHN_PINGPONGSUSTAIN);
		}

		sample.PrecomputeLoops(sndFile);
	}
	SetCurSel(0, 0);
	SetModified(dwUpdateFlags, true);
}


void CViewSample::OnEditCut()
//---------------------------
{
	OnEditCopy();
	OnEditDelete();
}


void CViewSample::OnEditCopy()
//----------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	if(pMainFrm == nullptr || GetDocument() == nullptr)
	{
		return;
	}

	const CSoundFile &sndFile = GetDocument()->GetrSoundFile();
	const ModSample &sample = sndFile.GetSample(m_nSample);

	bool addLoopInfo = true;
	size_t smpSize = sample.nLength;
	size_t smpOffset = 0;

	// First things first: Calculate sample size, taking partial selections into account.
	LimitMax(m_dwEndSel, sample.nLength);
	if(m_dwEndSel > m_dwBeginSel)
	{
		smpSize = m_dwEndSel - m_dwBeginSel;
		smpOffset = m_dwBeginSel;
		addLoopInfo = false;
	}

	smpSize *= sample.GetBytesPerSample();
	smpOffset *= sample.GetBytesPerSample();

	// Ok, now calculate size of the resulting WAV file.
	size_t memSize = sizeof(RIFFHeader)									// RIFF Header
		+ sizeof(RIFFChunk) + sizeof(WAVFormatChunk)					// Sample format
		+ sizeof(RIFFChunk) + ((smpSize + 1) & ~1)						// Sample data
		+ sizeof(RIFFChunk) + sizeof(WAVExtraChunk)						// Sample metadata
		+ MAX_SAMPLENAME + MAX_SAMPLEFILENAME;							// Sample name
	STATIC_ASSERT((sizeof(WAVExtraChunk) % 2u) == 0);
	STATIC_ASSERT((MAX_SAMPLENAME % 2u) == 0);
	STATIC_ASSERT((MAX_SAMPLEFILENAME % 2u) == 0);

	if(addLoopInfo)
	{
		// We want to store some loop metadata as well.
		memSize += sizeof(RIFFChunk) + sizeof(WAVSampleInfoChunk) + 2 * sizeof(WAVSampleLoop);
	}

	ASSERT((memSize % 2u) == 0);

	BeginWaitCursor();
	HGLOBAL hCpy;
	if(pMainFrm->OpenClipboard() && (hCpy = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, memSize)) != nullptr)
	{
		EmptyClipboard();

		void *p = GlobalLock(hCpy);
		WAVWriter file(p, memSize);

		// Write sample format
		file.WriteFormat(sample.GetSampleRate(sndFile.GetType()), sample.GetElementarySampleSize() * 8, sample.GetNumChannels(), WAVFormatChunk::fmtPCM);

		// Write sample data
		file.StartChunk(RIFFChunk::iddata);
		
		uint8 *sampleData = static_cast<uint8 *>(p) + file.GetPosition();
		memcpy(sampleData, static_cast<const char *>(sample.pSample) + smpOffset, smpSize);
		if(sample.GetElementarySampleSize() == 1)
		{
			// 8-Bit samples have to be unsigned.
			for(size_t i = smpSize; i != 0; i--)
			{
				*(sampleData++) += 0x80u;
			}
		}
		
		file.Skip(smpSize);

		if(addLoopInfo)
		{
			file.WriteLoopInformation(sample);
		}
		file.WriteExtraInformation(sample, sndFile.GetType(), sndFile.GetSampleName(m_nSample));

		file.Finalize();

		GlobalUnlock(hCpy);
		SetClipboardData (CF_WAVE, (HANDLE)hCpy);
		CloseClipboard();
	}
	EndWaitCursor();
}


void CViewSample::OnEditPaste()
//-----------------------------
{
	CModDoc *pModDoc = GetDocument();
	BeginWaitCursor();
	if ((pModDoc) && (OpenClipboard()))
	{
		char s[MAX_SAMPLENAME], s2[MAX_SAMPLEFILENAME];
		HGLOBAL hCpy = ::GetClipboardData(CF_WAVE);
		LPBYTE p;

		if ((hCpy) && ((p = (LPBYTE)GlobalLock(hCpy)) != NULL))
		{
			pModDoc->GetSampleUndo().PrepareUndo(m_nSample, sundo_replace, "Paste");

			CSoundFile &sndFile = pModDoc->GetrSoundFile();
			ModSample &sample = sndFile.GetSample(m_nSample);

			MemCopy(s, sndFile.m_szNames[m_nSample]);
			MemCopy(s2, sample.filename);

			FileReader file(p, GlobalSize(hCpy));
			sndFile.ReadSampleFromFile(m_nSample, file);

			if(!sndFile.m_szNames[m_nSample][0])
			{
				MemCopy(sndFile.m_szNames[m_nSample], s);
			}
			if (!sample.filename[0])
			{
				MemCopy(sample.filename, s2);
			}

			GlobalUnlock(hCpy);
			SetCurSel(0, 0);
			sample.PrecomputeLoops(sndFile, true);
			SetModified(HINT_SAMPLEINFO | HINT_SAMPLEDATA | HINT_SMPNAMES, true);
		}
		CloseClipboard();
	}
	EndWaitCursor();
}


void CViewSample::OnEditUndo()
//----------------------------
{
	CModDoc *pModDoc = GetDocument();
	if(pModDoc == nullptr) return;
	if(pModDoc->GetSampleUndo().Undo(m_nSample))
	{
		SetModified(HINT_SAMPLEINFO | HINT_SAMPLEDATA | HINT_SMPNAMES, true);
	}
}


void CViewSample::OnEditRedo()
//----------------------------
{
	CModDoc *pModDoc = GetDocument();
	if(pModDoc == nullptr) return;
	if(pModDoc->GetSampleUndo().Redo(m_nSample))
	{
		SetModified(HINT_SAMPLEINFO | HINT_SAMPLEDATA | HINT_SMPNAMES, true);
	}
}


void CViewSample::On8BitConvert()
//-------------------------------
{
	CModDoc *pModDoc = GetDocument();
	BeginWaitCursor();
	if ((pModDoc) && (m_nSample <= pModDoc->GetNumSamples()))
	{
		CSoundFile &sndFile = pModDoc->GetrSoundFile();
		ModSample &sample = sndFile.GetSample(m_nSample);
		if(sample.uFlags[CHN_16BIT] && sample.pSample != nullptr && sample.nLength != 0)
		{
			ASSERT(sample.GetElementarySampleSize() == 2);
			pModDoc->GetSampleUndo().PrepareUndo(m_nSample, sundo_replace, "8-Bit Conversion");

			CriticalSection cs;

			int8 *p = (int8 *)(sample.pSample);
			SmpLength len = (sample.nLength + 1) * sample.GetNumChannels();
			for (SmpLength i=0; i<=len; i++)
			{
				p[i] = (int8) ((*((int16 *)(p+i*2))) / 256);
			}
			sample.uFlags.reset(CHN_16BIT);
			for (CHANNELINDEX j = 0; j < MAX_CHANNELS; j++) if (sndFile.m_PlayState.Chn[j].pModSample == &sample)
			{
				sndFile.m_PlayState.Chn[j].dwFlags.reset(CHN_16BIT);
			}

			sample.PrecomputeLoops(sndFile, false);
			cs.Leave();

			SetModified(HINT_SAMPLEDATA | HINT_SAMPLEINFO, true);
		}
	}
	EndWaitCursor();
}


void CViewSample::On16BitConvert()
//--------------------------------
{
	CModDoc *pModDoc = GetDocument();
	BeginWaitCursor();
	if ((pModDoc) && (m_nSample <= pModDoc->GetNumSamples()))
	{
		CSoundFile &sndFile = pModDoc->GetrSoundFile();
		ModSample &sample = sndFile.GetSample(m_nSample);
		if(!sample.uFlags[CHN_16BIT] && sample.pSample != nullptr && sample.nLength != 0)
		{
			ASSERT(sample.GetElementarySampleSize() == 1);
			const SmpLength numSamples = sample.nLength * sample.GetNumChannels();
			int16 *newSample = static_cast<int16 *>(ModSample::AllocateSample(numSamples, 2));
			if(newSample != nullptr)
			{
				const int8 *oldSample = static_cast<const int8 *>(sample.pSample);
				pModDoc->GetSampleUndo().PrepareUndo(m_nSample, sundo_replace, "16-Bit Conversion");
				CopySample<SC::ConversionChain<SC::Convert<int16, int8>, SC::DecodeIdentity<int8> > >(newSample, numSamples, 1, oldSample, sample.GetSampleSizeInBytes(), 1);
				sample.uFlags.set(CHN_16BIT);
				ctrlSmp::ReplaceSample(sample, newSample, sample.nLength, sndFile);
				sample.PrecomputeLoops(sndFile, false);

				SetModified(HINT_SAMPLEDATA | HINT_SAMPLEINFO, true);
			}
		}
	}
	EndWaitCursor();
}


void CViewSample::OnMonoConvert(ctrlSmp::StereoToMonoMode convert)
//----------------------------------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	BeginWaitCursor();
	if(pModDoc != nullptr && (m_nSample <= pModDoc->GetNumSamples()))
	{
		CSoundFile &sndFile = pModDoc->GetrSoundFile();
		ModSample &sample = sndFile.GetSample(m_nSample);
		if(sample.GetNumChannels() > 1 && sample.pSample != nullptr && sample.nLength != 0)
		{
			SAMPLEINDEX rightSmp = SAMPLEINDEX_INVALID;
			if(convert == ctrlSmp::splitSample)
			{
				// Split sample into two slots
				rightSmp = pModDoc->InsertSample(true);
				if(rightSmp != SAMPLEINDEX_INVALID)
				{
					sndFile.ReadSampleFromSong(rightSmp, sndFile, m_nSample);
				} else
				{
					return;
				}
			}

			pModDoc->GetSampleUndo().PrepareUndo(m_nSample, sundo_replace, "Mono Conversion");

			if(ctrlSmp::ConvertToMono(sample, sndFile, convert))
			{
				if(convert == ctrlSmp::splitSample)
				{
					// Split mode: We need to convert the right channel as well!
					ModSample &right = sndFile.GetSample(rightSmp);
					ctrlSmp::ConvertToMono(right, sndFile, ctrlSmp::onlyRight);

					// Try to create a new instrument as well which maps to the right sample.
					INSTRUMENTINDEX ins = pModDoc->FindSampleParent(m_nSample);
					if(ins != INSTRUMENTINDEX_INVALID)
					{
						INSTRUMENTINDEX rightIns = pModDoc->InsertInstrument(0, ins);
						if(rightIns != INSTRUMENTINDEX_INVALID)
						{
							for(size_t i = 0; i < CountOf(sndFile.Instruments[rightIns]->Keyboard); i++)
							{
								if(sndFile.Instruments[rightIns]->Keyboard[i] == m_nSample)
								{
									sndFile.Instruments[rightIns]->Keyboard[i] = rightSmp;
								}
							}
						}
					}

					// Finally, adjust sample panning
					if(sndFile.GetType() & (MOD_TYPE_IT | MOD_TYPE_MPT | MOD_TYPE_XM))
					{
						sample.uFlags.set(CHN_PANNING);
						sample.nPan = 0;
						right.uFlags.set(CHN_PANNING);
						right.nPan = 256;
					}
				}
				SetModified(HINT_SAMPLEDATA | HINT_SAMPLEINFO, true);
			} else
			{
				pModDoc->GetSampleUndo().RemoveLastUndoStep(m_nSample);
			}
		}
	}
	EndWaitCursor();
}


void CViewSample::OnSampleTrim()
//------------------------------
{
	CModDoc *pModDoc = GetDocument();
	//nothing loaded or invalid sample slot.
	if(!pModDoc || m_nSample > pModDoc->GetNumSamples()) return;

	CSoundFile &sndFile = pModDoc->GetrSoundFile();
	ModSample &sample = sndFile.GetSample(m_nSample);

	if(m_dwBeginSel == m_dwEndSel)
	{
		// Trim around loop points if there's no selection
		m_dwBeginSel = sample.nLoopStart;
		m_dwEndSel = sample.nLoopEnd;
	}

	if (m_dwBeginSel >= m_dwEndSel) return; // invalid selection

	BeginWaitCursor();
	SmpLength nStart = m_dwBeginSel;
	SmpLength nEnd = m_dwEndSel - m_dwBeginSel;

	if ((sample.pSample) && (nStart+nEnd <= sample.nLength) && (nEnd >= MIN_TRIM_LENGTH))
	{
		pModDoc->GetSampleUndo().PrepareUndo(m_nSample, sundo_replace, "Trim");

		CriticalSection cs;

		// Note: Sample is overwritten in-place! Unused data is not deallocated!
		int8 *p = static_cast<int8 *>(sample.pSample);
		memmove(p, p + nStart * sample.GetBytesPerSample(), nEnd * sample.GetBytesPerSample());

		if (sample.nLoopStart >= nStart) sample.nLoopStart -= nStart;
		if (sample.nLoopEnd >= nStart) sample.nLoopEnd -= nStart;
		if (sample.nSustainStart >= nStart) sample.nSustainStart -= nStart;
		if (sample.nSustainEnd >= nStart) sample.nSustainEnd -= nStart;
		if (sample.nLoopEnd > nEnd) sample.nLoopEnd = nEnd;
		if (sample.nSustainEnd > nEnd) sample.nSustainEnd = nEnd;
		sample.nLength = nEnd;
		sample.PrecomputeLoops(sndFile);
		cs.Leave();

		SetCurSel(0, 0);
		SetModified(HINT_SAMPLEDATA | HINT_SAMPLEINFO, true);
	}
	EndWaitCursor();
}


void CViewSample::OnChar(UINT /*nChar*/, UINT, UINT /*nFlags*/)
//-------------------------------------------------------------
{
}


void CViewSample::PlayNote(ModCommand::NOTE note, const SmpLength nStartPos, int volume)
//--------------------------------------------------------------------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	CModDoc *pModDoc = GetDocument();
	if ((pModDoc) && (pMainFrm))
	{
		if (note >= NOTE_MIN_SPECIAL)
		{
			pModDoc->NoteOff(0, (note == NOTE_NOTECUT));
		} else
		{
			if(m_dwStatus[SMPSTATUS_KEYDOWN])
				pModDoc->NoteOff(note, true);
			else
				pModDoc->NoteOff(0, true);

			SmpLength loopstart = m_dwBeginSel, loopend = m_dwEndSel;
			// If selection is too small -> no loop
			if(m_nZoom >= 0 && loopend - loopstart < (SmpLength)(4 << m_nZoom))
				loopend = loopstart = 0;
			else if(m_nZoom < 0 && loopend - loopstart < 4)
				loopend = loopstart = 0;

			noteChannel[note - NOTE_MIN] = pModDoc->PlayNote(note, 0, m_nSample, false, volume, loopstart, loopend, CHANNELINDEX_INVALID, nStartPos);

			m_dwStatus.set(SMPSTATUS_KEYDOWN);

			CSoundFile &sndFile = pModDoc->GetrSoundFile();
			ModSample &sample = sndFile.GetSample(m_nSample);
			uint32 freq = sndFile.GetFreqFromPeriod(sndFile.GetPeriodFromNote(note + (sndFile.GetType() == MOD_TYPE_XM ? sample.RelativeTone : 0), sample.nFineTune, sample.nC5Speed), 0);

			const std::string s = mpt::String::Print("%1 (%2.%3 Hz)",
				sndFile.GetNoteName((ModCommand::NOTE)note),
				freq >> FREQ_FRACBITS,
				mpt::fmt::dec0<2>(Util::muldiv(freq & ((1 << FREQ_FRACBITS) - 1), 100, 1 << FREQ_FRACBITS)));
			pMainFrm->SetInfoText(s.c_str());
		}
	}
}


void CViewSample::NoteOff(ModCommand::NOTE note)
//----------------------------------------------
{
	CSoundFile &sndFile = GetDocument()->GetrSoundFile();
	ModChannel &chn = sndFile.m_PlayState.Chn[noteChannel[note - NOTE_MIN]];
	sndFile.KeyOff(&chn);
	chn.dwFlags.set(CHN_NOTEFADE);
	noteChannel[note - NOTE_MIN] = CHANNELINDEX_INVALID;
}


// Drop files from Windows
void CViewSample::OnDropFiles(HDROP hDropInfo)
//--------------------------------------------
{
	const UINT nFiles = ::DragQueryFileW(hDropInfo, (UINT)-1, NULL, 0);
	CMainFrame::GetMainFrame()->SetForegroundWindow();
	for(UINT f = 0; f < nFiles; f++)
	{
		WCHAR fileName[MAX_PATH];
		if(::DragQueryFileW(hDropInfo, f, fileName, CountOf(fileName)))
		{
			const mpt::PathString file = mpt::PathString::FromNative(fileName);
			if(SendCtrlMessage(CTRLMSG_SMP_OPENFILE, (LPARAM)&file) && f < nFiles - 1)
			{
				// Insert more sample slots
				SendCtrlMessage(IDC_SAMPLE_NEW);
			}
		}
	}
	::DragFinish(hDropInfo);
}


BOOL CViewSample::OnDragonDrop(BOOL bDoDrop, const DRAGONDROP *lpDropInfo)
//------------------------------------------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	CSoundFile *pSndFile;
	BOOL bCanDrop = FALSE, bUpdate;

	if ((!lpDropInfo) || (!pModDoc)) return FALSE;
	pSndFile = pModDoc->GetSoundFile();
	switch(lpDropInfo->dwDropType)
	{
	case DRAGONDROP_SAMPLE:
		if (lpDropInfo->pModDoc == pModDoc)
		{
			bCanDrop = ((lpDropInfo->dwDropItem)
					 && (lpDropInfo->dwDropItem <= pSndFile->m_nSamples));
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
	// Do the drop
	BeginWaitCursor();
	bUpdate = FALSE;
	switch(lpDropInfo->dwDropType)
	{
	case DRAGONDROP_SAMPLE:
		if (lpDropInfo->pModDoc == pModDoc)
		{
			SendCtrlMessage(CTRLMSG_SETCURRENTINSTRUMENT, lpDropInfo->dwDropItem);
		} else
		{
			SendCtrlMessage(CTRLMSG_SMP_SONGDROP, (LPARAM)lpDropInfo);
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

					pModDoc->GetSampleUndo().PrepareUndo(m_nSample, sundo_replace, "Replace");
					bCanDrop = dlsbank.ExtractSample(*pSndFile, m_nSample, nIns, nRgn);
				}
				bUpdate = TRUE;
				break;
			}
		}
		MPT_FALLTHROUGH;
	case DRAGONDROP_SOUNDFILE:
		SendCtrlMessage(CTRLMSG_SMP_OPENFILE, lpDropInfo->lDropParam);
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
			// Melodic: (MidiBank << 16) | (Instrument)
			{
				nRgn = pDLSBank->GetRegionFromKey(nIns, 60);
			}
			CriticalSection cs;

			pModDoc->GetSampleUndo().PrepareUndo(m_nSample, sundo_replace, "Replace");
			bCanDrop = pDLSBank->ExtractSample(*pSndFile, m_nSample, nIns, nRgn);

			bUpdate = TRUE;
		}
		break;
	}
	if (bUpdate)
	{
		SetModified(HINT_SAMPLEDATA | HINT_SAMPLEINFO | HINT_SMPNAMES, true);
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


void CViewSample::OnZoomOnSel()
//-----------------------------
{
	int zoom = 0;
	SmpLength selLength = (m_dwEndSel - m_dwBeginSel);
	if (selLength > 0 && m_rcClient.right > 0)
	{
		zoom = GetZoomLevel(selLength);
		if(zoom < 0)
		{
			zoom++;
			if(zoom >= -1)
				zoom = 1;
			else if(zoom < MIN_ZOOM)
				zoom = MIN_ZOOM;
		} else if(zoom > MAX_ZOOM)
		{
			zoom = 0;
		}
	}

	SendCtrlMessage(CTRLMSG_SMP_SETZOOM, zoom);
	if (zoom)
	{
		SetZoom(zoom, m_dwBeginSel + selLength / 2);
	}
}


SmpLength CViewSample::ScrollPosToSamplePos(int nZoom) const
//----------------------------------------------------------
{
	if(nZoom < 0)
		return m_nScrollPosX;
	else if(nZoom > 0)
		return m_nScrollPosX << (nZoom - 1);
	else
		return 0;
}


void CViewSample::OnSetLoopStart()
//--------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if (pModDoc)
	{
		CSoundFile &sndFile = pModDoc->GetrSoundFile();
		ModSample &sample = sndFile.GetSample(m_nSample);
		if ((m_dwMenuParam+4 <= sample.nLoopEnd) && (sample.nLoopStart != m_dwMenuParam))
		{
			pModDoc->GetSampleUndo().PrepareUndo(m_nSample, sundo_none, "Set Loop Start");
			sample.SetLoop(m_dwMenuParam, sample.nLoopEnd, true, sample.uFlags[CHN_PINGPONGLOOP], sndFile);
			SetModified(HINT_SAMPLEDATA | HINT_SAMPLEINFO, true);
		}
	}
}


void CViewSample::OnSetLoopEnd()
//------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if (pModDoc)
	{
		CSoundFile &sndFile = pModDoc->GetrSoundFile();
		ModSample &sample = sndFile.GetSample(m_nSample);
		if ((m_dwMenuParam >= sample.nLoopStart+4) && (sample.nLoopEnd != m_dwMenuParam))
		{
			pModDoc->GetSampleUndo().PrepareUndo(m_nSample, sundo_none, "Set Loop End");
			sample.SetLoop(sample.nLoopStart, m_dwMenuParam, true, sample.uFlags[CHN_PINGPONGLOOP], sndFile);
			SetModified(HINT_SAMPLEDATA | HINT_SAMPLEINFO, true);
		}
	}
}


void CViewSample::OnSetSustainStart()
//-----------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if (pModDoc)
	{
		CSoundFile &sndFile = pModDoc->GetrSoundFile();
		ModSample &sample = sndFile.GetSample(m_nSample);
		if ((m_dwMenuParam+4 <= sample.nSustainEnd) && (sample.nSustainStart != m_dwMenuParam))
		{
			pModDoc->GetSampleUndo().PrepareUndo(m_nSample, sundo_none, "Set Sustain Start");
			sample.SetSustainLoop(m_dwMenuParam, sample.nSustainEnd, true, sample.uFlags[CHN_PINGPONGSUSTAIN], sndFile);
			SetModified(HINT_SAMPLEDATA | HINT_SAMPLEINFO, true);
		}
	}
}


void CViewSample::OnSetSustainEnd()
//---------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if (pModDoc)
	{
		CSoundFile &sndFile = pModDoc->GetrSoundFile();
		ModSample &sample = sndFile.GetSample(m_nSample);
		if ((m_dwMenuParam >= sample.nSustainStart+4) && (sample.nSustainEnd != m_dwMenuParam))
		{
			pModDoc->GetSampleUndo().PrepareUndo(m_nSample, sundo_none, "Set Sustain End");
			sample.SetSustainLoop(sample.nSustainStart, m_dwMenuParam, true, sample.uFlags[CHN_PINGPONGSUSTAIN], sndFile);
			SetModified(HINT_SAMPLEDATA | HINT_SAMPLEINFO, true);
		}
	}
}


void CViewSample::OnZoomUp()
//--------------------------
{
	DoZoom(1);
}


void CViewSample::OnZoomDown()
//----------------------------
{
	DoZoom(-1);
}


void CViewSample::OnDrawingToggle()
//---------------------------------
{
	const CModDoc *pModDoc = GetDocument();
	if(!pModDoc) return;
	const CSoundFile &sndFile = pModDoc->GetrSoundFile();

	const ModSample &sample = sndFile.GetSample(m_nSample);
	if(sample.pSample == nullptr)
	{
		OnAddSilence();
		if(sample.pSample == nullptr)
		{
			return;
		}
	}

	m_dwStatus.flip(SMPSTATUS_DRAWING);
	UpdateNcButtonState();
}


void CViewSample::OnAddSilence()
//------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if (!pModDoc) return;
	CSoundFile &sndFile = pModDoc->GetrSoundFile();

	ModSample &sample = sndFile.GetSample(m_nSample);

	CAddSilenceDlg dlg(this, 32, sample.nLength);
	if (dlg.DoModal() != IDOK) return;

	const SmpLength nOldLength = sample.nLength;

	if(MAX_SAMPLE_LENGTH - nOldLength < dlg.m_nSamples && dlg.m_nEditOption != addsilence_resize)
	{
		CString str; str.Format(TEXT("Can't add silence because the new sample length would exceed maximum sample length %u."), MAX_SAMPLE_LENGTH);
		Reporting::Information(str);
		return;
	}

	BeginWaitCursor();

	if(dlg.m_nEditOption == addsilence_resize)
	{
		// resize - dlg.m_nSamples = new size
		if(dlg.m_nSamples != sample.nLength)
		{
			CriticalSection cs;

			if(dlg.m_nSamples < sample.nLength)	// make it shorter!
				pModDoc->GetSampleUndo().PrepareUndo(m_nSample, sundo_delete, "Resize", dlg.m_nSamples, sample.nLength);
			else	// make it longer!
				pModDoc->GetSampleUndo().PrepareUndo(m_nSample, sundo_insert, "Add Silence", sample.nLength, dlg.m_nSamples);
			ctrlSmp::ResizeSample(sample, dlg.m_nSamples, sndFile);
		}
	} else
	{
		// add silence - dlg.m_nSamples = amount of bytes to be added
		if(dlg.m_nSamples > 0)
		{
			CriticalSection cs;

			SmpLength nStart = (dlg.m_nEditOption == addsilence_at_end) ? sample.nLength : 0;
			pModDoc->GetSampleUndo().PrepareUndo(m_nSample, sundo_insert, "Add Silence", nStart, nStart + dlg.m_nSamples);
			ctrlSmp::InsertSilence(sample, dlg.m_nSamples, nStart, sndFile);
		}
	}

	EndWaitCursor();

	if(nOldLength != sample.nLength)
	{
		SetCurSel(0, 0);
		SetModified(HINT_SAMPLEDATA | HINT_SAMPLEINFO | HINT_SMPNAMES, true);
	}
}

LRESULT CViewSample::OnMidiMsg(WPARAM dwMidiDataParam, LPARAM)
//------------------------------------------------------------
{
	const DWORD dwMidiData = dwMidiDataParam;
	static BYTE midivolume = 127;

	CModDoc *pModDoc = GetDocument();
	BYTE midibyte1 = MIDIEvents::GetDataByte1FromEvent(dwMidiData);
	BYTE midibyte2 = MIDIEvents::GetDataByte2FromEvent(dwMidiData);

	CSoundFile* pSndFile = (pModDoc) ? pModDoc->GetSoundFile() : NULL;
	if (!pSndFile) return 0;

	uint8 nNote = midibyte1 + NOTE_MIN;
	int nVol = midibyte2;
	MIDIEvents::EventType event  = MIDIEvents::GetTypeFromEvent(dwMidiData);
	if(event == MIDIEvents::evNoteOn && !nVol)
	{
		event = MIDIEvents::evNoteOff;	//Convert event to note-off if req'd
	}

	// Handle MIDI messages assigned to shortcuts
	CInputHandler *ih = CMainFrame::GetMainFrame()->GetInputHandler();
	if(ih->HandleMIDIMessage(kCtxViewSamples, dwMidiData) != kcNull
		|| ih->HandleMIDIMessage(kCtxAllContexts, dwMidiData) != kcNull)
	{
		// Mapped to a command, no need to pass message on.
		return 1;
	}

	switch(event)
	{
	case MIDIEvents::evNoteOff: // Note Off
		midibyte2 = 0;

	case MIDIEvents::evNoteOn: // Note On
		LimitMax(nNote, NOTE_MAX);
		pModDoc->NoteOff(nNote, true);
		if(midibyte2 & 0x7F)
		{
			nVol = CMainFrame::ApplyVolumeRelatedSettings(dwMidiData, midivolume);
			PlayNote(nNote, 0, nVol);
		}
		break;

	case MIDIEvents::evControllerChange: //Controller change
		switch(midibyte1)
		{
		case MIDIEvents::MIDICC_Volume_Coarse: //Volume
			midivolume = midibyte2;
			break;
		}
		break;
	}

	return 1;
}

BOOL CViewSample::PreTranslateMessage(MSG *pMsg)
//-----------------------------------------------
{
	if (pMsg)
	{
		//We handle keypresses before Windows has a chance to handle them (for alt etc..)
		if ((pMsg->message == WM_SYSKEYUP)   || (pMsg->message == WM_KEYUP) ||
			(pMsg->message == WM_SYSKEYDOWN) || (pMsg->message == WM_KEYDOWN))
		{
			CInputHandler* ih = (CMainFrame::GetMainFrame())->GetInputHandler();

			//Translate message manually
			UINT nChar = pMsg->wParam;
			UINT nRepCnt = LOWORD(pMsg->lParam);
			UINT nFlags = HIWORD(pMsg->lParam);
			KeyEventType kT = ih->GetKeyEventType(nFlags);
			InputTargetContext ctx = (InputTargetContext)(kCtxViewSamples);

			if (ih->KeyEvent(ctx, nChar, nRepCnt, nFlags, kT) != kcNull)
				return true; // Mapped to a command, no need to pass message on.
		}

	}

	return CModScrollView::PreTranslateMessage(pMsg);
}

LRESULT CViewSample::OnCustomKeyMsg(WPARAM wParam, LPARAM lParam)
//---------------------------------------------------------------
{
	if (wParam == kcNull)
		return NULL;

	CModDoc *pModDoc = GetDocument();
	if (!pModDoc) return NULL;
	CSoundFile &sndFile = pModDoc->GetrSoundFile();

	switch(wParam)
	{
		case kcSampleTrim:		OnSampleTrim() ; return wParam;
		case kcSampleZoomUp:	OnZoomUp(); return wParam;
		case kcSampleZoomDown:	OnZoomDown(); return wParam;
		case kcSampleZoomSelection: OnZoomOnSel(); return wParam;
		case kcPrevInstrument:	OnPrevInstrument(); return wParam;
		case kcNextInstrument:	OnNextInstrument(); return wParam;
		case kcEditSelectAll:	OnEditSelectAll(); return wParam;
		case kcSampleDelete:	OnEditDelete(); return wParam;
		case kcEditCut:			OnEditCut(); return wParam;
		case kcEditCopy:		OnEditCopy(); return wParam;
		case kcEditPaste:		OnEditPaste(); return wParam;
		case kcEditUndo:		OnEditUndo(); return wParam;
		case kcEditRedo:		OnEditRedo(); return wParam;
		case kcSample8Bit:		if(sndFile.GetSample(m_nSample).uFlags[CHN_16BIT])
									On8BitConvert();
								else
									On16BitConvert();
								return wParam;
		case kcSampleMonoMix:	OnMonoConvertMix(); return wParam;
		case kcSampleMonoLeft:	OnMonoConvertLeft(); return wParam;
		case kcSampleMonoRight:	OnMonoConvertRight(); return wParam;
		case kcSampleMonoSplit:	OnMonoConvertSplit(); return wParam;

		case kcSampleReverse:			PostCtrlMessage(IDC_SAMPLE_REVERSE); return wParam;
		case kcSampleSilence:			PostCtrlMessage(IDC_SAMPLE_SILENCE); return wParam;
		case kcSampleNormalize:			PostCtrlMessage(IDC_SAMPLE_NORMALIZE); return wParam;
		case kcSampleAmplify:			PostCtrlMessage(IDC_SAMPLE_AMPLIFY); return wParam;
		case kcSampleInvert:			PostCtrlMessage(IDC_SAMPLE_INVERT); return wParam;
		case kcSampleSignUnsign:		PostCtrlMessage(IDC_SAMPLE_SIGN_UNSIGN); return wParam;
		case kcSampleRemoveDCOffset:	PostCtrlMessage(IDC_SAMPLE_DCOFFSET); return wParam;
		case kcSampleXFade:				PostCtrlMessage(IDC_SAMPLE_XFADE); return wParam;
		case kcSampleAutotune:			PostCtrlMessage(IDC_SAMPLE_AUTOTUNE); return wParam;
		case kcSampleQuickFade:			PostCtrlMessage(IDC_SAMPLE_QUICKFADE); return wParam;

		// Those don't seem to work.
		case kcNoteOff:			PlayNote(NOTE_KEYOFF); return wParam;
		case kcNoteCut:			PlayNote(NOTE_NOTECUT); return wParam;
	}

	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	if(wParam >= kcSampStartNotes && wParam <= kcSampEndNotes)
	{
		const ModCommand::NOTE note = static_cast<ModCommand::NOTE>(wParam - kcSampStartNotes + NOTE_MIN + pMainFrm->GetBaseOctave() * 12);
		if(ModCommand::IsNote(note))
		{
			switch(TrackerSettings::Instance().sampleEditorKeyBehaviour)
			{
			case seNoteOffOnKeyRestrike:
				if(noteChannel[note - NOTE_MIN] != CHANNELINDEX_INVALID)
				{
					NoteOff(note);
					break;
				}
				// Fall-through
			default:
				PlayNote(note);
			}
			return wParam;
		}
	} else if(wParam >= kcSampStartNoteStops && wParam <= kcSampEndNoteStops)
	{
		const ModCommand::NOTE note = static_cast<ModCommand::NOTE>(wParam - kcSampStartNoteStops + NOTE_MIN + pMainFrm->GetBaseOctave() * 12);
		if(ModCommand::IsNote(note))
		{
			switch(TrackerSettings::Instance().sampleEditorKeyBehaviour)
			{
			case seNoteOffOnNewKey:
				m_dwStatus.reset(SMPSTATUS_KEYDOWN);
				if(noteChannel[note - NOTE_MIN] != CHANNELINDEX_INVALID)
				{
					// Release sustain loop on key up
					pModDoc->GetrSoundFile().KeyOff(&GetDocument()->GetrSoundFile().m_PlayState.Chn[noteChannel[note - NOTE_MIN]]);
				}
				break;
			case seNoteOffOnKeyUp:
				if(noteChannel[note - NOTE_MIN] != CHANNELINDEX_INVALID)
				{
					NoteOff(note);
				}
				break;
			}
			return wParam;
		}
	}

	// Pass on to ctrl_smp
	return GetControlDlg()->SendMessage(WM_MOD_KEYCOMMAND, wParam, lParam);
}


bool CViewSample::CanZoomSelection() const
//----------------------------------------
{
	return GetZoomLevel(m_dwEndSel - m_dwBeginSel) <= MAX_ZOOM;
}


// Returns auto-zoom level compared to other zoom levels.
// Result is not limited to MIN_ZOOM...MAX_ZOOM range.
int CViewSample::GetZoomLevel(SmpLength length) const
//---------------------------------------------------
{
	if (m_rcClient.Width() == 0 || length == 0)
		return MAX_ZOOM + 1;

	// When m_nZoom > 0, 2^(m_nZoom - 1) = samplesPerPixel  [1]
	// With auto-zoom setting the whole sample is fitted to screen:
	// ViewScreenWidthInPixels * samplesPerPixel = sampleLength (approximately)  [2].
	// Solve samplesPerPixel from [2], then "m_nZoom" from [1].
	float zoom = static_cast<float>(length) / static_cast<float>(m_rcClient.Width());
	zoom = 1 + (log10(zoom) / log10(2.0f));
	if(zoom <= 0) zoom -= 2;

	return static_cast<int>(zoom + sgn(zoom));
}


void CViewSample::DoZoom(int direction, const CPoint &zoomPoint)
//--------------------------------------------------------------
{
	const CSoundFile &sndFile = GetDocument()->GetrSoundFile();
	// zoomOrder: Biggest to smallest zoom order.
	int zoomOrder[(-MIN_ZOOM - 1) + (MAX_ZOOM + 1)];
	for(int i = 2; i < -MIN_ZOOM + 1; ++i)
		zoomOrder[i - 2] = MIN_ZOOM + i - 2;	// -6, -5, -4, -3...

	for(int i = 1; i <= MAX_ZOOM; ++i)
		zoomOrder[i - 1 + (-MIN_ZOOM - 1)] = i; // 1, 2, 3...
	zoomOrder[CountOf(zoomOrder) - 1] = 0;
	int* const pZoomOrderEnd = zoomOrder + CountOf(zoomOrder);
	int autoZoomLevel = GetZoomLevel(sndFile.GetSample(m_nSample).nLength);
	if(autoZoomLevel < MIN_ZOOM) autoZoomLevel = MIN_ZOOM;

	// If auto-zoom is not the smallest zoom, move auto-zoom index(=zero)
	// to the right position in the zoom order.
	if (autoZoomLevel < MAX_ZOOM + 1)
	{
		int* p = std::find(zoomOrder, pZoomOrderEnd, autoZoomLevel);
		if (p != pZoomOrderEnd)
		{
			memmove(p + 1, p, sizeof(zoomOrder[0]) * (pZoomOrderEnd - (p+1)));
			*p = 0;
		}
		else
			ASSERT(false);
	}
	const ptrdiff_t nPos = std::find(zoomOrder, pZoomOrderEnd, m_nZoom) - zoomOrder;

	int newZoom;
	if (direction > 0 && nPos > 0)	// Zoom in
		newZoom = zoomOrder[nPos - 1];
	else if (direction < 0 && nPos + 1 < CountOf(zoomOrder))
		newZoom = zoomOrder[nPos + 1];
	else
		return;

	if(m_rcClient.PtInRect(zoomPoint))
	{
		SetZoom(newZoom, ScreenToSample(zoomPoint.x));
	} else
	{
		SetZoom(newZoom);
	}
	SendCtrlMessage(CTRLMSG_SMP_SETZOOM, newZoom);
}


BOOL CViewSample::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
//------------------------------------------------------------------
{
	// Ctrl + mouse wheel: zoom control.
	// One scroll direction zooms in and the other zooms out.
	// This behaviour is different from what would happen if simply scrolling
	// the zoom levels in the zoom combobox.
	if (nFlags == MK_CONTROL && GetDocument())
	{
		ScreenToClient(&pt);
		DoZoom(zDelta, pt);
	}

	return CModScrollView::OnMouseWheel(nFlags, zDelta, pt);
}


void CViewSample::OnChangeGridSize()
//----------------------------------
{
	CSampleGridDlg dlg(this, m_nGridSegments, GetDocument()->GetSoundFile()->GetSample(m_nSample).nLength);
	if(dlg.DoModal() == IDOK)
	{
		m_nGridSegments = dlg.m_nSegments;
		InvalidateSample();
	}
}


void CViewSample::OnUpdateUndo(CCmdUI *pCmdUI)
//--------------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if ((pCmdUI) && (pModDoc))
	{
		pCmdUI->Enable(pModDoc->GetSampleUndo().CanUndo(m_nSample));
		pCmdUI->SetText(CString("Undo ") + CString(pModDoc->GetSampleUndo().GetUndoName(m_nSample))
			+ CString("\t") + CMainFrame::GetInputHandler()->GetKeyTextFromCommand(kcEditUndo));
	}
}


void CViewSample::OnUpdateRedo(CCmdUI *pCmdUI)
//--------------------------------------------
{
	CModDoc *pModDoc = GetDocument();
	if ((pCmdUI) && (pModDoc))
	{
		pCmdUI->Enable(pModDoc->GetSampleUndo().CanRedo(m_nSample));
		pCmdUI->SetText(CString("Redo ") + CString(pModDoc->GetSampleUndo().GetRedoName(m_nSample))
			+ CString("\t") + CMainFrame::GetInputHandler()->GetKeyTextFromCommand(kcEditRedo));
	}
}


OPENMPT_NAMESPACE_END
