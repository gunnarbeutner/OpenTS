/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The control message wrappers and parameter crackers of <windowsx.h>, macros over SendMessage as under Windows.

#pragma once

#include "windows.h"

#ifndef SNDMSG
#define SNDMSG	SendMessageA
#endif

#define GET_X_LPARAM(lp)	((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp)	((int)(short)HIWORD(lp))
#define Button_GetCheck(hwnd)			((int)SendMessageA((hwnd), BM_GETCHECK, 0, 0))
#define Button_SetCheck(hwnd, check)	((void)SendMessageA((hwnd), BM_SETCHECK, (WPARAM)(int)(check), 0))
#define Button_GetState(hwnd)			((int)SendMessageA((hwnd), BM_GETSTATE, 0, 0))
#define Button_SetState(hwnd, state)	((void)SendMessageA((hwnd), BM_SETSTATE, (WPARAM)(BOOL)(state), 0))
#define Button_Enable(hwnd, enable)		EnableWindow((hwnd), (enable))
#define Button_SetText(hwnd, text)		((void)SetWindowTextA((hwnd), (text)))

#define Static_SetText(hwnd, text)		((void)SetWindowTextA((hwnd), (text)))
#define Static_GetText(hwnd, text, n)	GetWindowTextA((hwnd), (text), (n))

#define Edit_SetText(hwnd, text)		((void)SetWindowTextA((hwnd), (text)))
#define Edit_GetText(hwnd, text, n)		GetWindowTextA((hwnd), (text), (n))
#define Edit_SetSel(hwnd, start, end)	((void)SendMessageA((hwnd), EM_SETSEL, (WPARAM)(int)(start), (LPARAM)(int)(end)))
#define Edit_LimitText(hwnd, limit)		((void)SendMessageA((hwnd), EM_LIMITTEXT, (WPARAM)(limit), 0))
#define Edit_ReplaceSel(hwnd, text)		((void)SendMessageA((hwnd), EM_REPLACESEL, 0, (LPARAM)(LPCSTR)(text)))

#define ListBox_AddString(hwnd, text)			((int)SendMessageA((hwnd), LB_ADDSTRING, 0, (LPARAM)(LPCSTR)(text)))
#define ListBox_InsertString(hwnd, i, text)		((int)SendMessageA((hwnd), LB_INSERTSTRING, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(text)))
#define ListBox_DeleteString(hwnd, i)			((int)SendMessageA((hwnd), LB_DELETESTRING, (WPARAM)(int)(i), 0))
#define ListBox_ResetContent(hwnd)				((BOOL)SendMessageA((hwnd), LB_RESETCONTENT, 0, 0))
#define ListBox_GetCount(hwnd)					((int)SendMessageA((hwnd), LB_GETCOUNT, 0, 0))
#define ListBox_GetCurSel(hwnd)					((int)SendMessageA((hwnd), LB_GETCURSEL, 0, 0))
#define ListBox_SetCurSel(hwnd, i)				((int)SendMessageA((hwnd), LB_SETCURSEL, (WPARAM)(int)(i), 0))
#define ListBox_GetText(hwnd, i, text)			((int)SendMessageA((hwnd), LB_GETTEXT, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(text)))
#define ListBox_GetTextLen(hwnd, i)				((int)SendMessageA((hwnd), LB_GETTEXTLEN, (WPARAM)(int)(i), 0))
#define ListBox_GetItemData(hwnd, i)			((LRESULT)SendMessageA((hwnd), LB_GETITEMDATA, (WPARAM)(int)(i), 0))
#define ListBox_SetItemData(hwnd, i, data)		((int)SendMessageA((hwnd), LB_SETITEMDATA, (WPARAM)(int)(i), (LPARAM)(data)))
#define ListBox_GetTopIndex(hwnd)				((int)SendMessageA((hwnd), LB_GETTOPINDEX, 0, 0))
#define ListBox_SetTopIndex(hwnd, i)			((int)SendMessageA((hwnd), LB_SETTOPINDEX, (WPARAM)(int)(i), 0))
#define ListBox_SetSel(hwnd, select, i)			((int)SendMessageA((hwnd), LB_SETSEL, (WPARAM)(BOOL)(select), (LPARAM)(int)(i)))
#define ListBox_GetSel(hwnd, i)					((int)SendMessageA((hwnd), LB_GETSEL, (WPARAM)(int)(i), 0))
#define ListBox_FindString(hwnd, i, text)		((int)SendMessageA((hwnd), LB_FINDSTRING, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(text)))
#define ListBox_SelectString(hwnd, i, text)		((int)SendMessageA((hwnd), LB_SELECTSTRING, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(text)))

