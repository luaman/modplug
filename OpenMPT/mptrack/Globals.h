/*
 * globals.h
 * ---------
 * Purpose: Implementation of various views of the tracker interface.
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#pragma once

OPENMPT_NAMESPACE_BEGIN

#ifndef WM_HELPHITTEST
#define WM_HELPHITTEST		0x366
#endif

#ifndef HID_BASE_COMMAND
#define HID_BASE_COMMAND	0x10000
#endif

#define ID_EDIT_MIXPASTE ID_EDIT_PASTE_SPECIAL		//rewbs.mixPaste

class CModControlView;
class CModControlBar;

//=======================================
class CModControlBar: public CToolBarCtrl
//=======================================
{
public:
	BOOL Init(CImageList &icons, CImageList &disabledIcons);
	void UpdateStyle();
	BOOL AddButton(UINT nID, int iImage=0, UINT nStyle=TBSTYLE_BUTTON, UINT nState=TBSTATE_ENABLED);
	afx_msg LRESULT OnHelpHitTest(WPARAM, LPARAM);
	DECLARE_MESSAGE_MAP()
};


//==================================
class CModControlDlg: public CDialog
//==================================
{
protected:
	CModDoc &m_modDoc;
	CSoundFile &m_sndFile;
	CModControlView &m_parent;
	HWND m_hWndView;
	LONG m_nLockCount;
	BOOL m_bInitialized;

public:
	CModControlDlg(CModControlView &parent, CModDoc &document);
	virtual ~CModControlDlg();
	
public:
	void SetViewWnd(HWND hwndView) { m_hWndView = hwndView; }
	HWND GetViewWnd() const { return m_hWndView; }
	LRESULT SendViewMessage(UINT uMsg, LPARAM lParam=0) const;
	BOOL PostViewMessage(UINT uMsg, LPARAM lParam=0) const;
	LRESULT SwitchToView() const { return SendViewMessage(VIEWMSG_SETACTIVE); }
	void LockControls() { m_nLockCount++; }
	void UnlockControls() { PostMessage(WM_MOD_UNLOCKCONTROLS); }
	bool IsLocked() const { return (m_nLockCount > 0); }
	virtual Setting<LONG>* GetSplitPosRef() = 0; 	//rewbs.varWindowSize

protected:
	//{{AFX_VIRTUAL(CModControlDlg)
	public:
	
	afx_msg void OnEditCut() { if (m_hWndView) ::SendMessage(m_hWndView, WM_COMMAND, ID_EDIT_CUT, 0); }			//rewbs.customKeys
	afx_msg void OnEditCopy() { if (m_hWndView) ::SendMessage(m_hWndView, WM_COMMAND, ID_EDIT_COPY, 0); }		//rewbs.customKeys
	afx_msg void OnEditPaste() { if (m_hWndView) ::SendMessage(m_hWndView, WM_COMMAND, ID_EDIT_PASTE, 0); }		//rewbs.customKeys
	afx_msg void OnEditMixPaste() { if (m_hWndView) ::SendMessage(m_hWndView, WM_COMMAND, ID_EDIT_MIXPASTE, 0); }		//rewbs.mixPaste
	afx_msg void OnEditMixPasteITStyle() { if (m_hWndView) ::SendMessage(m_hWndView, WM_COMMAND, ID_EDIT_MIXPASTE_ITSTYLE, 0); }
	afx_msg void OnEditPasteFlood() { if (m_hWndView) ::SendMessage(m_hWndView, WM_COMMAND, ID_EDIT_PASTEFLOOD, 0); }
	afx_msg void OnEditPushForwardPaste() { if (m_hWndView) ::SendMessage(m_hWndView, WM_COMMAND, ID_EDIT_PUSHFORWARDPASTE, 0); }
	afx_msg void OnEditFind() { if (m_hWndView) ::SendMessage(m_hWndView, WM_COMMAND, ID_EDIT_FIND, 0); }		//rewbs.customKeys
	afx_msg void OnEditFindNext() { if (m_hWndView) ::SendMessage(m_hWndView, WM_COMMAND, ID_EDIT_FINDNEXT, 0); }	  //rewbs.customKeys
	afx_msg void OnSwitchToView() { if (m_hWndView) ::PostMessage(m_hWndView, WM_MOD_VIEWMSG, VIEWMSG_SETFOCUS, 0); } //rewbs.customKeys

	virtual void OnOK() {}
	virtual void OnCancel() {}
	virtual void RecalcLayout() {}
	virtual void UpdateView(DWORD, CObject *) {}
	virtual CRuntimeClass *GetAssociatedViewClass() { return NULL; }
	virtual LRESULT OnModCtrlMsg(WPARAM wParam, LPARAM lParam);
	virtual void OnActivatePage(LPARAM) {}
	virtual void OnDeactivatePage() {}
	virtual BOOL OnInitDialog();
	virtual INT_PTR OnToolHitTest( CPoint point, TOOLINFO* pTI ) const;
	virtual BOOL GetToolTipText(UINT, LPSTR) { return FALSE; }
	//}}AFX_VIRTUAL
	//{{AFX_MSG(CModControlDlg)
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg LRESULT OnUnlockControls(WPARAM, LPARAM) { if (m_nLockCount > 0) m_nLockCount--; return 0; }
	afx_msg BOOL OnToolTipText(UINT, NMHDR* pNMHDR, LRESULT* pResult);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};


//================================
class CModTabCtrl: public CTabCtrl
//================================
{
public:
	BOOL InsertItem(int nIndex, LPSTR pszText, LPARAM lParam=0, int iImage=-1);
	BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
	LPARAM GetItemData(int nIndex);
};


//=================================
class CModControlView: public CView
//=================================
{
protected:
	enum { MAX_PAGES=6 };		//rewbs.graph: 5 to 6

protected:
	CModTabCtrl m_TabCtrl;
	CModControlDlg *m_Pages[MAX_PAGES];
	int m_nActiveDlg, m_nInstrumentChanged;
	HWND m_hWndView, m_hWndMDI;

protected: // create from serialization only
	CModControlView();
	DECLARE_DYNCREATE(CModControlView)

public:
	virtual ~CModControlView() {}
	CModDoc* GetDocument() const { return (CModDoc *)m_pDocument; }
	void InstrumentChanged(int nInstr=-1) { m_nInstrumentChanged = nInstr; }
	int GetInstrumentChange() const { return m_nInstrumentChanged; }
	void SetMDIParentFrame(HWND hwnd) { m_hWndMDI = hwnd; }
	void ForceRefresh();
	CModControlDlg *GetCurrentControlDlg() { return m_Pages[m_nActiveDlg]; }

protected:
	void RecalcLayout();
	void UpdateView(DWORD dwHintMask=0, CObject *pHint=NULL);
	BOOL SetActivePage(int nIndex=-1, LPARAM lParam=-1);
	int GetActivePage();

	//{{AFX_VIRTUAL(CModControlView)
	public:
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	protected:
	virtual void OnInitialUpdate(); // called first time after construct
	virtual void OnDraw(CDC *) {}
	virtual void OnUpdate(CView* pSender, LPARAM lHint, CObject* pHint);
	//}}AFX_VIRTUAL

protected:
	//{{AFX_MSG(CModControlView)
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
	afx_msg void OnTabSelchange(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnEditCut() { if (m_hWndView) ::SendMessage(m_hWndView, WM_COMMAND, ID_EDIT_CUT, 0); }
	afx_msg void OnEditCopy() { if (m_hWndView) ::SendMessage(m_hWndView, WM_COMMAND, ID_EDIT_COPY, 0); }
	afx_msg void OnEditPaste() { if (m_hWndView) ::SendMessage(m_hWndView, WM_COMMAND, ID_EDIT_PASTE, 0); }
	afx_msg void OnEditMixPaste() { if (m_hWndView) ::SendMessage(m_hWndView, WM_COMMAND, ID_EDIT_MIXPASTE, 0); }		//rewbs.mixPaste
	afx_msg void OnEditMixPasteITStyle() { if (m_hWndView) ::SendMessage(m_hWndView, WM_COMMAND, ID_EDIT_MIXPASTE_ITSTYLE, 0); }
	afx_msg void OnEditFind() { if (m_hWndView) ::SendMessage(m_hWndView, WM_COMMAND, ID_EDIT_FIND, 0); }
	afx_msg void OnEditFindNext() { if (m_hWndView) ::SendMessage(m_hWndView, WM_COMMAND, ID_EDIT_FINDNEXT, 0); }
	afx_msg void OnSwitchToView() { if (m_hWndView) ::PostMessage(m_hWndView, WM_MOD_VIEWMSG, VIEWMSG_SETFOCUS, 0); }
	afx_msg LRESULT OnActivateModView(WPARAM, LPARAM);
	afx_msg LRESULT OnModCtrlMsg(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnGetToolTipText(WPARAM, LPARAM);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

// Non-client button attributes
#define NCBTNS_MOUSEOVER		0x01
#define NCBTNS_CHECKED			0x02
#define NCBTNS_DISABLED			0x04
#define NCBTNS_PUSHED			0x08


//======================================
class CModScrollView: public CScrollView
//======================================
{
protected:
	HWND m_hWndCtrl;
	int m_nScrollPosX, m_nScrollPosY;

public:
	DECLARE_SERIAL(CModScrollView)
	CModScrollView() : m_hWndCtrl(nullptr), m_nScrollPosX(0), m_nScrollPosY(0) { }
	virtual ~CModScrollView() {}

public:
	CModDoc* GetDocument() const { return (CModDoc *)m_pDocument; }
	LRESULT SendCtrlMessage(UINT uMsg, LPARAM lParam=0) const;
	BOOL PostCtrlMessage(UINT uMsg, LPARAM lParam=0) const;
	void UpdateIndicator(LPCSTR lpszText=NULL);

public:
	//{{AFX_VIRTUAL(CModScrollView)
	virtual void OnDraw(CDC *) {}
	virtual void OnPrepareDC(CDC*, CPrintInfo*) {}
	virtual void OnUpdate(CView* pSender, LPARAM lHint, CObject* pHint);
	virtual void UpdateView(DWORD, CObject *) {}
	virtual LRESULT OnModViewMsg(WPARAM wParam, LPARAM lParam);
	virtual BOOL OnDragonDrop(BOOL, const DRAGONDROP *) { return FALSE; }
	virtual LRESULT OnPlayerNotify(Notification *) { return 0; }
	//}}AFX_VIRTUAL

	CModControlDlg *GetControlDlg() { return static_cast<CModControlView *>(CWnd::FromHandle(m_hWndCtrl))->GetCurrentControlDlg(); }

protected:
	//{{AFX_MSG(CModScrollView)
	afx_msg void OnDestroy();
	afx_msg LRESULT OnReceiveModViewMsg(WPARAM wParam, LPARAM lParam);
	afx_msg BOOL OnMouseWheel(UINT fFlags, short zDelta, CPoint point);
	afx_msg LRESULT OnDragonDropping(WPARAM bDoDrop, LPARAM lParam) { return OnDragonDrop((BOOL)bDoDrop, (const DRAGONDROP *)lParam); }
	LRESULT OnUpdatePosition(WPARAM, LPARAM);

	// Fixes for 16-bit limitation in MFC's CScrollView
	virtual BOOL OnScroll(UINT nScrollCode, UINT nPos, BOOL bDoScroll = TRUE);
	virtual BOOL OnScrollBy(CSize sizeScroll, BOOL bDoScroll = TRUE);
	virtual int SetScrollPos(int nBar, int nPos, BOOL bRedraw = TRUE);
	virtual void SetScrollSizes(int nMapMode, SIZE sizeTotal, const SIZE& sizePage = CScrollView::sizeDefault, const SIZE& sizeLine = CScrollView::sizeDefault);


	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.


OPENMPT_NAMESPACE_END
