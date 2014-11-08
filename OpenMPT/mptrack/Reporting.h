/*
 * Reporting.h
 * -----------
 * Purpose: A class for showing notifications, prompts, etc...
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#pragma once


OPENMPT_NAMESPACE_BEGIN


enum ConfirmAnswer
{
	cnfYes,
	cnfNo,
	cnfCancel,
};


enum RetryAnswer
{
	rtyRetry,
	rtyCancel,
};


//=============
class Reporting
//=============
{

public:

	// TODO Quick'n'dirty workaround for MsgBox(). Shouldn't be public.
	static UINT CustomNotification(const AnyStringLocale &text, const AnyStringLocale &caption, UINT flags, const CWnd *parent);

	// Show a simple notification
	static void Notification(const AnyStringLocale &text, const CWnd *parent = nullptr);
	static void Notification(const AnyStringLocale &text, const AnyStringLocale &caption, const CWnd *parent = nullptr);

	// Show a simple information
	static void Information(const AnyStringLocale &text, const CWnd *parent = nullptr);
	static void Information(const AnyStringLocale &text, const AnyStringLocale &caption, const CWnd *parent = nullptr);

	// Show a simple warning
	static void Warning(const AnyStringLocale &text, const CWnd *parent = nullptr);
	static void Warning(const AnyStringLocale &text, const AnyStringLocale &caption, const CWnd *parent = nullptr);

	// Show an error box.
	static void Error(const AnyStringLocale &text, const CWnd *parent = nullptr);
	static void Error(const AnyStringLocale &text, const AnyStringLocale &caption, const CWnd *parent = nullptr);

	// Show a confirmation dialog.
	static ConfirmAnswer Confirm(const AnyStringLocale &text, bool showCancel = false, bool defaultNo = false, const CWnd *parent = nullptr);
	static ConfirmAnswer Confirm(const AnyStringLocale &text, const AnyStringLocale &caption, bool showCancel = false, bool defaultNo = false, const CWnd *parent = nullptr);

	// Show a confirmation dialog.
	static RetryAnswer RetryCancel(const AnyStringLocale &text, const CWnd *parent = nullptr);
	static RetryAnswer RetryCancel(const AnyStringLocale &text, const AnyStringLocale &caption, const CWnd *parent = nullptr);

};


OPENMPT_NAMESPACE_END