#define ComboBox_AddString(hwnd, text)			((int)SendMessageA((hwnd), CB_ADDSTRING, 0, (LPARAM)(LPCSTR)(text)))
#define ComboBox_ResetContent(hwnd)				((int)SendMessageA((hwnd), CB_RESETCONTENT, 0, 0))
#define ComboBox_GetCurSel(hwnd)				((int)SendMessageA((hwnd), CB_GETCURSEL, 0, 0))
#define ComboBox_SetCurSel(hwnd, i)				((int)SendMessageA((hwnd), CB_SETCURSEL, (WPARAM)(int)(i), 0))
#define ComboBox_GetItemData(hwnd, i)			((LRESULT)SendMessageA((hwnd), CB_GETITEMDATA, (WPARAM)(int)(i), 0))
#define ComboBox_SetItemData(hwnd, i, data)		((int)SendMessageA((hwnd), CB_SETITEMDATA, (WPARAM)(int)(i), (LPARAM)(data)))
#define ComboBox_GetCount(hwnd)					((int)SendMessageA((hwnd), CB_GETCOUNT, 0, 0))
#define ComboBox_DeleteString(hwnd, i)			((int)SendMessageA((hwnd), CB_DELETESTRING, (WPARAM)(int)(i), 0))
#define ComboBox_InsertString(hwnd, i, text)	((int)SendMessageA((hwnd), CB_INSERTSTRING, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(text)))
#define ComboBox_FindString(hwnd, i, text)		((int)SendMessageA((hwnd), CB_FINDSTRING, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(text)))
#define ComboBox_GetLBText(hwnd, i, text)		((int)SendMessageA((hwnd), CB_GETLBTEXT, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(text)))
#define ComboBox_GetLBTextLen(hwnd, i)			((int)SendMessageA((hwnd), CB_GETLBTEXTLEN, (WPARAM)(int)(i), 0))
#define ComboBox_GetDroppedState(hwnd)			((BOOL)SendMessageA((hwnd), CB_GETDROPPEDSTATE, 0, 0))
#define ComboBox_GetDroppedControlRect(hwnd, r)	((void)SendMessageA((hwnd), CB_GETDROPPEDCONTROLRECT, 0, (LPARAM)(RECT *)(r)))
#define ComboBox_ShowDropdown(hwnd, show)		((BOOL)SendMessageA((hwnd), CB_SHOWDROPDOWN, (WPARAM)(BOOL)(show), 0))
#define ComboBox_SetItemHeight(hwnd, i, cy)		((int)SendMessageA((hwnd), CB_SETITEMHEIGHT, (WPARAM)(int)(i), (LPARAM)(int)(cy)))
#define ComboBox_GetItemHeight(hwnd, i)			((int)SendMessageA((hwnd), CB_GETITEMHEIGHT, (WPARAM)(int)(i), 0))
#define ListBox_FindStringExact(hwnd, i, text) \
	((int)SendMessageA((hwnd), LB_FINDSTRINGEXACT, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(text)))
#define ListBox_GetItemRect(hwnd, i, rect) \
	((int)SendMessageA((hwnd), LB_GETITEMRECT, (WPARAM)(int)(i), (LPARAM)(RECT *)(rect)))
