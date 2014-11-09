/*
 * Moptions.cpp
 * ------------
 * Purpose: Implementation of various setup dialogs.
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"
#include <shlobj.h>
#include <afxpriv.h>
#include "mptrack.h"
#include "mainfrm.h"
#include "moptions.h"
#include "moddoc.h"
#include "Settings.h"
#include "dlg_misc.h"
#include "FileDialog.h"


OPENMPT_NAMESPACE_BEGIN


//////////////////////////////////////////////////////////////
// COptionsColors

static const struct ColorDescriptions
{
	const char *name;
	int previewImage;
	uint32 colorIndex1, colorIndex2, colorIndex3;
	const char *descText1, *descText2, *descText3;
} colorDefs[] =
{
	{"Pattern Editor",	0,	MODCOLOR_BACKNORMAL, MODCOLOR_TEXTNORMAL, MODCOLOR_BACKHILIGHT, "Background:", "Foreground:", "Highlighted:"},
	{"Active Row",		0,	MODCOLOR_BACKCURROW, MODCOLOR_TEXTCURROW, 0, "Background:", "Foreground:", NULL},
	{"Pattern Selection",0,	MODCOLOR_BACKSELECTED, MODCOLOR_TEXTSELECTED, 0, "Background:", "Foreground:", NULL},
	{"Play Cursor",		0,	MODCOLOR_BACKPLAYCURSOR, MODCOLOR_TEXTPLAYCURSOR, 0, "Background:", "Foreground:", NULL},
	{"Note Highlight",	0,	MODCOLOR_NOTE, MODCOLOR_INSTRUMENT, MODCOLOR_VOLUME, "Note:", "Instrument:", "Volume:"},
	{"Effect Highlight",0,	MODCOLOR_PANNING, MODCOLOR_PITCH, MODCOLOR_GLOBALS, "Panning Effects:", "Pitch Effects:", "Global Effects:"},
	{"Invalid Commands",0,	MODCOLOR_DODGY_COMMANDS, 0, 0, "Invalid Note:", NULL, NULL},
	{"Channel Separator",0,	MODCOLOR_SEPHILITE, MODCOLOR_SEPFACE, MODCOLOR_SEPSHADOW, "Highlight:", "Face:", "Shadow:"},
	{"Next/Prev Pattern",0,	MODCOLOR_BLENDCOLOR, 0, 0, "Blend Colour:", NULL, NULL},
	{"Sample Editor",	1,	MODCOLOR_SAMPLE, 0, 0, "Sample Data:", NULL, NULL},
	{"Instrument Editor",2,	MODCOLOR_ENVELOPES, 0, 0, "Envelopes:", NULL, NULL},
	{"VU-Meters",		0,	MODCOLOR_VUMETER_HI, MODCOLOR_VUMETER_MED, MODCOLOR_VUMETER_LO, "Hi:", "Med:", "Lo:"}
};

#define PREVIEWBMP_WIDTH	88
#define PREVIEWBMP_HEIGHT	39


BEGIN_MESSAGE_MAP(COptionsColors, CPropertyPage)
	ON_WM_DRAWITEM()
	ON_CBN_SELCHANGE(IDC_COMBO1,		OnColorSelChanged)
	ON_EN_CHANGE(IDC_PRIMARYHILITE,		OnSettingsChanged)
	ON_EN_CHANGE(IDC_SECONDARYHILITE,	OnSettingsChanged)
	ON_COMMAND(IDC_BUTTON1,				OnSelectColor1)
	ON_COMMAND(IDC_BUTTON2,				OnSelectColor2)
	ON_COMMAND(IDC_BUTTON3,				OnSelectColor3)
	ON_COMMAND(IDC_BUTTON5,				OnPresetMPT)
	ON_COMMAND(IDC_BUTTON6,				OnPresetFT2)
	ON_COMMAND(IDC_BUTTON7,				OnPresetIT)
	ON_COMMAND(IDC_BUTTON8,				OnPresetBuzz)
	ON_COMMAND(IDC_LOAD_COLORSCHEME,	OnLoadColorScheme)
	ON_COMMAND(IDC_SAVE_COLORSCHEME,	OnSaveColorScheme)
	ON_COMMAND(IDC_CHECK1,				OnSettingsChanged)
	ON_COMMAND(IDC_CHECK2,				OnPreviewChanged)
	ON_COMMAND(IDC_CHECK3,				OnSettingsChanged)
	ON_COMMAND(IDC_CHECK4,				OnPreviewChanged)
END_MESSAGE_MAP()


void COptionsColors::DoDataExchange(CDataExchange* pDX)
//-----------------------------------------------------
{
	CPropertyPage::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(COptionsColors)
	DDX_Control(pDX, IDC_COMBO1,		m_ComboItem);
	DDX_Control(pDX, IDC_BUTTON1,		m_BtnColor1);
	DDX_Control(pDX, IDC_BUTTON2,		m_BtnColor2);
	DDX_Control(pDX, IDC_BUTTON3,		m_BtnColor3);
	DDX_Control(pDX, IDC_BUTTON4,		m_BtnPreview);
	DDX_Control(pDX, IDC_TEXT1,			m_TxtColor1);
	DDX_Control(pDX, IDC_TEXT2,			m_TxtColor2);
	DDX_Control(pDX, IDC_TEXT3,			m_TxtColor3);
	//}}AFX_DATA_MAP
}


BOOL COptionsColors::OnInitDialog()
//---------------------------------
{
	CPropertyPage::OnInitDialog();
	m_pPreviewDib = LoadDib(MAKEINTRESOURCE(IDB_COLORSETUP));
	MemCopy(CustomColors, TrackerSettings::Instance().rgbCustomColors);
	for (UINT i = 0; i < CountOf(colorDefs); i++)
	{
		m_ComboItem.SetItemData(m_ComboItem.AddString(colorDefs[i].name), i);
	}
	m_ComboItem.SetCurSel(0);
	m_BtnPreview.SetWindowPos(NULL, 0,0, PREVIEWBMP_WIDTH*2+2, PREVIEWBMP_HEIGHT*2+2, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
	if (TrackerSettings::Instance().m_dwPatternSetup & PATTERN_STDHIGHLIGHT) CheckDlgButton(IDC_CHECK1, MF_CHECKED);
	if (TrackerSettings::Instance().m_dwPatternSetup & PATTERN_EFFECTHILIGHT) CheckDlgButton(IDC_CHECK2, MF_CHECKED);
	if (TrackerSettings::Instance().m_dwPatternSetup & PATTERN_SMALLFONT) CheckDlgButton(IDC_CHECK3, MF_CHECKED);
	if (TrackerSettings::Instance().m_dwPatternSetup & PATTERN_2NDHIGHLIGHT) CheckDlgButton(IDC_CHECK4, MF_CHECKED);
	SetDlgItemInt(IDC_PRIMARYHILITE, TrackerSettings::Instance().m_nRowHighlightMeasures);
	SetDlgItemInt(IDC_SECONDARYHILITE, TrackerSettings::Instance().m_nRowHighlightBeats);
	
	OnColorSelChanged();
	return TRUE;
}


BOOL COptionsColors::OnKillActive()
//---------------------------------
{
	int temp_nRowSpacing = GetDlgItemInt(IDC_PRIMARYHILITE);
	int temp_nRowSpacing2 = GetDlgItemInt(IDC_SECONDARYHILITE);

	if ((temp_nRowSpacing2 > temp_nRowSpacing))
	{
		Reporting::Warning("Error: Primary highlight must be greater than or equal secondary highlight.");
		::SetFocus(::GetDlgItem(m_hWnd, IDC_PRIMARYHILITE));
		return 0;
	}

	return CPropertyPage::OnKillActive();
}


void COptionsColors::OnOK()
//-------------------------
{
	TrackerSettings::Instance().m_dwPatternSetup &= ~(PATTERN_STDHIGHLIGHT|PATTERN_2NDHIGHLIGHT|PATTERN_EFFECTHILIGHT|PATTERN_SMALLFONT);
	if (IsDlgButtonChecked(IDC_CHECK1)) TrackerSettings::Instance().m_dwPatternSetup |= PATTERN_STDHIGHLIGHT;
	if (IsDlgButtonChecked(IDC_CHECK2)) TrackerSettings::Instance().m_dwPatternSetup |= PATTERN_EFFECTHILIGHT;
	if (IsDlgButtonChecked(IDC_CHECK3)) TrackerSettings::Instance().m_dwPatternSetup |= PATTERN_SMALLFONT;
	if (IsDlgButtonChecked(IDC_CHECK4)) TrackerSettings::Instance().m_dwPatternSetup |= PATTERN_2NDHIGHLIGHT;

	TrackerSettings::Instance().m_nRowHighlightMeasures = GetDlgItemInt(IDC_PRIMARYHILITE);
	TrackerSettings::Instance().m_nRowHighlightBeats = GetDlgItemInt(IDC_SECONDARYHILITE);

	MemCopy(TrackerSettings::Instance().rgbCustomColors, CustomColors);
	CMainFrame::UpdateColors();
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	if (pMainFrm) pMainFrm->PostMessage(WM_MOD_INVALIDATEPATTERNS, HINT_MPTOPTIONS);
	CPropertyPage::OnOK();
}


BOOL COptionsColors::OnSetActive()
//--------------------------------
{
	CMainFrame::m_nLastOptionsPage = OPTIONS_PAGE_COLORS;
	return CPropertyPage::OnSetActive();
}


void COptionsColors::OnDrawItem(int nIdCtl, LPDRAWITEMSTRUCT lpdis)
//-----------------------------------------------------------------
{
	int nColor = -1;
	switch(nIdCtl)
	{
	case IDC_BUTTON1:	nColor = colorDefs[m_nColorItem].colorIndex1; break;
	case IDC_BUTTON2:	nColor = colorDefs[m_nColorItem].colorIndex2; break;
	case IDC_BUTTON3:	nColor = colorDefs[m_nColorItem].colorIndex3; break;
	}
	if (!lpdis) return;
	if (nColor >= 0)
	{
		HPEN pen1, pen2;
		pen1 = (HPEN)::GetStockObject(WHITE_PEN);
		pen2 = (HPEN)::GetStockObject(BLACK_PEN);
		if (lpdis->itemState & ODS_SELECTED)
		{
			HPEN pentmp = pen1;
			pen1 = pen2;
			pen2 = pentmp;
		}
		HDC hdc = lpdis->hDC;
		HBRUSH brush = ::CreateSolidBrush(CustomColors[nColor]);
		::FillRect(hdc, &lpdis->rcItem, brush);
		::DeleteObject(brush);
		HPEN oldpen = (HPEN)::SelectObject(hdc, pen1);
		::MoveToEx(hdc, lpdis->rcItem.left, lpdis->rcItem.bottom-1, NULL);
		::LineTo(hdc, lpdis->rcItem.left, lpdis->rcItem.top);
		::LineTo(hdc, lpdis->rcItem.right, lpdis->rcItem.top);
		::SelectObject(hdc, pen2);
		::MoveToEx(hdc, lpdis->rcItem.right-1, lpdis->rcItem.top, NULL);
		::LineTo(hdc, lpdis->rcItem.right-1, lpdis->rcItem.bottom-1);
		::LineTo(hdc, lpdis->rcItem.left, lpdis->rcItem.bottom-1);
		if (oldpen) ::SelectObject(hdc, oldpen);
	} else
	if ((nIdCtl == IDC_BUTTON4) && (m_pPreviewDib))
	{
		int y = colorDefs[m_nColorItem].previewImage;
		RGBQUAD *p = m_pPreviewDib->bmiColors;
		if (IsDlgButtonChecked(IDC_CHECK2))
		{
			p[1] = rgb2quad(CustomColors[MODCOLOR_GLOBALS]);
			p[3] = rgb2quad(CustomColors[MODCOLOR_PITCH]);
			p[5] = rgb2quad(CustomColors[MODCOLOR_INSTRUMENT]);
			p[6] = rgb2quad(CustomColors[MODCOLOR_VOLUME]);
			p[12] = rgb2quad(CustomColors[MODCOLOR_NOTE]);
			p[14] = rgb2quad(CustomColors[MODCOLOR_PANNING]);
		} else
		{
			p[1] = rgb2quad(CustomColors[MODCOLOR_TEXTNORMAL]);
			p[3] = rgb2quad(CustomColors[MODCOLOR_TEXTNORMAL]);
			p[5] = rgb2quad(CustomColors[MODCOLOR_TEXTNORMAL]);
			p[6] = rgb2quad(CustomColors[MODCOLOR_TEXTNORMAL]);
			p[12] = rgb2quad(CustomColors[MODCOLOR_TEXTNORMAL]);
			p[14] = rgb2quad(CustomColors[MODCOLOR_TEXTNORMAL]);
		}
		p[4] = rgb2quad(CustomColors[MODCOLOR_TEXTNORMAL]);
		p[8] = rgb2quad(CustomColors[MODCOLOR_TEXTNORMAL]);
		p[9] = rgb2quad(CustomColors[MODCOLOR_SAMPLE]);
		p[10] = rgb2quad(CustomColors[MODCOLOR_BACKNORMAL]);
		p[11] = rgb2quad(CustomColors[MODCOLOR_BACKHILIGHT]);
		p[13] = rgb2quad(CustomColors[MODCOLOR_ENVELOPES]);
		p[15] = rgb2quad((y) ? RGB(255,255,255) : CustomColors[MODCOLOR_BACKNORMAL]);
		// Special cases: same bitmap, different palette
		switch(m_nColorItem)
		{
		// Current Row
		case 1:
			p[8] = rgb2quad(CustomColors[MODCOLOR_TEXTCURROW]);
			p[11] = rgb2quad(CustomColors[MODCOLOR_BACKCURROW]);
			break;
		// Selection
		case 2:
			p[5] = rgb2quad(CustomColors[MODCOLOR_TEXTSELECTED]);
			p[6] = rgb2quad(CustomColors[MODCOLOR_TEXTSELECTED]);
			p[8] = rgb2quad(CustomColors[MODCOLOR_TEXTSELECTED]);
			p[11] = rgb2quad(CustomColors[MODCOLOR_BACKSELECTED]);
			p[12] = rgb2quad(CustomColors[MODCOLOR_TEXTSELECTED]);
			break;
		// Play Cursor
		case 3:
			p[8] = rgb2quad(CustomColors[MODCOLOR_TEXTPLAYCURSOR]);
			p[11] = rgb2quad(CustomColors[MODCOLOR_BACKPLAYCURSOR]);
			break;
		}
		HDC hdc = lpdis->hDC;
		HPEN oldpen = (HPEN)::SelectObject(hdc, CMainFrame::penDarkGray);
		::MoveToEx(hdc, lpdis->rcItem.left, lpdis->rcItem.bottom-1, NULL);
		::LineTo(hdc, lpdis->rcItem.left, lpdis->rcItem.top);
		::LineTo(hdc, lpdis->rcItem.right, lpdis->rcItem.top);
		::SelectObject(hdc, CMainFrame::penLightGray);
		::MoveToEx(hdc, lpdis->rcItem.right-1, lpdis->rcItem.top, NULL);
		::LineTo(hdc, lpdis->rcItem.right-1, lpdis->rcItem.bottom-1);
		::LineTo(hdc, lpdis->rcItem.left, lpdis->rcItem.bottom-1);
		if (oldpen) ::SelectObject(hdc, oldpen);
		StretchDIBits(	hdc,
						lpdis->rcItem.left+1,
						lpdis->rcItem.top+1,
						lpdis->rcItem.right - lpdis->rcItem.left - 2,
						lpdis->rcItem.bottom - lpdis->rcItem.top - 2,
						0,
						m_pPreviewDib->bmiHeader.biHeight - ((y+1) * PREVIEWBMP_HEIGHT),
						m_pPreviewDib->bmiHeader.biWidth,
						PREVIEWBMP_HEIGHT,
						m_pPreviewDib->lpDibBits,
						(LPBITMAPINFO)m_pPreviewDib,
						DIB_RGB_COLORS,
						SRCCOPY);
	}
}


static DWORD rgbCustomColors[16] =
{
	0x808080,	0x0000FF,	0x00FF00,	0x00FFFF,
	0xFF0000,	0xFF00FF,	0xFFFF00,	0xFFFFFF,
	0xC0C0C0,	0x80FFFF,	0xE0E8E0,	0x606060,
	0x505050,	0x404040,	0x004000,	0x000000,
};


void COptionsColors::SelectColor(COLORREF *lprgb)
//-----------------------------------------------
{
	CHOOSECOLOR cc;
	cc.lStructSize = sizeof(CHOOSECOLOR);
	cc.hwndOwner = m_hWnd;
	cc.hInstance = NULL;
	cc.rgbResult = *lprgb;
	cc.lpCustColors = rgbCustomColors;
	cc.Flags = CC_RGBINIT;
	cc.lCustData = 0;
	cc.lpfnHook = NULL;
	cc.lpTemplateName = NULL;
	if (::ChooseColor(&cc))
	{
		*lprgb = cc.rgbResult;
		InvalidateRect(NULL, FALSE);
		OnSettingsChanged();
	}
}


void COptionsColors::OnSelectColor1()
//-----------------------------------
{
	SelectColor(&CustomColors[colorDefs[m_nColorItem].colorIndex1]);
}


void COptionsColors::OnSelectColor2()
//-----------------------------------
{
	SelectColor(&CustomColors[colorDefs[m_nColorItem].colorIndex2]);
}


void COptionsColors::OnSelectColor3()
//-----------------------------------
{
	SelectColor(&CustomColors[colorDefs[m_nColorItem].colorIndex3]);
}


void COptionsColors::OnColorSelChanged()
//--------------------------------------
{
	int sel = m_ComboItem.GetCurSel();
	if (sel >= 0)
	{
		m_nColorItem = m_ComboItem.GetItemData(sel);
		OnUpdateDialog();
	}
}

void COptionsColors::OnSettingsChanged()
//--------------------------------------
{
	SetModified(TRUE);
}

void COptionsColors::OnUpdateDialog()
//-----------------------------------
{
	const ColorDescriptions *p = &colorDefs[m_nColorItem];
	if (p->descText1) m_TxtColor1.SetWindowText(p->descText1);
	if (p->descText2)
	{
		m_TxtColor2.SetWindowText(p->descText2);
		m_TxtColor2.ShowWindow(SW_SHOW);
		m_BtnColor2.ShowWindow(SW_SHOW);
		m_BtnColor2.InvalidateRect(NULL, FALSE);
	} else
	{
		m_TxtColor2.ShowWindow(SW_HIDE);
		m_BtnColor2.ShowWindow(SW_HIDE);
	}
	if (p->descText3)
	{
		m_TxtColor3.SetWindowText(p->descText3);
		m_TxtColor3.ShowWindow(SW_SHOW);
		m_BtnColor3.ShowWindow(SW_SHOW);
		m_BtnColor3.InvalidateRect(NULL, FALSE);
	} else
	{
		m_TxtColor3.ShowWindow(SW_HIDE);
		m_BtnColor3.ShowWindow(SW_HIDE);
	}
	m_BtnColor1.InvalidateRect(NULL, FALSE);
	m_BtnPreview.InvalidateRect(NULL, FALSE);
}


void COptionsColors::OnPreviewChanged()
//-------------------------------------
{
	OnSettingsChanged();
	m_BtnPreview.InvalidateRect(NULL, FALSE);
	m_BtnColor1.InvalidateRect(NULL, FALSE);
	m_BtnColor2.InvalidateRect(NULL, FALSE);
	m_BtnColor3.InvalidateRect(NULL, FALSE);
}

void COptionsColors::OnPresetMPT()
//--------------------------------
{
	TrackerSettings::GetDefaultColourScheme(CustomColors);
	OnPreviewChanged();
}


void COptionsColors::OnPresetFT2()
//--------------------------------
{
	// "blue"
	CustomColors[MODCOLOR_BACKNORMAL] = RGB(0x00, 0x00, 0x00);
	CustomColors[MODCOLOR_TEXTNORMAL] = RGB(0xE0, 0xE0, 0x40);
	CustomColors[MODCOLOR_BACKCURROW] = RGB(0x70, 0x70, 0x70);
	CustomColors[MODCOLOR_TEXTCURROW] = RGB(0xF0, 0xF0, 0x50);
	CustomColors[MODCOLOR_BACKSELECTED] = RGB(0x40, 0x40, 0xA0);
	CustomColors[MODCOLOR_TEXTSELECTED] = RGB(0xFF, 0xFF, 0xFF);
	CustomColors[MODCOLOR_SAMPLE] = RGB(0xFF, 0x00, 0x00);
	CustomColors[MODCOLOR_BACKPLAYCURSOR] = RGB(0x50, 0x50, 0x70);
	CustomColors[MODCOLOR_TEXTPLAYCURSOR] = RGB(0xE0, 0xE0, 0x40);
	CustomColors[MODCOLOR_BACKHILIGHT] = RGB(0x40, 0x40, 0x80);
	CustomColors[MODCOLOR_NOTE] = RGB(0xE0, 0xE0, 0x40);
	CustomColors[MODCOLOR_INSTRUMENT] = RGB(0xFF, 0xFF, 0x00);
	CustomColors[MODCOLOR_VOLUME] = RGB(0x00, 0xFF, 0x00);
	CustomColors[MODCOLOR_PANNING] = RGB(0x00, 0xFF, 0xFF);
	CustomColors[MODCOLOR_PITCH] = RGB(0xFF, 0xFF, 0x00);
	CustomColors[MODCOLOR_GLOBALS] = RGB(0xFF, 0x40, 0x40);
	CustomColors[MODCOLOR_ENVELOPES] = RGB(0x00, 0x00, 0xFF);
	CustomColors[MODCOLOR_VUMETER_LO] = RGB(0x00, 0xC8, 0x00);
	CustomColors[MODCOLOR_VUMETER_MED] = RGB(0xFF, 0xC8, 0x00);
	CustomColors[MODCOLOR_VUMETER_HI] = RGB(0xE1, 0x00, 0x00);
	CustomColors[MODCOLOR_SEPSHADOW] = RGB(0x2E, 0x2E, 0x5C);
	CustomColors[MODCOLOR_SEPFACE] = RGB(0x40, 0x40, 0x80);
	CustomColors[MODCOLOR_SEPHILITE] = RGB(0x99, 0x99, 0xCC);
	CustomColors[MODCOLOR_BLENDCOLOR] = RGB(0x2E, 0x2E, 0x5A);
	CustomColors[MODCOLOR_DODGY_COMMANDS] = RGB(0xC0, 0x40, 0x40);
	OnPreviewChanged();
}


void COptionsColors::OnPresetIT()
//-------------------------------
{
	// "green"
	CustomColors[MODCOLOR_BACKNORMAL] = RGB(0x00, 0x00, 0x00);
	CustomColors[MODCOLOR_TEXTNORMAL] = RGB(0x00, 0xE0, 0x00);
	CustomColors[MODCOLOR_BACKCURROW] = RGB(0x70, 0x70, 0x70);
	CustomColors[MODCOLOR_TEXTCURROW] = RGB(0x00, 0xE0, 0x00);
	CustomColors[MODCOLOR_BACKSELECTED] = RGB(0xE0, 0xE0, 0xE0);
	CustomColors[MODCOLOR_TEXTSELECTED] = RGB(0x00, 0x00, 0x00);
	CustomColors[MODCOLOR_SAMPLE] = RGB(0xFF, 0x00, 0x00);
	CustomColors[MODCOLOR_BACKPLAYCURSOR] = RGB(0x80, 0x80, 0x00);
	CustomColors[MODCOLOR_TEXTPLAYCURSOR] = RGB(0x00, 0xE0, 0x00);
	CustomColors[MODCOLOR_BACKHILIGHT] = RGB(0x40, 0x68, 0x40);
	CustomColors[MODCOLOR_NOTE] = RGB(0x00, 0xFF, 0x00);
	CustomColors[MODCOLOR_INSTRUMENT] = RGB(0xFF, 0xFF, 0x00);
	CustomColors[MODCOLOR_VOLUME] = RGB(0x00, 0xFF, 0x00);
	CustomColors[MODCOLOR_PANNING] = RGB(0x00, 0xFF, 0xFF);
	CustomColors[MODCOLOR_PITCH] = RGB(0xFF, 0xFF, 0x00);
	CustomColors[MODCOLOR_GLOBALS] = RGB(0xFF, 0x40, 0x40);
	CustomColors[MODCOLOR_ENVELOPES] = RGB(0x00, 0x00, 0xFF);
	CustomColors[MODCOLOR_VUMETER_LO] = RGB(0x00, 0xC8, 0x00);
	CustomColors[MODCOLOR_VUMETER_MED] = RGB(0xFF, 0xC8, 0x00);
	CustomColors[MODCOLOR_VUMETER_HI] = RGB(0xE1, 0x00, 0x00);
	CustomColors[MODCOLOR_SEPSHADOW] = RGB(0x23, 0x38, 0x23);
	CustomColors[MODCOLOR_SEPFACE] = RGB(0x40, 0x68, 0x40);
	CustomColors[MODCOLOR_SEPHILITE] = RGB(0x94, 0xBC, 0x94);
	CustomColors[MODCOLOR_BLENDCOLOR] = RGB(0x00, 0x40, 0x00);
	CustomColors[MODCOLOR_DODGY_COMMANDS] = RGB(0xFF, 0x80, 0x80);
	OnPreviewChanged();
}


void COptionsColors::OnPresetBuzz()
//---------------------------------
{
	CustomColors[MODCOLOR_BACKNORMAL] = RGB(0xE1, 0xDB, 0xD0);
	CustomColors[MODCOLOR_TEXTNORMAL] = RGB(0x3A, 0x34, 0x27);
	CustomColors[MODCOLOR_BACKCURROW] = RGB(0xC0, 0xB4, 0x9E);
	CustomColors[MODCOLOR_TEXTCURROW] = RGB(0x00, 0x00, 0x00);
	CustomColors[MODCOLOR_BACKSELECTED] = RGB(0x00, 0x00, 0x00);
	CustomColors[MODCOLOR_TEXTSELECTED] = RGB(0xDD, 0xD7, 0xCC);
	CustomColors[MODCOLOR_SAMPLE] = RGB(0x00, 0xFF, 0x00);
	CustomColors[MODCOLOR_BACKPLAYCURSOR] = RGB(0xA9, 0x99, 0x7A);
	CustomColors[MODCOLOR_TEXTPLAYCURSOR] = RGB(0x00, 0x00, 0x00);
	CustomColors[MODCOLOR_BACKHILIGHT] = RGB(0xCE, 0xC5, 0xB5);
	CustomColors[MODCOLOR_NOTE] = RGB(0x00, 0x00, 0x5B);
	CustomColors[MODCOLOR_INSTRUMENT] = RGB(0x00, 0x55, 0x55);
	CustomColors[MODCOLOR_VOLUME] = RGB(0x00, 0x5E, 0x00);
	CustomColors[MODCOLOR_PANNING] = RGB(0x00, 0x68, 0x68);
	CustomColors[MODCOLOR_PITCH] = RGB(0x62, 0x62, 0x00);
	CustomColors[MODCOLOR_GLOBALS] = RGB(0x66, 0x00, 0x00);
	CustomColors[MODCOLOR_ENVELOPES] = RGB(0xFF, 0x00, 0x00);
	CustomColors[MODCOLOR_VUMETER_LO] = RGB(0x00, 0xC8, 0x00);
	CustomColors[MODCOLOR_VUMETER_MED] = RGB(0xFF, 0xC8, 0x00);
	CustomColors[MODCOLOR_VUMETER_HI] = RGB(0xE1, 0x00, 0x00);
	CustomColors[MODCOLOR_SEPSHADOW] = RGB(0xAC, 0xA8, 0xA1);
	CustomColors[MODCOLOR_SEPFACE] = RGB(0xD6, 0xD0, 0xC6);
	CustomColors[MODCOLOR_SEPHILITE] = RGB(0xEC, 0xE8, 0xE1);
	CustomColors[MODCOLOR_BLENDCOLOR] = RGB(0xE1, 0xDB, 0xD0);
	CustomColors[MODCOLOR_DODGY_COMMANDS] = RGB(0xC0, 0x00, 0x00);
	OnPreviewChanged();
}

void COptionsColors::OnLoadColorScheme()
//--------------------------------------
{
	FileDialog dlg = OpenFileDialog()
		.DefaultExtension("mptcolor")
		.ExtensionFilter("OpenMPT Color Schemes|*.mptcolor||")
		.WorkingDirectory(theApp.GetConfigPath());
	if(!dlg.Show(this)) return;

	// Ensure that all colours are reset (for outdated colour schemes)
	OnPresetMPT();
	{
		IniFileSettingsContainer file(dlg.GetFirstFile());
		for(int i = 0; i < MAX_MODCOLORS; i++)
		{
			TCHAR sKeyName[16];
			wsprintf(sKeyName, "Color%02d", i);
			CustomColors[i] = file.Read<int32>("Colors", sKeyName, CustomColors[i]);
		}
	}
	OnPreviewChanged();
}

void COptionsColors::OnSaveColorScheme()
//--------------------------------------
{
	FileDialog dlg = SaveFileDialog()
		.DefaultExtension("mptcolor")
		.ExtensionFilter("OpenMPT Color Schemes|*.mptcolor||")
		.WorkingDirectory(theApp.GetConfigPath());
	if(!dlg.Show(this)) return;

	{
		IniFileSettingsContainer file(dlg.GetFirstFile());
		for(int i = 0; i < MAX_MODCOLORS; i++)
		{
			TCHAR sKeyName[16];
			wsprintf(sKeyName, "Color%02d", i);
			file.Write<int32>("Colors", sKeyName, CustomColors[i]);
		}
	}
}


/////////////////////////////////////////////////////////////////////////////////
// COptionsGeneral

BEGIN_MESSAGE_MAP(COptionsGeneral, CPropertyPage)
	ON_EN_CHANGE(IDC_OPTIONS_DIR_MODS,			OnSettingsChanged)
	ON_EN_CHANGE(IDC_OPTIONS_DIR_SAMPS,			OnSettingsChanged)
	ON_EN_CHANGE(IDC_OPTIONS_DIR_INSTS,			OnSettingsChanged)
	ON_EN_CHANGE(IDC_OPTIONS_DIR_VSTPRESETS,	OnSettingsChanged)
	ON_LBN_SELCHANGE(IDC_LIST1,					OnOptionSelChanged)
	ON_COMMAND(IDC_BUTTON_CHANGE_MODDIR,		OnBrowseSongs)
	ON_COMMAND(IDC_BUTTON_CHANGE_SAMPDIR,		OnBrowseSamples)
	ON_COMMAND(IDC_BUTTON_CHANGE_INSTRDIR,		OnBrowseInstruments)
	ON_COMMAND(IDC_BUTTON_CHANGE_VSTDIR,		OnBrowsePlugins)
	ON_COMMAND(IDC_BUTTON_CHANGE_VSTPRESETSDIR,	OnBrowsePresets)
	ON_CLBN_CHKCHANGE(IDC_LIST1,				OnSettingsChanged)
END_MESSAGE_MAP()


static const struct GeneralOptionsDescriptions
{
	uint32 flag;
	const char *name, *description;
} generalOptionsList[] =
{
	{PATTERN_PLAYNEWNOTE,	"Play new notes while recording",	"When this option is enabled, notes entered in the pattern editor will always be played (If not checked, notes won't be played in record mode)."},
	{PATTERN_PLAYEDITROW,	"Play whole row while recording",	"When this option is enabled, all notes on the current row are played when entering notes in the pattern editor."},
	{PATTERN_CENTERROW,		"Always center active row",			"Turn on this option to have the active row always centered in the pattern editor."},
	{PATTERN_LARGECOMMENTS,	"Use large font for comments",		"With this option enabled, the song message editor will use a larger font."},
	{PATTERN_HEXDISPLAY,	"Display rows in hex",				"With this option enabled, row numbers and sequence numbers will be displayed in hexadecimal."},
	{PATTERN_WRAP,			"Cursor wrap in pattern editor",	"When this option is active, going past the end of a pattern row or channel will move the cursor to the beginning. When \"Continuous scroll\"-option is enabled, row wrap is disabled."},
	{PATTERN_CREATEBACKUP,	"Create backup files (*.bak)",		"When this option is active, saving a file will create a backup copy of the original."},
	{PATTERN_DRAGNDROPEDIT,	"Drag and Drop Editing",			"Enable moving a selection in the pattern editor (copying if pressing shift while dragging)"},
	{PATTERN_FLATBUTTONS,	"Flat Buttons",						"Use flat buttons in toolbars"},
	{PATTERN_SINGLEEXPAND,	"Single click to expand tree",		"Single-clicking in the left tree view will expand a node."},
	{PATTERN_MUTECHNMODE,	"Ignored muted channels",			"Notes will not be played on muted channels (unmuting will only start on a new note)."},
	{PATTERN_NOEXTRALOUD,	"No loud sample preview",			"Disable loud playback of samples in the sample/instrument editor. Sample volume depends on the sample volume slider on the general tab when activated (if disabled, samples are previewed at 0 dB)."},
	{PATTERN_SHOWPREVIOUS,	"Show Prev/Next patterns",			"Displays grayed-out version of the previous/next patterns in the pattern editor. Does not work if \"always center active row\" is disabled."},
	{PATTERN_CONTSCROLL,	"Continuous scroll",				"Jumps to the next pattern when moving past the end of a pattern"},
	{PATTERN_KBDNOTEOFF,	"Record note off",					"Record note off when a key is released on the PC keyboard."},
	{PATTERN_FOLLOWSONGOFF,	"Follow song off by default",		"Ensure follow song is off when opening or starting a new song."},
	{PATTERN_MIDIRECORD,	"MIDI record",						"Enable MIDI in record by default."},
	{PATTERN_OLDCTXMENUSTYLE, "Old style pattern context menu", "Check this option to hide unavailable items in the pattern editor context menu. Uncheck to grey-out unavailable items instead."},
	{PATTERN_SYNCMUTE,		"Maintain sample sync on mute",		"Samples continue to be processed when channels are muted (like in IT2 and FT2)"},
	{PATTERN_SYNCSAMPLEPOS,	"Maintain sample sync on seek",		"Sample that are still active from previous patterns are continued to be played after seeking.\nNote: Samples with portamento effects applied are not synced. This feature may slow down seeking."},
	{PATTERN_AUTODELAY,		"Automatic delay commands",			"Automatically insert appropriate note-delay commands when recording notes during live playback."},
	{PATTERN_NOTEFADE,		"Note fade on key up",				"Enable to fade / stop notes on key up in pattern tab."},
	{PATTERN_OVERFLOWPASTE,	"Overflow paste mode",				"Wrap pasted pattern data into next pattern. This is useful for creating echo channels."},
	{PATTERN_RESETCHANNELS,	"Reset channels on loop",			"If enabled, channels will be reset to their initial state when song looping is enabled.\nNote: This does not affect manual song loops (i.e. triggered by pattern commands) and is not recommended to be enabled."},
	{PATTERN_LIVEUPDATETREE,"Update sample status in tree",		"If enabled, active samples and instruments will be indicated by a different icon in the treeview."},
	{PATTERN_NOCLOSEDIALOG,	"Disable modern close dialog",		"When closing the main window, a confirmation window is shown for every unsaved document instead of one single window with a list of unsaved documents."},
	{PATTERN_DBLCLICKSELECT, "Double-click to select channel",	"Instead of showing the note properties, double-clicking a pattern cell selects the whole channel."},
	{PATTERN_SHOWDEFAULTVOLUME, "Show default volume commands",	"If there is no volume command next to a note + instrument combination, the sample's default volume is shown."},
};


void COptionsGeneral::DoDataExchange(CDataExchange* pDX)
//------------------------------------------------------
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CModTypeDlg)
	DDX_Control(pDX, IDC_LIST1,			m_CheckList);
	//}}AFX_DATA_MAP
}


BOOL COptionsGeneral::OnInitDialog()
//----------------------------------
{

	CPropertyPage::OnInitDialog();
	for(size_t i = 0; i < CountOf(generalOptionsList); i++)
	{
		m_CheckList.AddString(generalOptionsList[i].name);
		const int check = (TrackerSettings::Instance().m_dwPatternSetup & generalOptionsList[i].flag) != 0 ? BST_CHECKED : BST_UNCHECKED;
		m_CheckList.SetCheck(i, check);
	}
	m_CheckList.SetCurSel(0);
	OnOptionSelChanged();

	::SetDlgItemTextW(m_hWnd, IDC_OPTIONS_DIR_MODS, TrackerDirectories::Instance().GetDefaultDirectory(DIR_MODS).AsNative().c_str());
	::SetDlgItemTextW(m_hWnd, IDC_OPTIONS_DIR_SAMPS, TrackerDirectories::Instance().GetDefaultDirectory(DIR_SAMPLES).AsNative().c_str());
	::SetDlgItemTextW(m_hWnd, IDC_OPTIONS_DIR_INSTS, TrackerDirectories::Instance().GetDefaultDirectory(DIR_INSTRUMENTS).AsNative().c_str());
	::SetDlgItemTextW(m_hWnd, IDC_OPTIONS_DIR_VSTS, TrackerDirectories::Instance().GetDefaultDirectory(DIR_PLUGINS).AsNative().c_str());
	::SetDlgItemTextW(m_hWnd, IDC_OPTIONS_DIR_VSTPRESETS,	TrackerDirectories::Instance().GetDefaultDirectory(DIR_PLUGINPRESETS).AsNative().c_str());

	return TRUE;
}


void COptionsGeneral::OnOK()
//--------------------------
{
	// Default paths
	WCHAR szModDir[MAX_PATH], szSmpDir[MAX_PATH], szInsDir[MAX_PATH], szVstDir[MAX_PATH], szPresetDir[MAX_PATH];
	szModDir[0] = szInsDir[0] = szSmpDir[0] = szVstDir[0] = szPresetDir[0] = 0;
	::GetDlgItemTextW(m_hWnd, IDC_OPTIONS_DIR_MODS, szModDir, MAX_PATH);
	::GetDlgItemTextW(m_hWnd, IDC_OPTIONS_DIR_SAMPS, szSmpDir, MAX_PATH);
	::GetDlgItemTextW(m_hWnd, IDC_OPTIONS_DIR_INSTS, szInsDir, MAX_PATH);
	::GetDlgItemTextW(m_hWnd, IDC_OPTIONS_DIR_VSTS, szVstDir, MAX_PATH);
	::GetDlgItemTextW(m_hWnd, IDC_OPTIONS_DIR_VSTPRESETS, szPresetDir, MAX_PATH);

	for(size_t i = 0; i < CountOf(generalOptionsList); i++)
	{
		const bool check = (m_CheckList.GetCheck(i) != BST_UNCHECKED);

		if(check) TrackerSettings::Instance().m_dwPatternSetup |= generalOptionsList[i].flag;
		else TrackerSettings::Instance().m_dwPatternSetup &= ~generalOptionsList[i].flag;
	}

	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	if (pMainFrm)
	{
		pMainFrm->SetupDirectories(
			mpt::PathString::FromNative(szModDir),
			mpt::PathString::FromNative(szSmpDir),
			mpt::PathString::FromNative(szInsDir),
			mpt::PathString::FromNative(szVstDir),
			mpt::PathString::FromNative(szPresetDir)
			);
		pMainFrm->SetupMiscOptions();
	}

	CPropertyPage::OnOK();
}


BOOL COptionsGeneral::OnSetActive()
//---------------------------------
{
	CMainFrame::m_nLastOptionsPage = OPTIONS_PAGE_GENERAL;
	return CPropertyPage::OnSetActive();
}


void COptionsGeneral::BrowseForFolder(UINT nID)
//---------------------------------------------
{
	WCHAR szPath[MAX_PATH] = L"";
	::GetDlgItemTextW(m_hWnd, nID, szPath, CountOf(szPath));

	OPENMPT_NAMESPACE::BrowseForFolder dlg(mpt::PathString::FromNative(szPath), TEXT("Select a default folder..."));
	if(dlg.Show(this))
	{
		::SetDlgItemTextW(m_hWnd, nID, dlg.GetDirectory().AsNative().c_str());
		OnSettingsChanged();
	}
}


void COptionsGeneral::OnOptionSelChanged()
//----------------------------------------
{
	LPCSTR pszDesc = NULL;
	const int sel = m_CheckList.GetCurSel();
	if ((sel >= 0) && (sel < CountOf(generalOptionsList)))
	{
		pszDesc = generalOptionsList[sel].description;
	}
	SetDlgItemText(IDC_TEXT1, (pszDesc) ? pszDesc : "");
}


BEGIN_MESSAGE_MAP(COptionsSampleEditor, CPropertyPage)
	ON_WM_HSCROLL()
	ON_EN_CHANGE(IDC_EDIT_UNDOSIZE,			OnUndoSizeChanged)
	ON_EN_CHANGE(IDC_EDIT_FINETUNE,			OnSettingsChanged)
	ON_EN_CHANGE(IDC_FLAC_COMPRESSION,		OnSettingsChanged)
	ON_CBN_SELCHANGE(IDC_DEFAULT_FORMAT,	OnSettingsChanged)
	ON_CBN_SELCHANGE(IDC_VOLUME_HANDLING,	OnSettingsChanged)
	ON_COMMAND(IDC_RADIO1,					OnSettingsChanged)
	ON_COMMAND(IDC_RADIO2,					OnSettingsChanged)
	ON_COMMAND(IDC_RADIO3,					OnSettingsChanged)
	ON_COMMAND(IDC_COMPRESS_ITI,			OnSettingsChanged)
	ON_COMMAND(IDC_PREVIEW_SAMPLES,			OnSettingsChanged)
	ON_COMMAND(IDC_NORMALIZE,				OnSettingsChanged)
END_MESSAGE_MAP()


void COptionsSampleEditor::DoDataExchange(CDataExchange* pDX)
//-----------------------------------------------------------
{
	CPropertyPage::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(COptionsSampleEditor)
	DDX_Control(pDX, IDC_DEFAULT_FORMAT,		m_cbnDefaultSampleFormat);
	DDX_Control(pDX, IDC_VOLUME_HANDLING,		m_cbnDefaultVolumeHandling);
	//}}AFX_DATA_MAP
}


BOOL COptionsSampleEditor::OnInitDialog()
//---------------------------------------
{
	CPropertyPage::OnInitDialog();
	SetDlgItemInt(IDC_EDIT_FINETUNE, TrackerSettings::Instance().m_nFinetuneStep);
	SetDlgItemInt(IDC_EDIT_UNDOSIZE, TrackerSettings::Instance().m_SampleUndoBufferSize.Get().GetSizeInPercent());
	RecalcUndoSize();

	m_cbnDefaultSampleFormat.SetItemData(m_cbnDefaultSampleFormat.AddString("FLAC"), dfFLAC);
	m_cbnDefaultSampleFormat.SetItemData(m_cbnDefaultSampleFormat.AddString("WAV"), dfWAV);
	m_cbnDefaultSampleFormat.SetItemData(m_cbnDefaultSampleFormat.AddString("RAW"), dfRAW);
	m_cbnDefaultSampleFormat.SetCurSel(TrackerSettings::Instance().m_defaultSampleFormat);

	CSliderCtrl *slider = static_cast<CSliderCtrl *>(GetDlgItem(IDC_SLIDER1));
	slider->SetRange(0, 8);
	slider->SetTicFreq(1);
	slider->SetPos(TrackerSettings::Instance().m_FLACCompressionLevel);

	CheckRadioButton(IDC_RADIO1, IDC_RADIO3, IDC_RADIO1 + TrackerSettings::Instance().sampleEditorKeyBehaviour);

	CheckDlgButton(IDC_COMPRESS_ITI, TrackerSettings::Instance().compressITI ? MF_CHECKED : MF_UNCHECKED);

	m_cbnDefaultVolumeHandling.SetItemData(m_cbnDefaultVolumeHandling.AddString("MIDI volume"), PLUGIN_VOLUMEHANDLING_MIDI);
	m_cbnDefaultVolumeHandling.SetItemData(m_cbnDefaultVolumeHandling.AddString("Dry/Wet ratio"), PLUGIN_VOLUMEHANDLING_DRYWET);
	m_cbnDefaultVolumeHandling.SetItemData(m_cbnDefaultVolumeHandling.AddString("None"), PLUGIN_VOLUMEHANDLING_IGNORE);
	m_cbnDefaultVolumeHandling.SetCurSel(TrackerSettings::Instance().DefaultPlugVolumeHandling);

	CheckDlgButton(IDC_PREVIEW_SAMPLES, TrackerSettings::Instance().previewInFileDialogs ? MF_CHECKED : MF_UNCHECKED);
	CheckDlgButton(IDC_NORMALIZE, TrackerSettings::Instance().m_MayNormalizeSamplesOnLoad ? MF_CHECKED : MF_UNCHECKED);

	return TRUE;
}


void COptionsSampleEditor::OnOK()
//-------------------------------
{
	CPropertyPage::OnOK();

	TrackerSettings::Instance().m_nFinetuneStep = GetDlgItemInt(IDC_EDIT_FINETUNE);
	TrackerSettings::Instance().m_SampleUndoBufferSize = SampleUndoBufferSize(GetDlgItemInt(IDC_EDIT_UNDOSIZE));
	TrackerSettings::Instance().m_defaultSampleFormat = static_cast<SampleEditorDefaultFormat>(m_cbnDefaultSampleFormat.GetItemData(m_cbnDefaultSampleFormat.GetCurSel()));
	TrackerSettings::Instance().m_FLACCompressionLevel = static_cast<CSliderCtrl *>(GetDlgItem(IDC_SLIDER1))->GetPos();
	TrackerSettings::Instance().sampleEditorKeyBehaviour = static_cast<SampleEditorKeyBehaviour>(GetCheckedRadioButton(IDC_RADIO1, IDC_RADIO3) -IDC_RADIO1);
	TrackerSettings::Instance().compressITI = IsDlgButtonChecked(IDC_COMPRESS_ITI) != MF_UNCHECKED;
	TrackerSettings::Instance().DefaultPlugVolumeHandling = static_cast<PLUGVOLUMEHANDLING>(m_cbnDefaultVolumeHandling.GetItemData(m_cbnDefaultVolumeHandling.GetCurSel()));
	TrackerSettings::Instance().previewInFileDialogs = IsDlgButtonChecked(IDC_PREVIEW_SAMPLES) != MF_UNCHECKED;
	TrackerSettings::Instance().m_MayNormalizeSamplesOnLoad = IsDlgButtonChecked(IDC_NORMALIZE) != MF_UNCHECKED;

	std::vector<CModDoc *> docs = theApp.GetOpenDocuments();
	for(std::vector<CModDoc *>::iterator i = docs.begin(); i != docs.end(); i++)
	{
		(**i).GetSampleUndo().RestrictBufferSize();
	}
}


BOOL COptionsSampleEditor::OnSetActive()
//--------------------------------------
{
	CMainFrame::m_nLastOptionsPage = OPTIONS_PAGE_SAMPLEDITOR;
	return CPropertyPage::OnSetActive();
}


void COptionsSampleEditor::OnUndoSizeChanged()
//--------------------------------------------
{
	RecalcUndoSize();
	OnSettingsChanged();
}


void COptionsSampleEditor::RecalcUndoSize()
//-----------------------------------------
{
	UINT sizePercent = GetDlgItemInt(IDC_EDIT_UNDOSIZE);
	uint32 sizeMB = mpt::saturate_cast<uint32>(SampleUndoBufferSize(sizePercent).GetSizeInBytes() >> 20);
	CString text = _T("% of physical memory (");
	if(sizePercent)
		text.AppendFormat(_T("%u MiB)"), sizeMB);
	else
		text.Append(_T("disabled)"));
	SetDlgItemText(IDC_UNDOSIZE, text);
}


#if defined(MPT_SETTINGS_CACHE)

BEGIN_MESSAGE_MAP(COptionsAdvanced, CPropertyPage)
	ON_LBN_DBLCLK(IDC_LIST4,	OnOptionDblClick)
	ON_EN_CHANGE(IDC_EDIT1,		OnFindStringChanged)
END_MESSAGE_MAP()

void COptionsAdvanced::DoDataExchange(CDataExchange* pDX)
//-------------------------------------------------------
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CModTypeDlg)
	DDX_Control(pDX, IDC_LIST4,			m_List);
	//}}AFX_DATA_MAP
}


static CString FormatSetting(const SettingPath &path, const SettingValue &val)
//----------------------------------------------------------------------------
{
	return mpt::ToCString(path.FormatAsString() + MPT_USTRING(" = ") + val.FormatAsString());
}


BOOL COptionsAdvanced::PreTranslateMessage(MSG *msg)
//--------------------------------------------------
{
	if(msg->message == WM_KEYDOWN && msg->wParam == VK_RETURN)
	{
		OnOptionDblClick();
		return TRUE;
	}
	return FALSE;
}


BOOL COptionsAdvanced::OnInitDialog()
//-----------------------------------
{
	CPropertyPage::OnInitDialog();
	ReInit();
	return TRUE;
}


void COptionsAdvanced::ReInit()
//-----------------------------
{
	m_List.SetRedraw(FALSE);
	m_List.ResetContent();
	m_IndexToPath.clear();
	CString findStr;
	GetDlgItemText(IDC_EDIT1, findStr);
	findStr.MakeLower();
	for(SettingsContainer::SettingsMap::const_iterator it = theApp.GetSettings().begin(); it != theApp.GetSettings().end(); ++it)
	{
		CString str = FormatSetting(it->first, it->second);
		bool addString = true;
		if(!findStr.IsEmpty())
		{
			CString strLower = str;
			addString = strLower.MakeLower().Find(findStr) >= 0;
		}
		if(addString)
		{
			int index = m_List.AddString(str);
			m_IndexToPath[index] = it->first;
		}
	}
	m_List.SetRedraw(TRUE);
	m_List.Invalidate(FALSE);
}


void COptionsAdvanced::OnOK()
//---------------------------
{
	CPropertyPage::OnOK();
}


BOOL COptionsAdvanced::OnSetActive()
//----------------------------------
{
	ReInit();
	CMainFrame::m_nLastOptionsPage = OPTIONS_PAGE_ADVANCED;
	return CPropertyPage::OnSetActive();
}


void COptionsAdvanced::OnOptionDblClick()
//---------------------------------------
{
	const int index = m_List.GetCurSel();
	if(m_IndexToPath.find(index) == m_IndexToPath.end())
	{
		return;
	}
	const SettingPath path = m_IndexToPath[index];
	SettingValue val = theApp.GetSettings().GetMap().find(path)->second;
	if(val.GetType() == SettingTypeBool)
	{
		val = !val.as<bool>();
	} else
	{
		CInputDlg inputDlg(this, mpt::ToCString(path.FormatAsString()), mpt::ToCString(val.FormatValueAsString()));
		if(inputDlg.DoModal() != IDOK)
		{
			return;
		}
		val.SetFromString(inputDlg.resultString);
	}
	theApp.GetSettings().Write(path, val);
	m_List.DeleteString(index);
	m_List.InsertString(index, FormatSetting(path, val));
	m_List.SetCurSel(index);
	OnSettingsChanged();
}

#endif // MPT_SETTINGS_CACHE


OPENMPT_NAMESPACE_END
