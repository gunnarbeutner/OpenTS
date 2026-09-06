/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The common controls of <commctrl.h>: trackbar, progress, hot key, tree view, tab, list view, and image list.

#pragma once

#include "windows.h"

#ifndef SNDMSG
#define SNDMSG	SendMessageA
#endif

// The common controls, from commctrl.h.
#define TBM_GETPOS			(WM_USER)
#define TBM_GETRANGEMIN		(WM_USER + 1)
#define TBM_GETRANGEMAX		(WM_USER + 2)
#define TBM_SETPOS			(WM_USER + 5)
#define TBM_SETRANGE		(WM_USER + 6)
#define TBM_SETRANGEMIN		(WM_USER + 7)
#define TBM_SETRANGEMAX		(WM_USER + 8)
#define TBM_SETTICFREQ		(WM_USER + 20)
#define TBM_SETPAGESIZE		(WM_USER + 21)
#define TBM_SETLINESIZE		(WM_USER + 23)

#define PBM_SETRANGE		(WM_USER + 1)
#define PBM_SETPOS			(WM_USER + 2)
#define PBM_DELTAPOS		(WM_USER + 3)
#define PBM_SETSTEP			(WM_USER + 4)
#define PBM_STEPIT			(WM_USER + 5)
#define PBM_SETRANGE32		(WM_USER + 6)

BOOL InitCommonControls(void);
#define HKM_SETHOTKEY		(WM_USER + 1)
#define HKM_GETHOTKEY		(WM_USER + 2)
#define HKM_SETRULES		(WM_USER + 3)
#define TRACKBAR_CLASS		"msctls_trackbar32"
#define PROGRESS_CLASS		"msctls_progress32"
#define WC_TREEVIEWA		"SysTreeView32"
#define WC_TREEVIEW			WC_TREEVIEWA
DECLARE_HANDLE(HTREEITEM);
DECLARE_HANDLE(HIMAGELIST);

typedef struct tagTVITEMA {
	UINT mask;
	HTREEITEM hItem;
	UINT state;
	UINT stateMask;
	LPSTR pszText;
	int cchTextMax;
	int iImage;
	int iSelectedImage;
	int cChildren;
	LPARAM lParam;
} TVITEMA, TV_ITEM, TVITEM, * LPTVITEMA;

typedef struct tagNMTREEVIEWA {
	NMHDR hdr;
	UINT action;
	TVITEMA itemOld;
	TVITEMA itemNew;
	POINT ptDrag;
} NMTREEVIEWA, NM_TREEVIEW, NMTREEVIEW, * LPNMTREEVIEWA, * LPNMTREEVIEW;

typedef struct tagTVHITTESTINFO {
	POINT pt;
	UINT flags;
	HTREEITEM hItem;
} TVHITTESTINFO, TV_HITTESTINFO, * LPTVHITTESTINFO;
#define TVGN_ROOT			0x0000
#define TVGN_NEXT			0x0001
#define TVGN_PREVIOUS		0x0002
#define TVGN_PARENT			0x0003
#define TVGN_CHILD			0x0004
#define TVGN_FIRSTVISIBLE	0x0005
#define TVGN_NEXTVISIBLE	0x0006
#define TVGN_CARET			0x0009
#define TVGN_DROPHILITE		0x0008
#define TVM_SELECTITEM		(0x1100 + 11)
#define TVM_GETNEXTITEM		(0x1100 + 10)
#define TVM_GETINDENT		(0x1100 + 6)
#define TVM_HITTEST			(0x1100 + 17)
#define TVM_GETITEMA		(0x1100 + 12)
#define TVM_SETITEMA		(0x1100 + 13)
#define TVM_EXPAND			(0x1100 + 2)
#define TVN_SELCHANGEDA		(0u - 402u)
#define TVN_BEGINDRAGA		(0u - 407u)

#define TreeView_SelectItem(hwnd, item) \
	((BOOL)SendMessageA((hwnd), TVM_SELECTITEM, TVGN_CARET, (LPARAM)(HTREEITEM)(item)))
#define TreeView_SelectDropTarget(hwnd, item) \
	((BOOL)SendMessageA((hwnd), TVM_SELECTITEM, TVGN_DROPHILITE, (LPARAM)(HTREEITEM)(item)))
#define TreeView_GetNextItem(hwnd, item, code) \
	((HTREEITEM)SendMessageA((hwnd), TVM_GETNEXTITEM, (WPARAM)(code), (LPARAM)(HTREEITEM)(item)))
