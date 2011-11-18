/*
 * MIDIMacroDialog.h
 * -----------------
 * Purpose: Header file for MIDI macro configuration dialog implementation
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 */

#pragma once
#ifndef MIDIMACRODIALOG_H
#define MIDIMACRODIALOG_H
//class CColourEdit;
#include "ColourEdit.h"
#include "../soundlib/MIDIMacros.h"


//===================================
class CMidiMacroSetup: public CDialog
//===================================
{
public:
	CMidiMacroSetup(CSoundFile &sndFile, CWnd *parent = NULL) : CDialog(IDD_MIDIMACRO, parent), m_SndFile(sndFile), macroTools(sndFile), m_MidiCfg(sndFile.m_MidiCfg)
	{
		m_bEmbed = (m_SndFile.m_dwSongFlags & SONG_EMBEDMIDICFG) != 0;
	}

	bool m_bEmbed;
	MODMIDICFG m_MidiCfg;


protected:
	CComboBox m_CbnSFx, m_CbnSFxPreset, m_CbnZxx, m_CbnZxxPreset, m_CbnMacroPlug, m_CbnMacroParam, m_CbnMacroCC;
	CEdit m_EditSFx, m_EditZxx;
	CColourEdit m_EditMacroValue[NUM_MACROS], m_EditMacroType[NUM_MACROS]; //rewbs.macroGUI
	CButton m_EditMacro[NUM_MACROS], m_BtnMacroShowAll[NUM_MACROS];

	CSoundFile &m_SndFile;
	MIDIMacroTools macroTools;

	bool ValidateMacroString(CEdit &wnd, char *lastMacro, bool isParametric);

	void UpdateMacroList(int macro=-1);
	void ToggleBoxes(UINT preset, UINT sfx);
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange* pDX);
	afx_msg void UpdateDialog();
	afx_msg void OnSetAsDefault();
	afx_msg void OnResetCfg();
	afx_msg void OnMacroHelp();
	afx_msg void OnEmbedMidiCfg();
	afx_msg void OnSFxChanged();
	afx_msg void OnSFxPresetChanged();
	afx_msg void OnZxxPresetChanged();
	afx_msg void OnSFxEditChanged();
	afx_msg void OnZxxEditChanged();
	afx_msg void OnPlugChanged();
	afx_msg void OnPlugParamChanged();
	afx_msg void OnCCChanged();

	afx_msg void OnViewAllParams(UINT id);
	afx_msg void OnSetSFx(UINT id);
	DECLARE_MESSAGE_MAP()
};

#endif // MIDIMACRODIALOG_H