#define TreeView_GetFirstVisible(hwnd)	TreeView_GetNextItem((hwnd), NULL, TVGN_FIRSTVISIBLE)
#define TreeView_GetSelection(hwnd)		TreeView_GetNextItem((hwnd), NULL, TVGN_CARET)
#define TreeView_GetParent(hwnd, item)	TreeView_GetNextItem((hwnd), (item), TVGN_PARENT)
#define TreeView_GetChild(hwnd, item)	TreeView_GetNextItem((hwnd), (item), TVGN_CHILD)
#define TreeView_GetNextSibling(hwnd, item)	TreeView_GetNextItem((hwnd), (item), TVGN_NEXT)
#define TreeView_GetIndent(hwnd)		((UINT)SendMessageA((hwnd), TVM_GETINDENT, 0, 0))
#define TreeView_HitTest(hwnd, info) \
	((HTREEITEM)SendMessageA((hwnd), TVM_HITTEST, 0, (LPARAM)(LPTVHITTESTINFO)(info)))
#define TreeView_GetItem(hwnd, item) \
	((BOOL)SendMessageA((hwnd), TVM_GETITEMA, 0, (LPARAM)(TVITEMA *)(item)))
#define TreeView_SetItem(hwnd, item) \
	((BOOL)SendMessageA((hwnd), TVM_SETITEMA, 0, (LPARAM)(TVITEMA const *)(item)))
#define TreeView_Expand(hwnd, item, code) \
	((BOOL)SendMessageA((hwnd), TVM_EXPAND, (WPARAM)(code), (LPARAM)(HTREEITEM)(item)))
HIMAGELIST ImageList_Create(int cx, int cy, UINT flags, int initial, int grow);
BOOL ImageList_Destroy(HIMAGELIST list);
BOOL ImageList_BeginDrag(HIMAGELIST list, int track, int hotspotx, int hotspoty);
void ImageList_EndDrag(void);
BOOL ImageList_DragEnter(HWND lock, int x, int y);
BOOL ImageList_DragLeave(HWND lock);
BOOL ImageList_DragMove(int x, int y);
BOOL ImageList_DragShowNolock(BOOL show);
#define WC_TABCONTROL		"SysTabControl32"
#define HOTKEY_CLASS		"msctls_hotkey32"
#define UPDOWN_CLASS		"msctls_updown32"
#define TVIF_TEXT			0x0001
#define TVIF_IMAGE			0x0002
#define TVIF_PARAM			0x0004
#define TVIF_STATE			0x0008
#define TVIF_HANDLE			0x0010
#define TVIF_SELECTEDIMAGE	0x0020
#define TVIF_CHILDREN		0x0040
#define TVE_COLLAPSE		0x0001
#define TVE_EXPAND			0x0002
#define TVE_TOGGLE			0x0003
#define TVM_CREATEDRAGIMAGE	(0x1100 + 18)
#define TVM_GETITEMRECT		(0x1100 + 4)
#define TVM_GETEDITCONTROL	(0x1100 + 15)
#define TVM_SELECTITEM_FIRSTVISIBLE	TVGN_FIRSTVISIBLE

typedef struct tagTVDISPINFOA {
	NMHDR hdr;
	TVITEMA item;
} NMTVDISPINFOA, NMTVDISPINFO, TV_DISPINFO, * LPNMTVDISPINFOA;

#define TreeView_CreateDragImage(hwnd, item) \
	((HIMAGELIST)SendMessageA((hwnd), TVM_CREATEDRAGIMAGE, 0, (LPARAM)(HTREEITEM)(item)))
#define TreeView_GetItemRect(hwnd, item, rect, code) \
	(*(HTREEITEM *)(rect) = (item), (BOOL)SendMessageA((hwnd), TVM_GETITEMRECT, (WPARAM)(BOOL)(code), (LPARAM)(RECT *)(rect)))
#define TreeView_GetPrevVisible(hwnd, item)	TreeView_GetNextItem((hwnd), (item), TVGN_PREVIOUS)
#define TreeView_GetNextVisible(hwnd, item)	TreeView_GetNextItem((hwnd), (item), TVGN_NEXTVISIBLE)
#define TreeView_SelectSetFirstVisible(hwnd, item) \
	((BOOL)SendMessageA((hwnd), TVM_SELECTITEM, TVGN_FIRSTVISIBLE, (LPARAM)(HTREEITEM)(item)))
#define TreeView_GetEditControl(hwnd) \
	((HWND)SendMessageA((hwnd), TVM_GETEDITCONTROL, 0, 0))
#define TCM_FIRST			0x1300
#define TCM_SETITEMSIZE		(TCM_FIRST + 41)
#define TCM_GETITEMCOUNT	(TCM_FIRST + 4)
#define TCM_GETCURSEL		(TCM_FIRST + 11)
#define TCM_SETCURSEL		(TCM_FIRST + 12)
#define TCM_ADJUSTRECT		(TCM_FIRST + 40)
#define TCM_SETPADDING		(TCM_FIRST + 43)

#define TreeView_GetRoot(hwnd)	TreeView_GetNextItem((hwnd), NULL, TVGN_ROOT)
#define WC_LISTVIEWA		"SysListView32"
#define WC_LISTVIEW			WC_LISTVIEWA
#define WC_TABCONTROLA		"SysTabControl32"

#define TabCtrl_GetItemCount(hwnd)		((int)SendMessageA((hwnd), TCM_GETITEMCOUNT, 0, 0))
#define TabCtrl_GetCurSel(hwnd)			((int)SendMessageA((hwnd), TCM_GETCURSEL, 0, 0))
#define TabCtrl_SetCurSel(hwnd, i)		((int)SendMessageA((hwnd), TCM_SETCURSEL, (WPARAM)(int)(i), 0))
#define TabCtrl_SetItemSize(hwnd, x, y)	((DWORD)SendMessageA((hwnd), TCM_SETITEMSIZE, 0, MAKELPARAM((x), (y))))
#define TabCtrl_AdjustRect(hwnd, larger, r) \
	((void)SendMessageA((hwnd), TCM_ADJUSTRECT, (WPARAM)(BOOL)(larger), (LPARAM)(RECT *)(r)))
#define TabCtrl_SetPadding(hwnd, cx, cy) \
	((void)SendMessageA((hwnd), TCM_SETPADDING, 0, MAKELPARAM((cx), (cy))))
#define TCM_GETITEMRECT		(TCM_FIRST + 10)
#define TabCtrl_GetItemRect(hwnd, i, rect) \
	((BOOL)SendMessageA((hwnd), TCM_GETITEMRECT, (WPARAM)(int)(i), (LPARAM)(RECT *)(rect)))

#define LVM_FIRST				0x1000
#define LVM_GETCOLUMNWIDTH		(LVM_FIRST + 29)
#define LVM_SETCOLUMNWIDTH		(LVM_FIRST + 30)
#define LVM_GETITEMCOUNT		(LVM_FIRST + 4)
#define LVM_DELETEALLITEMS		(LVM_FIRST + 9)
#define LVM_GETNEXTITEM			(LVM_FIRST + 12)
#define LVSCW_AUTOSIZE			(-1)
#define LVSCW_AUTOSIZE_USEHEADER (-2)

#define ListView_GetColumnWidth(hwnd, i) \
	((int)SendMessageA((hwnd), LVM_GETCOLUMNWIDTH, (WPARAM)(int)(i), 0))
#define ListView_SetColumnWidth(hwnd, i, cx) \
	((BOOL)SendMessageA((hwnd), LVM_SETCOLUMNWIDTH, (WPARAM)(int)(i), MAKELPARAM((cx), 0)))
#define ListView_GetItemCount(hwnd) \
	((int)SendMessageA((hwnd), LVM_GETITEMCOUNT, 0, 0))
#define ListView_DeleteAllItems(hwnd) \
	((BOOL)SendMessageA((hwnd), LVM_DELETEALLITEMS, 0, 0))
#define TVIS_SELECTED		0x0002
#define TVIS_CUT			0x0004
#define TVIS_DROPHILITED	0x0008
#define TVIS_BOLD			0x0010
#define TVIS_EXPANDED		0x0020
#define TVIS_EXPANDEDONCE	0x0040
#define TVIS_STATEIMAGEMASK	0xF000

#define TCIF_TEXT			0x0001
#define TCIF_IMAGE			0x0002
#define TCIF_PARAM			0x0008

typedef struct tagTCITEMA {
	UINT mask;
	DWORD dwState;
	DWORD dwStateMask;
	LPSTR pszText;
	int cchTextMax;
	int iImage;
	LPARAM lParam;
} TCITEMA, TC_ITEM, TCITEM, * LPTCITEMA;

#define TCM_GETITEMA		(TCM_FIRST + 5)
#define TCM_SETITEMA		(TCM_FIRST + 6)
#define TCM_INSERTITEMA		(TCM_FIRST + 7)
#define TabCtrl_GetItem(hwnd, i, item) \
	((BOOL)SendMessageA((hwnd), TCM_GETITEMA, (WPARAM)(int)(i), (LPARAM)(TC_ITEM *)(item)))
#define TabCtrl_SetItem(hwnd, i, item) \
	((BOOL)SendMessageA((hwnd), TCM_SETITEMA, (WPARAM)(int)(i), (LPARAM)(TC_ITEM *)(item)))
#define TabCtrl_InsertItem(hwnd, i, item) \
	((int)SendMessageA((hwnd), TCM_INSERTITEMA, (WPARAM)(int)(i), (LPARAM)(TC_ITEM const *)(item)))
#define TB_LINEUP			0
#define TB_LINEDOWN			1
#define TB_PAGEUP			2
#define TB_PAGEDOWN			3
#define TB_THUMBPOSITION	4
#define TB_THUMBTRACK		5
#define TB_TOP				6
#define TB_BOTTOM			7
#define TB_ENDTRACK			8
