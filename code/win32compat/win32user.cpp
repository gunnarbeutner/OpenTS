/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Windows, a hierarchy, and message delivery for the WebAssembly build; nothing
// here draws. Hit tests take child rectangles literally, as Windows does, and
// msgroute.cpp corrects the frame-to-canvas mismatch the engine's layout has.

#include "always.h"
#include "substitute.h"
#include "commctrl.h"

#include "win32user.h"

#if defined(OPENTS_WIN32_SUBSTITUTE)

#include "browser.h"
#include "data.h"
#include "dbgprint.h"
#include "keyboard.h"
#include "phase.h"
#include "vidscale.h"
#include "win32ctrl.h"


#include <cstring>
#include <deque>
#include <string>
#include <vector>


// A queue this deep is not being drained, so overflow is reported, not grown.
static unsigned int const MESSAGE_QUEUE_LIMIT = 512;

// DWLP_MSGRESULT, DWLP_DLGPROC and DWLP_USER are addressed as window extra, and
// the engine writes DWLP_USER on windows whose class declared none.
static int const MINIMUM_WINDOW_EXTRA = 32;

// win32compat.h declares neither WM_HOTKEY nor RDW_UPDATENOW.
static UINT const WM_HOTKEY_MESSAGE = 0x0312;
static UINT const RDW_UPDATENOW_FLAG = 0x0100;


struct UserClass
{
	std::string Name;
	UINT Style;
	WNDPROC Procedure;
	int ClassExtra;
	int WindowExtra;
	HINSTANCE Instance;
	HICON Icon;
	HCURSOR Cursor;
	HBRUSH Background;
	ATOM Atom;
};


struct UserWindow
{
	UserClass * Class;
	WNDPROC Procedure;
	HINSTANCE Instance;
	int ID;
	DWORD Style;
	DWORD ExStyle;
	RECT Rect;
	std::string Text;
	LONG_PTR UserData;
	std::vector<unsigned char> Extra;
	bool Visible;
	bool Enabled;
	bool NeedsPaint;
	bool Painting;
	RECT UpdateRect;
	UserWindow * Parent;

	// Front to back; a hit test stops at the first sibling owning the point.
	std::vector<UserWindow *> Children;
};


struct UserHotKey
{
	UserWindow * Window;
	int ID;
	UINT Modifiers;
	UINT Key;
};


// A taken tick pushes Due forward, so a slow pump sees one tick, not a backlog.
struct UserTimer
{
	UserWindow * Window;
	UINT_PTR ID;
	UINT Elapse;
	DWORD Due;
};


static std::vector<UserClass *> _Classes;
static std::vector<UserWindow *> _Windows;
static std::vector<UserWindow *> _TopLevel;
static std::vector<UserHotKey> _HotKeys;
static std::vector<UserTimer> _Timers;
// The modifiers held when the page reported the event; the engine reads them
// when it acts on the message, which on a page is a later moment.
struct QueuedMessage
{
	MSG Message;
	unsigned short Modifiers;
};

static std::deque<QueuedMessage> _Queue;

static UserWindow * _Focus = nullptr;
static UserWindow * _Active = nullptr;
static UserWindow * _Foreground = nullptr;

static ATOM _NextAtom = 0xC000;

static bool _QueueReported = false;

// Repeats of one generated paint before the window is taken never to validate.
static unsigned int const PAINT_REPEAT_LIMIT = 64;

static UserWindow * _PaintWindow = nullptr;
static unsigned int _PaintRepeats = 0;

// The page's last report, in window pixels, so that a change becomes a message.
static POINT _LastMouse = { -1, -1 };
static unsigned char _LastKeyDown[256];
static bool _InputStarted = false;

static void Update_Text_Input(void);


//------------------------------------------------------------------------------
// Handles and lookups.
//------------------------------------------------------------------------------


static HWND Handle_Of(UserWindow * window)
{
	return((HWND)window);
}


// A handle held past its window's destruction is checked, never dereferenced.
static UserWindow * Window_Of(HWND handle)
{
	if (handle == nullptr) {
		return(nullptr);
	}

	for (unsigned int index = 0; index < _Windows.size(); index++) {
		if (_Windows[index] == (UserWindow *)handle) {
			return(_Windows[index]);
		}
	}

	return(nullptr);
}


static bool Names_Match(char const * left, char const * right)
{
	if (left == nullptr || right == nullptr) {
		return(false);
	}

	while (*left != '\0' && *right != '\0') {
		char a = *left;
		char b = *right;
		if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
		if (a != b) return(false);
		left++;
		right++;
	}

	return(*left == *right);
}


static UserClass * Class_Of(LPCSTR name)
{
	if (name == nullptr) {
		return(nullptr);
	}

	// A wasm module's literals sit inside the range an atom occupies, so a
	// small value is tried as an atom first and then as a string.
	if ((ULONG_PTR)name <= 0xFFFF) {
		ATOM atom = (ATOM)(ULONG_PTR)name;
		for (unsigned int index = 0; index < _Classes.size(); index++) {
			if (_Classes[index]->Atom == atom) {
				return(_Classes[index]);
			}
		}
	}

	for (unsigned int index = 0; index < _Classes.size(); index++) {
		if (Names_Match(_Classes[index]->Name.c_str(), name)) {
			return(_Classes[index]);
		}
	}

	return(nullptr);
}


//------------------------------------------------------------------------------
// Geometry.
//------------------------------------------------------------------------------


static POINT Screen_Origin(UserWindow const * window)
{
	POINT origin;
	origin.x = 0;
	origin.y = 0;

	for (UserWindow const * walk = window; walk != nullptr; walk = walk->Parent) {
		origin.x += walk->Rect.left;
		origin.y += walk->Rect.top;
	}

	return(origin);
}


bool Win32_User_Window_Rect(HWND window, RECT * rect)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr || rect == nullptr) {
		return(false);
	}

	POINT origin = Screen_Origin(entry);

	rect->left = origin.x;
	rect->top = origin.y;
	rect->right = origin.x + (entry->Rect.right - entry->Rect.left);
	rect->bottom = origin.y + (entry->Rect.bottom - entry->Rect.top);
	return(true);
}


static void Append_JSON_String(std::string & out, std::string const & text)
{
	out += '"';
	for (char character : text) {
		switch (character) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if ((unsigned char)character < 0x20) {
					char escaped[8];
					snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned char)character);
					out += escaped;
				} else {
					out += character;
				}
				break;
		}
	}
	out += '"';
}


static void Describe_Window(std::string & out, UserWindow const * window, int depth, bool & first)
{
	if (!window->Visible) {
		return;
	}

	POINT origin = Screen_Origin(window);
	POINT corner;
	corner.x = origin.x + (window->Rect.right - window->Rect.left);
	corner.y = origin.y + (window->Rect.bottom - window->Rect.top);
	Window_Point_To_Game(origin);
	Window_Point_To_Game(corner);

	char buffer[160];
	snprintf(buffer, sizeof(buffer),
		"{\"id\":%d,\"depth\":%d,\"rect\":[%ld,%ld,%ld,%ld],\"enabled\":%s,\"focus\":%s,\"class\":",
		window->ID, depth, (long)origin.x, (long)origin.y,
		(long)(corner.x - origin.x), (long)(corner.y - origin.y),
		window->Enabled ? "true" : "false",
		window == _Focus ? "true" : "false");

	if (!first) out += ',';
	first = false;
	out += buffer;
	Append_JSON_String(out, window->Class != nullptr ? window->Class->Name : std::string());
	out += ",\"text\":";
	Append_JSON_String(out, window->Text);
	out += '}';

	for (UserWindow const * child : window->Children) {
		Describe_Window(out, child, depth + 1, first);
	}
}


void Win32_User_Describe(std::string & out)
{
	bool first = true;

	out += '[';
	for (UserWindow const * window : _TopLevel) {
		Describe_Window(out, window, 0, first);
	}
	out += ']';
}


bool Win32_User_Has_Pending_Messages(void)
{
	return(!_Queue.empty());
}


bool Win32_User_Client_Rect(HWND window, RECT * rect)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr || rect == nullptr) {
		return(false);
	}

	rect->left = 0;
	rect->top = 0;
	rect->right = entry->Rect.right - entry->Rect.left;
	rect->bottom = entry->Rect.bottom - entry->Rect.top;
	return(true);
}


bool Win32_User_Client_Origin(HWND window, POINT * origin)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr || origin == nullptr) {
		return(false);
	}

	*origin = Screen_Origin(entry);
	return(true);
}


// The deepest visible, enabled descendant that owns the point.
static UserWindow * Descendant_From_Screen_Point(UserWindow * parent, POINT point)
{
	POINT origin = Screen_Origin(parent);

	for (unsigned int index = 0; index < parent->Children.size(); index++) {
		UserWindow * child = parent->Children[index];

		if (!child->Visible || !child->Enabled) {
			continue;
		}

		LONG left = origin.x + child->Rect.left;
		LONG top = origin.y + child->Rect.top;
		LONG right = left + (child->Rect.right - child->Rect.left);
		LONG bottom = top + (child->Rect.bottom - child->Rect.top);

		if (point.x < left || point.x >= right || point.y < top || point.y >= bottom) {
			continue;
		}

		UserWindow * deeper = Descendant_From_Screen_Point(child, point);
		return(deeper != nullptr ? deeper : child);
	}

	return(nullptr);
}


static UserWindow * Window_From_Screen_Point(POINT point)
{
	for (unsigned int index = 0; index < _TopLevel.size(); index++) {
		UserWindow * window = _TopLevel[index];

		if (!window->Visible) {
			continue;
		}

		if (point.x < window->Rect.left || point.x >= window->Rect.right) continue;
		if (point.y < window->Rect.top || point.y >= window->Rect.bottom) continue;

		UserWindow * child = Descendant_From_Screen_Point(window, point);
		return(child != nullptr ? child : window);
	}

	return(nullptr);
}


//------------------------------------------------------------------------------
// Classes.
//------------------------------------------------------------------------------


static ATOM Register_Class(UINT style, WNDPROC procedure, int classextra, int windowextra,
	HINSTANCE instance, HICON icon, HCURSOR cursor, HBRUSH background, LPCSTR name)
{
	if (name == nullptr || procedure == nullptr) {
		return(0);
	}

	if (Class_Of(name) != nullptr) {
		return(0);
	}

	UserClass * entry = new UserClass;
	entry->Name = name;
	entry->Style = style;
	entry->Procedure = procedure;
	entry->ClassExtra = classextra;
	entry->WindowExtra = windowextra;
	entry->Instance = instance;
	entry->Icon = icon;
	entry->Cursor = cursor;
	entry->Background = background;
	entry->Atom = _NextAtom++;

	_Classes.push_back(entry);
	return(entry->Atom);
}


ATOM RegisterClassA(WNDCLASSA const * windowclass)
{
	if (windowclass == nullptr) {
		return(0);
	}

	return(Register_Class(windowclass->style, windowclass->lpfnWndProc, windowclass->cbClsExtra,
		windowclass->cbWndExtra, windowclass->hInstance, windowclass->hIcon, windowclass->hCursor,
		windowclass->hbrBackground, windowclass->lpszClassName));
}


ATOM RegisterClassExA(WNDCLASSEXA const * windowclass)
{
	if (windowclass == nullptr) {
		return(0);
	}

	return(Register_Class(windowclass->style, windowclass->lpfnWndProc, windowclass->cbClsExtra,
		windowclass->cbWndExtra, windowclass->hInstance, windowclass->hIcon, windowclass->hCursor,
		windowclass->hbrBackground, windowclass->lpszClassName));
}


BOOL UnregisterClassA(LPCSTR classname, HINSTANCE)
{
	UserClass * entry = Class_Of(classname);
	if (entry == nullptr) {
		return(FALSE);
	}

	for (unsigned int index = 0; index < _Windows.size(); index++) {
		if (_Windows[index]->Class == entry) {
			return(FALSE);
		}
	}

	for (unsigned int index = 0; index < _Classes.size(); index++) {
		if (_Classes[index] == entry) {
			_Classes.erase(_Classes.begin() + index);
			break;
		}
	}

	delete entry;
	return(TRUE);
}


int GetClassNameA(HWND window, LPSTR classname, int count)
{
	if (classname == nullptr || count <= 0) {
		return(0);
	}

	classname[0] = '\0';

	UserWindow * entry = Window_Of(window);
	if (entry == nullptr || entry->Class == nullptr) {
		return(0);
	}

	int length = (int)entry->Class->Name.size();
	if (length > count - 1) {
		length = count - 1;
	}

	memcpy(classname, entry->Class->Name.c_str(), (size_t)length);
	classname[length] = '\0';
	return(length);
}


//------------------------------------------------------------------------------
// Dispatch.
//------------------------------------------------------------------------------


LRESULT CallWindowProcA(WNDPROC previous, HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (previous == nullptr) {
		return(DefWindowProcA(window, message, wparam, lparam));
	}

	return(previous(window, message, wparam, lparam));
}


LRESULT SendMessageA(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(0);
	}

	WNDPROC procedure = entry->Procedure;
	if (procedure == nullptr) {
		return(DefWindowProcA(window, message, wparam, lparam));
	}

	return(procedure(window, message, wparam, lparam));
}


LRESULT SendDlgItemMessageA(HWND dialog, int id, UINT message, WPARAM wparam, LPARAM lparam)
{
	HWND item = GetDlgItem(dialog, id);
	if (item == nullptr) {
		return(0);
	}

	return(SendMessageA(item, message, wparam, lparam));
}


static bool Queue_Message(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (_Queue.size() >= MESSAGE_QUEUE_LIMIT) {
		if (!_QueueReported) {
			_QueueReported = true;
			DebugString("Win32 user: the message queue is full; posted messages are being dropped.\n");
		}
		return(false);
	}

	MSG entry;
	entry.hwnd = window;
	entry.message = message;
	entry.wParam = wparam;
	entry.lParam = lparam;
	entry.time = GetTickCount();
	entry.pt.x = _LastMouse.x;
	entry.pt.y = _LastMouse.y;

	_Queue.push_back({entry, Browser_Event_Modifiers()});
	return(true);
}


BOOL PostMessageA(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (window != nullptr && Window_Of(window) == nullptr) {
		return(FALSE);
	}

	return(Queue_Message(window, message, wparam, lparam) ? TRUE : FALSE);
}


void PostQuitMessage(int exitcode)
{
	Queue_Message(nullptr, WM_QUIT, (WPARAM)exitcode, 0);
}


static bool Message_Matches(MSG const & entry, HWND window, UINT filtermin, UINT filtermax)
{
	if (window != nullptr && entry.hwnd != window) {
		return(false);
	}

	if (filtermin == 0 && filtermax == 0) {
		return(true);
	}

	return(entry.message >= filtermin && entry.message <= filtermax);
}


// Front to back, parents before children.
static UserWindow * Window_Awaiting_Paint(UserWindow * parent)
{
	std::vector<UserWindow *> const & windows = (parent != nullptr) ? parent->Children : _TopLevel;

	for (unsigned int index = 0; index < windows.size(); index++) {
		UserWindow * window = windows[index];

		if (!window->Visible) {
			continue;
		}

		if (window->NeedsPaint) {
			return(window);
		}

		UserWindow * deeper = Window_Awaiting_Paint(window);
		if (deeper != nullptr) {
			return(deeper);
		}
	}

	return(nullptr);
}


//------------------------------------------------------------------------------
// Window timers.
//------------------------------------------------------------------------------


UINT_PTR SetTimer(HWND window, UINT_PTR id, UINT elapse, TIMERPROC callback)
{
	if (callback != nullptr) {
		return(WIN32_UNSUPPORTED("SetTimer: a timer whose ticks go to a callback", 0));
	}

	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(WIN32_UNSUPPORTED("SetTimer: a timer belonging to no window", 0));
	}

	DWORD due = GetTickCount() + elapse;

	for (unsigned int index = 0; index < _Timers.size(); index++) {
		if (_Timers[index].Window == entry && _Timers[index].ID == id) {
			_Timers[index].Elapse = elapse;
			_Timers[index].Due = due;
			return(id);
		}
	}

	UserTimer timer;
	timer.Window = entry;
	timer.ID = id;
	timer.Elapse = elapse;
	timer.Due = due;
	_Timers.push_back(timer);
	return(id);
}


BOOL KillTimer(HWND window, UINT_PTR id)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	for (unsigned int index = 0; index < _Timers.size(); index++) {
		if (_Timers[index].Window == entry && _Timers[index].ID == id) {
			_Timers.erase(_Timers.begin() + index);
			return(TRUE);
		}
	}

	return(FALSE);
}


static int Timer_Awaiting_Tick(void)
{
	DWORD now = GetTickCount();

	for (unsigned int index = 0; index < _Timers.size(); index++) {
		if ((LONG)(now - _Timers[index].Due) >= 0) {
			return((int)index);
		}
	}

	return(-1);
}


BOOL PeekMessageA(LPMSG message, HWND window, UINT filtermin, UINT filtermax, UINT remove)
{
	if (message == nullptr) {
		return(FALSE);
	}

	for (unsigned int index = 0; index < _Queue.size(); index++) {
		if (!Message_Matches(_Queue[index].Message, window, filtermin, filtermax)) {
			continue;
		}

		*message = _Queue[index].Message;

		if ((remove & PM_REMOVE) != 0) {
			// The message's own modifiers stand while the caller acts on it.
			Browser_Apply_Modifiers(_Queue[index].Modifiers);
			_Queue.erase(_Queue.begin() + index);
		}

		return(TRUE);
	}

	// No past moment is owed an answer, so the current modifiers stand.
	if (_Queue.empty()) {
		Browser_Apply_Modifiers(Browser_Key_Modifiers());
	}

	// WM_PAINT is generated, not posted: it is reported once the queue is empty
	// and until the window validates itself, which is what makes an
	// InvalidateRect with no UpdateWindow behind it reach the window at all.
	UserWindow * painting = Window_Awaiting_Paint(nullptr);

	if (painting != _PaintWindow) {
		_PaintWindow = painting;
		_PaintRepeats = 0;
	}

	if (painting != nullptr) {
		_PaintRepeats++;

		// A window that never validates would spin the pump and freeze the tab,
		// so its update is dropped and the window named.
		if (_PaintRepeats > PAINT_REPEAT_LIMIT) {
			DebugString("Win32 user: a %s window never validates its paint; the update is being dropped.\n",
				(painting->Class != nullptr) ? painting->Class->Name.c_str() : "?");
			ValidateRect(Handle_Of(painting), nullptr);
			painting = nullptr;
			_PaintWindow = nullptr;
			_PaintRepeats = 0;
		}
	}

	if (painting != nullptr) {
		MSG paint;
		paint.hwnd = Handle_Of(painting);
		paint.message = WM_PAINT;
		paint.wParam = 0;
		paint.lParam = 0;
		paint.time = 0;
		paint.pt.x = _LastMouse.x;
		paint.pt.y = _LastMouse.y;

		if (Message_Matches(paint, window, filtermin, filtermax)) {
			*message = paint;
			return(TRUE);
		}
	}

	// WM_TIMER ranks after WM_PAINT and rearms only when the tick is removed.
	int due = Timer_Awaiting_Tick();

	if (due >= 0) {
		MSG tick;
		tick.hwnd = Handle_Of(_Timers[(unsigned int)due].Window);
		tick.message = WM_TIMER;
		tick.wParam = (WPARAM)_Timers[(unsigned int)due].ID;
		tick.lParam = 0;
		tick.time = GetTickCount();
		tick.pt.x = _LastMouse.x;
		tick.pt.y = _LastMouse.y;

		if (Message_Matches(tick, window, filtermin, filtermax)) {
			if ((remove & PM_REMOVE) != 0) {
				_Timers[(unsigned int)due].Due = GetTickCount() + _Timers[(unsigned int)due].Elapse;
			}
			*message = tick;
			return(TRUE);
		}
	}

	// A pump that finds nothing asks again at once, which freezes the tab.
	Browser_Yield_If_Due();
	return(FALSE);
}


BOOL GetMessageA(LPMSG message, HWND window, UINT filtermin, UINT filtermax)
{
	if (message == nullptr) {
		return(FALSE);
	}

	while (true) {
		if (PeekMessageA(message, window, filtermin, filtermax, PM_REMOVE)) {
			return(message->message == WM_QUIT ? FALSE : TRUE);
		}

		if (!Browser_Yield_Is_Available()) {
			// Without the yield scaffold a wait here would never return.
			return(WIN32_UNSUPPORTED("GetMessage: waiting for a message without the yield scaffold", FALSE));
		}

		Browser_Yield();
		Win32_User_Service();
	}
}


BOOL TranslateMessage(MSG const * message)
{
	if (message == nullptr) {
		return(FALSE);
	}

	if (message->message != WM_KEYDOWN && message->message != WM_SYSKEYDOWN) {
		return(FALSE);
	}

	unsigned short key = (unsigned short)(message->wParam & 0xFF);
	int character = Browser_Key_To_ASCII((unsigned short)(key | (Browser_Key_Modifiers() & WWKEY_SHIFT_BIT)));

	if (character == '\0') {
		return(FALSE);
	}

	UINT translated = (message->message == WM_SYSKEYDOWN) ? WM_SYSCHAR : WM_CHAR;
	Queue_Message(message->hwnd, translated, (WPARAM)character, message->lParam);
	return(TRUE);
}


LRESULT DispatchMessageA(MSG const * message)
{
	if (message == nullptr || message->hwnd == nullptr) {
		return(0);
	}

	return(SendMessageA(message->hwnd, message->message, message->wParam, message->lParam));
}


LRESULT DefWindowProcA(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	UserWindow * entry = Window_Of(window);

	switch (message) {
		case WM_SETTEXT:
			if (entry != nullptr) {
				entry->Text = (lparam != 0) ? (char const *)lparam : "";
				return(TRUE);
			}
			return(FALSE);

		case WM_GETTEXTLENGTH:
			return(entry != nullptr ? (LRESULT)entry->Text.size() : 0);

		case WM_GETTEXT: {
			char * buffer = (char *)lparam;
			int count = (int)wparam;
			if (buffer == nullptr || count <= 0) {
				return(0);
			}
			buffer[0] = '\0';
			if (entry == nullptr) {
				return(0);
			}
			int length = (int)entry->Text.size();
			if (length > count - 1) {
				length = count - 1;
			}
			memcpy(buffer, entry->Text.c_str(), (size_t)length);
			buffer[length] = '\0';
			return(length);
		}

		case WM_NCCREATE:
			return(TRUE);

		case WM_CLOSE:
			DestroyWindow(window);
			return(0);

		case WM_NCHITTEST:
			return(HTCLIENT);

		case WM_PAINT:
			ValidateRect(window, nullptr);
			return(0);

		default:
			return(0);
	}
}


//------------------------------------------------------------------------------
// Windows.
//------------------------------------------------------------------------------


HWND CreateWindowExA(DWORD exstyle, LPCSTR classname, LPCSTR windowname, DWORD style,
	int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param)
{
	UserClass * windowclass = Class_Of(classname);
	if (windowclass == nullptr) {
		return(nullptr);
	}

	UserWindow * parentwindow = Window_Of(parent);
	if (parent != nullptr && parentwindow == nullptr) {
		return(nullptr);
	}

	if (x == CW_USEDEFAULT) x = 0;
	if (y == CW_USEDEFAULT) y = 0;
	if (width == CW_USEDEFAULT) width = 0;
	if (height == CW_USEDEFAULT) height = 0;

	UserWindow * window = new UserWindow;
	window->Class = windowclass;
	window->Procedure = windowclass->Procedure;
	window->Instance = (instance != nullptr) ? instance : windowclass->Instance;
	window->ID = ((style & WS_CHILD) != 0) ? (int)(ULONG_PTR)menu : 0;
	window->Style = style;
	window->ExStyle = exstyle;
	window->Rect.left = x;
	window->Rect.top = y;
	window->Rect.right = x + width;
	window->Rect.bottom = y + height;
	window->Text = (windowname != nullptr) ? windowname : "";
	window->UserData = 0;
	window->Visible = ((style & WS_VISIBLE) != 0);
	window->Enabled = ((style & WS_DISABLED) == 0);
	window->NeedsPaint = window->Visible;
	window->Painting = false;
	window->UpdateRect.left = 0;
	window->UpdateRect.top = 0;
	window->UpdateRect.right = width;
	window->UpdateRect.bottom = height;
	window->Parent = ((style & WS_CHILD) != 0) ? parentwindow : nullptr;

	int extrabytes = windowclass->WindowExtra;
	if (extrabytes < MINIMUM_WINDOW_EXTRA) {
		extrabytes = MINIMUM_WINDOW_EXTRA;
	}
	window->Extra.assign((size_t)((extrabytes + 3) / 4) * 4, 0);

	_Windows.push_back(window);

	if (window->Parent != nullptr) {
		window->Parent->Children.insert(window->Parent->Children.begin(), window);
	} else {
		_TopLevel.insert(_TopLevel.begin(), window);
	}

	HWND handle = Handle_Of(window);

	// CREATESTRUCT describes the call as made: the class name as written,
	// string or atom, and the sizes asked for rather than those stored.
	CREATESTRUCTA create;
	create.lpCreateParams = param;
	create.hInstance = window->Instance;
	create.hMenu = menu;
	create.hwndParent = parent;
	create.cy = height;
	create.cx = width;
	create.y = y;
	create.x = x;
	create.style = (LONG)style;
	create.lpszName = windowname;
	create.lpszClass = classname;
	create.dwExStyle = exstyle;

	// WM_NCCREATE refuses with FALSE, WM_CREATE with -1.
	if (SendMessageA(handle, WM_NCCREATE, 0, (LPARAM)&create) == 0) {
		DestroyWindow(handle);
		return(nullptr);
	}

	if (SendMessageA(handle, WM_CREATE, 0, (LPARAM)&create) == (LRESULT)-1) {
		DestroyWindow(handle);
		return(nullptr);
	}

	if (window->Visible) {
		SendMessageA(handle, WM_SHOWWINDOW, TRUE, 0);
	}

	return(handle);
}


// Nothing composites: the parent's repaint is what puts the background back
// behind a window that goes away, moves, or hides.
static void Invalidate_Behind(UserWindow * window)
{
	if (window->Parent == nullptr || !window->Visible) {
		return;
	}

	InvalidateRect(Handle_Of(window->Parent), &window->Rect, TRUE);
}


static void Unlink_Window(UserWindow * window)
{
	std::vector<UserWindow *> & siblings = (window->Parent != nullptr) ? window->Parent->Children : _TopLevel;

	for (unsigned int index = 0; index < siblings.size(); index++) {
		if (siblings[index] == window) {
			siblings.erase(siblings.begin() + index);
			break;
		}
	}

	for (unsigned int index = 0; index < _Windows.size(); index++) {
		if (_Windows[index] == window) {
			_Windows.erase(_Windows.begin() + index);
			break;
		}
	}
}


static void Destroy_Window(UserWindow * window)
{
	HWND handle = Handle_Of(window);

	Invalidate_Behind(window);

	SendMessageA(handle, WM_DESTROY, 0, 0);

	while (!window->Children.empty()) {
		Destroy_Window(window->Children[0]);
	}

	SendMessageA(handle, WM_NCDESTROY, 0, 0);

	if (_Focus == window) {
		_Focus = nullptr;
		Update_Text_Input();
	}
	if (_Active == window) _Active = nullptr;
	if (_Foreground == window) _Foreground = nullptr;
	if (_PaintWindow == window) _PaintWindow = nullptr;

	if (GetCapture() == handle) {
		ReleaseCapture();
	}

	for (unsigned int index = 0; index < _HotKeys.size(); ) {
		if (_HotKeys[index].Window == window) {
			_HotKeys.erase(_HotKeys.begin() + index);
		} else {
			index++;
		}
	}

	for (unsigned int index = 0; index < _Timers.size(); ) {
		if (_Timers[index].Window == window) {
			_Timers.erase(_Timers.begin() + index);
		} else {
			index++;
		}
	}

	for (unsigned int index = 0; index < _Queue.size(); ) {
		if (_Queue[index].Message.hwnd == handle) {
			_Queue.erase(_Queue.begin() + index);
		} else {
			index++;
		}
	}

	Unlink_Window(window);
	delete window;
}


BOOL DestroyWindow(HWND window)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	Destroy_Window(entry);
	return(TRUE);
}


BOOL ShowWindow(HWND window, int command)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	bool wasvisible = entry->Visible;
	bool visible = (command != SW_HIDE);

	if (visible != wasvisible) {
		if (visible) {
			entry->Visible = true;
			entry->Style |= WS_VISIBLE;
			InvalidateRect(window, nullptr, TRUE);
		} else {
			Invalidate_Behind(entry);
			entry->Visible = false;
			entry->Style &= ~(DWORD)WS_VISIBLE;
		}
		SendMessageA(window, WM_SHOWWINDOW, visible ? TRUE : FALSE, 0);
	}

	return(wasvisible ? TRUE : FALSE);
}


BOOL IsWindow(HWND window)
{
	return(Window_Of(window) != nullptr ? TRUE : FALSE);
}


BOOL IsWindowVisible(HWND window)
{
	UserWindow * entry = Window_Of(window);

	for (UserWindow * walk = entry; walk != nullptr; walk = walk->Parent) {
		if (!walk->Visible) {
			return(FALSE);
		}
	}

	return(entry != nullptr ? TRUE : FALSE);
}


BOOL IsWindowEnabled(HWND window)
{
	UserWindow * entry = Window_Of(window);
	return((entry != nullptr && entry->Enabled) ? TRUE : FALSE);
}


BOOL IsIconic(HWND window)
{
	UserWindow * entry = Window_Of(window);
	return((entry != nullptr && (entry->Style & WS_MINIMIZE) != 0) ? TRUE : FALSE);
}


BOOL EnableWindow(HWND window, BOOL enable)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	bool wasdisabled = !entry->Enabled;
	bool enabled = (enable != FALSE);

	if (enabled != entry->Enabled) {
		entry->Enabled = enabled;
		if (enabled) {
			entry->Style &= ~(DWORD)WS_DISABLED;
		} else {
			entry->Style |= WS_DISABLED;
		}
		SendMessageA(window, WM_ENABLE, enable != FALSE ? TRUE : FALSE, 0);
	}

	return(wasdisabled ? TRUE : FALSE);
}


BOOL CloseWindow(HWND window)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	entry->Style |= WS_MINIMIZE;
	return(TRUE);
}


static void Move_Window(UserWindow * window, int x, int y, int width, int height, bool repaint)
{
	bool moved = (window->Rect.left != x || window->Rect.top != y);
	bool resized = ((window->Rect.right - window->Rect.left) != width ||
					(window->Rect.bottom - window->Rect.top) != height);

	if (moved || resized) {
		Invalidate_Behind(window);
	}

	window->Rect.left = x;
	window->Rect.top = y;
	window->Rect.right = x + width;
	window->Rect.bottom = y + height;

	HWND handle = Handle_Of(window);

	if (moved) {
		POINT origin = Screen_Origin(window);
		SendMessageA(handle, WM_MOVE, 0, MAKELONG(origin.x, origin.y));
	}

	if (resized) {
		SendMessageA(handle, WM_SIZE, 0, MAKELONG(width, height));
	}

	if (repaint && window->Visible) {
		InvalidateRect(handle, nullptr, TRUE);
	}
}


BOOL MoveWindow(HWND window, int x, int y, int width, int height, BOOL repaint)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	Move_Window(entry, x, y, width, height, repaint != FALSE);
	return(TRUE);
}


static void Raise_Window(UserWindow * window)
{
	std::vector<UserWindow *> & siblings = (window->Parent != nullptr) ? window->Parent->Children : _TopLevel;

	for (unsigned int index = 0; index < siblings.size(); index++) {
		if (siblings[index] == window) {
			siblings.erase(siblings.begin() + index);
			siblings.insert(siblings.begin(), window);
			return;
		}
	}
}


static void Lower_Window(UserWindow * window)
{
	std::vector<UserWindow *> & siblings = (window->Parent != nullptr) ? window->Parent->Children : _TopLevel;

	for (unsigned int index = 0; index < siblings.size(); index++) {
		if (siblings[index] == window) {
			siblings.erase(siblings.begin() + index);
			siblings.push_back(window);
			return;
		}
	}
}


BOOL SetWindowPos(HWND window, HWND insertafter, int x, int y, int cx, int cy, UINT flags)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	int newx = ((flags & SWP_NOMOVE) != 0) ? entry->Rect.left : x;
	int newy = ((flags & SWP_NOMOVE) != 0) ? entry->Rect.top : y;
	int width = ((flags & SWP_NOSIZE) != 0) ? (entry->Rect.right - entry->Rect.left) : cx;
	int height = ((flags & SWP_NOSIZE) != 0) ? (entry->Rect.bottom - entry->Rect.top) : cy;

	Move_Window(entry, newx, newy, width, height, (flags & SWP_NOREDRAW) == 0);

	if ((flags & SWP_NOZORDER) == 0) {
		if (insertafter == HWND_BOTTOM) {
			Lower_Window(entry);
		} else {
			Raise_Window(entry);
		}
	}

	if ((flags & SWP_SHOWWINDOW) != 0) ShowWindow(window, SW_SHOW);
	if ((flags & SWP_HIDEWINDOW) != 0) ShowWindow(window, SW_HIDE);

	return(TRUE);
}


BOOL BringWindowToTop(HWND window)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	Raise_Window(entry);
	return(TRUE);
}


HWND GetParent(HWND window)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr || entry->Parent == nullptr) {
		return(nullptr);
	}

	return(Handle_Of(entry->Parent));
}


HWND GetDesktopWindow(void)
{
	// There is no desktop window. NULL is also HWND_DESKTOP, which
	// MapWindowPoints callers pass to mean screen coordinates.
	return(nullptr);
}


HWND GetTopWindow(HWND window)
{
	UserWindow * entry = Window_Of(window);

	if (entry == nullptr) {
		return(_TopLevel.empty() ? nullptr : Handle_Of(_TopLevel[0]));
	}

	return(entry->Children.empty() ? nullptr : Handle_Of(entry->Children[0]));
}


HWND GetWindow(HWND window, UINT command)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(nullptr);
	}

	std::vector<UserWindow *> const & siblings = (entry->Parent != nullptr) ? entry->Parent->Children : _TopLevel;

	int position = -1;
	for (unsigned int index = 0; index < siblings.size(); index++) {
		if (siblings[index] == entry) {
			position = (int)index;
			break;
		}
	}

	switch (command) {
		case GW_CHILD:
			return(entry->Children.empty() ? nullptr : Handle_Of(entry->Children[0]));

		case GW_HWNDFIRST:
			return(siblings.empty() ? nullptr : Handle_Of(siblings[0]));

		case GW_HWNDLAST:
			return(siblings.empty() ? nullptr : Handle_Of(siblings[siblings.size() - 1]));

		case GW_HWNDNEXT:
			if (position >= 0 && (unsigned int)(position + 1) < siblings.size()) {
				return(Handle_Of(siblings[position + 1]));
			}
			return(nullptr);

		case GW_HWNDPREV:
			if (position > 0) {
				return(Handle_Of(siblings[position - 1]));
			}
			return(nullptr);

		case GW_OWNER:
			return(nullptr);

		default:
			return(WIN32_UNSUPPORTED("GetWindow: a relationship the registry does not keep", (HWND)nullptr));
	}
}


BOOL IsChild(HWND parent, HWND window)
{
	UserWindow * parententry = Window_Of(parent);
	UserWindow * entry = Window_Of(window);

	if (parententry == nullptr || entry == nullptr) {
		return(FALSE);
	}

	for (UserWindow * walk = entry->Parent; walk != nullptr; walk = walk->Parent) {
		if (walk == parententry) {
			return(TRUE);
		}
	}

	return(FALSE);
}


HWND GetDlgItem(HWND dialog, int id)
{
	UserWindow * entry = Window_Of(dialog);
	if (entry == nullptr) {
		return(nullptr);
	}

	for (unsigned int index = 0; index < entry->Children.size(); index++) {
		if (entry->Children[index]->ID == id) {
			return(Handle_Of(entry->Children[index]));
		}

		HWND deeper = GetDlgItem(Handle_Of(entry->Children[index]), id);
		if (deeper != nullptr) {
			return(deeper);
		}
	}

	return(nullptr);
}


int GetDlgCtrlID(HWND window)
{
	UserWindow * entry = Window_Of(window);
	return(entry != nullptr ? entry->ID : 0);
}


HWND FindWindowA(LPCSTR classname, LPCSTR windowname)
{
	for (unsigned int index = 0; index < _TopLevel.size(); index++) {
		UserWindow * window = _TopLevel[index];

		if (classname != nullptr && (window->Class == nullptr || !Names_Match(window->Class->Name.c_str(), classname))) {
			continue;
		}

		if (windowname != nullptr && !Names_Match(window->Text.c_str(), windowname)) {
			continue;
		}

		return(Handle_Of(window));
	}

	return(nullptr);
}


BOOL EnumChildWindows(HWND parent, WNDENUMPROC callback, LPARAM parameter)
{
	UserWindow * entry = Window_Of(parent);
	if (entry == nullptr || callback == nullptr) {
		return(FALSE);
	}

	// The callback may destroy what it is handed, so the walk uses a snapshot.
	std::vector<UserWindow *> children = entry->Children;

	for (unsigned int index = 0; index < children.size(); index++) {
		if (Window_Of(Handle_Of(children[index])) == nullptr) {
			continue;
		}

		if (!callback(Handle_Of(children[index]), parameter)) {
			return(FALSE);
		}

		if (Window_Of(Handle_Of(children[index])) == nullptr) {
			continue;
		}

		if (!EnumChildWindows(Handle_Of(children[index]), callback, parameter)) {
			return(FALSE);
		}
	}

	return(TRUE);
}


HWND ChildWindowFromPoint(HWND parent, POINT point)
{
	UserWindow * entry = Window_Of(parent);
	if (entry == nullptr) {
		return(nullptr);
	}

	for (unsigned int index = 0; index < entry->Children.size(); index++) {
		UserWindow * child = entry->Children[index];

		if (point.x < child->Rect.left || point.x >= child->Rect.right) continue;
		if (point.y < child->Rect.top || point.y >= child->Rect.bottom) continue;

		return(Handle_Of(child));
	}

	return(parent);
}


HWND WindowFromPoint(POINT point)
{
	UserWindow * window = Window_From_Screen_Point(point);
	return(window != nullptr ? Handle_Of(window) : nullptr);
}


int MapWindowPoints(HWND from, HWND to, LPPOINT points, UINT count)
{
	if (points == nullptr) {
		return(0);
	}

	POINT fromorigin;
	fromorigin.x = 0;
	fromorigin.y = 0;
	UserWindow * fromwindow = Window_Of(from);
	if (fromwindow != nullptr) {
		fromorigin = Screen_Origin(fromwindow);
	}

	POINT toorigin;
	toorigin.x = 0;
	toorigin.y = 0;
	UserWindow * towindow = Window_Of(to);
	if (towindow != nullptr) {
		toorigin = Screen_Origin(towindow);
	}

	LONG dx = fromorigin.x - toorigin.x;
	LONG dy = fromorigin.y - toorigin.y;

	for (UINT index = 0; index < count; index++) {
		points[index].x += dx;
		points[index].y += dy;
	}

	return((int)MAKELONG(dx, dy));
}


//------------------------------------------------------------------------------
// Window words.
//------------------------------------------------------------------------------


static bool Extra_Slot(UserWindow const * entry, int index, size_t width)
{
	return(index >= 0 && (index % 4) == 0 && (size_t)index + width <= entry->Extra.size());
}


LONG_PTR GetWindowLongPtrA(HWND window, int index)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(0);
	}

	switch (index) {
		case GWL_WNDPROC:	return((LONG_PTR)entry->Procedure);
		case GWL_HINSTANCE:	return((LONG_PTR)entry->Instance);
		case GWL_HWNDPARENT: return((LONG_PTR)(entry->Parent != nullptr ? Handle_Of(entry->Parent) : nullptr));
		case GWL_STYLE:		return((LONG)entry->Style);
		case GWL_EXSTYLE:	return((LONG)entry->ExStyle);
		case GWL_USERDATA:	return(entry->UserData);
		case GWL_ID:		return((LONG_PTR)entry->ID);
		default:			break;
	}

	if (!Extra_Slot(entry, index, sizeof(LONG_PTR))) {
		return(0);
	}

	LONG_PTR value;
	memcpy(&value, &entry->Extra[(size_t)index], sizeof(value));
	return(value);
}


LONG GetWindowLongA(HWND window, int index)
{
	if (index < 0) {
		return((LONG)GetWindowLongPtrA(window, index));
	}

	UserWindow * entry = Window_Of(window);
	if (entry == nullptr || !Extra_Slot(entry, index, sizeof(LONG))) {
		return(0);
	}

	LONG value;
	memcpy(&value, &entry->Extra[(size_t)index], sizeof(value));
	return(value);
}


LONG_PTR SetWindowLongPtrA(HWND window, int index, LONG_PTR value)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(0);
	}

	LONG_PTR previous = GetWindowLongPtrA(window, index);

	switch (index) {
		case GWL_WNDPROC:
			entry->Procedure = (WNDPROC)value;
			return(previous);

		case GWL_HINSTANCE:
			entry->Instance = (HINSTANCE)value;
			return(previous);

		case GWL_STYLE:
			entry->Style = (DWORD)value;
			entry->Visible = ((entry->Style & WS_VISIBLE) != 0);
			entry->Enabled = ((entry->Style & WS_DISABLED) == 0);
			return(previous);

		case GWL_EXSTYLE:
			entry->ExStyle = (DWORD)value;
			return(previous);

		case GWL_USERDATA:
			entry->UserData = value;
			return(previous);

		case GWL_ID:
			entry->ID = (int)value;
			return(previous);

		case GWL_HWNDPARENT:
			return(WIN32_UNSUPPORTED("SetWindowLongPtr: reparenting through GWLP_HWNDPARENT", previous));

		default:
			break;
	}

	if (!Extra_Slot(entry, index, sizeof(LONG_PTR))) {
		return(0);
	}

	memcpy(&entry->Extra[(size_t)index], &value, sizeof(value));
	return(previous);
}


LONG SetWindowLongA(HWND window, int index, LONG value)
{
	if (index < 0) {
		return((LONG)SetWindowLongPtrA(window, index, (LONG_PTR)value));
	}

	UserWindow * entry = Window_Of(window);
	if (entry == nullptr || !Extra_Slot(entry, index, sizeof(LONG))) {
		return(0);
	}

	LONG previous;
	memcpy(&previous, &entry->Extra[(size_t)index], sizeof(previous));
	memcpy(&entry->Extra[(size_t)index], &value, sizeof(value));
	return(previous);
}


BOOL SetWindowTextA(HWND window, LPCSTR text)
{
	if (Window_Of(window) == nullptr) {
		return(FALSE);
	}

	return(SendMessageA(window, WM_SETTEXT, 0, (LPARAM)text) != 0 ? TRUE : FALSE);
}


int GetWindowTextA(HWND window, LPSTR text, int count)
{
	if (text == nullptr || count <= 0) {
		return(0);
	}

	text[0] = '\0';

	if (Window_Of(window) == nullptr) {
		return(0);
	}

	return((int)SendMessageA(window, WM_GETTEXT, (WPARAM)count, (LPARAM)text));
}


int GetWindowTextLengthA(HWND window)
{
	if (Window_Of(window) == nullptr) {
		return(0);
	}

	return((int)SendMessageA(window, WM_GETTEXTLENGTH, 0, 0));
}


BOOL SetDlgItemTextA(HWND dialog, int id, LPCSTR text)
{
	HWND item = GetDlgItem(dialog, id);
	if (item == nullptr) {
		return(FALSE);
	}

	return(SetWindowTextA(item, text));
}


UINT GetDlgItemTextA(HWND dialog, int id, LPSTR text, int count)
{
	if (text != nullptr && count > 0) {
		text[0] = '\0';
	}

	HWND item = GetDlgItem(dialog, id);
	if (item == nullptr) {
		return(0);
	}

	return((UINT)GetWindowTextA(item, text, count));
}


BOOL CheckDlgButton(HWND dialog, int id, UINT check)
{
	HWND item = GetDlgItem(dialog, id);
	if (item == nullptr) {
		return(FALSE);
	}

	SendMessageA(item, BM_SETCHECK, (WPARAM)check, 0);
	return(TRUE);
}


UINT IsDlgButtonChecked(HWND dialog, int id)
{
	HWND item = GetDlgItem(dialog, id);
	if (item == nullptr) {
		return(0);
	}

	return((UINT)SendMessageA(item, BM_GETCHECK, 0, 0));
}


//------------------------------------------------------------------------------
// Focus, activation and painting.
//------------------------------------------------------------------------------


HWND SetFocus(HWND window)
{
	UserWindow * entry = Window_Of(window);
	if (window != nullptr && entry == nullptr) {
		return(nullptr);
	}

	UserWindow * previous = _Focus;
	if (previous == entry) {
		return(previous != nullptr ? Handle_Of(previous) : nullptr);
	}

	_Focus = entry;

	if (previous != nullptr) {
		SendMessageA(Handle_Of(previous), WM_KILLFOCUS, (WPARAM)window, 0);
	}
	if (entry != nullptr) {
		SendMessageA(window, WM_SETFOCUS, (WPARAM)(previous != nullptr ? Handle_Of(previous) : nullptr), 0);
	}

	Update_Text_Input();

	return(previous != nullptr ? Handle_Of(previous) : nullptr);
}


HWND GetFocus(void)
{
	return(_Focus != nullptr ? Handle_Of(_Focus) : nullptr);
}


HWND GetActiveWindow(void)
{
	return(_Active != nullptr ? Handle_Of(_Active) : nullptr);
}


HWND SetActiveWindow(HWND window)
{
	UserWindow * entry = Window_Of(window);
	if (window != nullptr && entry == nullptr) {
		return(nullptr);
	}

	UserWindow * previous = _Active;
	_Active = entry;

	if (previous != entry) {
		if (previous != nullptr) {
			SendMessageA(Handle_Of(previous), WM_ACTIVATE, 0, 0);
		}
		if (entry != nullptr) {
			SendMessageA(window, WM_ACTIVATE, 1, 0);
		}
	}

	return(previous != nullptr ? Handle_Of(previous) : nullptr);
}


HWND GetForegroundWindow(void)
{
	return(_Foreground != nullptr ? Handle_Of(_Foreground) : nullptr);
}


BOOL SetForegroundWindow(HWND window)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	_Foreground = entry;
	Raise_Window(entry);
	SetActiveWindow(window);
	return(TRUE);
}


BOOL InvalidateRect(HWND window, RECT const * rect, BOOL)
{
	if (window == nullptr) {
		for (unsigned int index = 0; index < _Windows.size(); index++) {
			_Windows[index]->NeedsPaint = true;
			Win32_User_Client_Rect(Handle_Of(_Windows[index]), &_Windows[index]->UpdateRect);
		}
		return(TRUE);
	}

	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	RECT client;
	Win32_User_Client_Rect(window, &client);

	if (rect == nullptr) {
		entry->UpdateRect = client;
	} else if (entry->NeedsPaint) {
		UnionRect(&entry->UpdateRect, &entry->UpdateRect, rect);
	} else {
		entry->UpdateRect = *rect;
	}

	entry->NeedsPaint = true;
	return(TRUE);
}


BOOL ValidateRect(HWND window, RECT const *)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	// Every caller validates the whole window, so no region is tracked.
	entry->NeedsPaint = false;
	SetRectEmpty(&entry->UpdateRect);
	return(TRUE);
}


BOOL GetUpdateRect(HWND window, LPRECT rect, BOOL)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		if (rect != nullptr) SetRectEmpty(rect);
		return(FALSE);
	}

	if (rect != nullptr) {
		*rect = entry->UpdateRect;
	}

	return(entry->NeedsPaint ? TRUE : FALSE);
}


BOOL UpdateWindow(HWND window)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	if (!entry->NeedsPaint || !entry->Visible || entry->Painting) {
		return(TRUE);
	}

	// The update region survives the paint, as on Windows, so reentry is
	// guarded by the flag rather than by clearing the region first.
	entry->Painting = true;
	SendMessageA(window, WM_PAINT, 0, 0);
	entry->Painting = false;
	return(TRUE);
}


BOOL RedrawWindow(HWND window, RECT const * update, HRGN, UINT flags)
{
	if (!InvalidateRect(window, update, FALSE)) {
		return(FALSE);
	}

	if ((flags & RDW_UPDATENOW_FLAG) != 0) {
		UpdateWindow(window);
	}

	return(TRUE);
}


//------------------------------------------------------------------------------
// Hot keys.
//------------------------------------------------------------------------------


BOOL RegisterHotKey(HWND window, int id, UINT modifiers, UINT key)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	for (unsigned int index = 0; index < _HotKeys.size(); index++) {
		if (_HotKeys[index].Window == entry && _HotKeys[index].ID == id) {
			return(FALSE);
		}
	}

	UserHotKey hotkey;
	hotkey.Window = entry;
	hotkey.ID = id;
	hotkey.Modifiers = modifiers;
	hotkey.Key = key;
	_HotKeys.push_back(hotkey);
	return(TRUE);
}


BOOL UnregisterHotKey(HWND window, int id)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	for (unsigned int index = 0; index < _HotKeys.size(); index++) {
		if (_HotKeys[index].Window == entry && _HotKeys[index].ID == id) {
			_HotKeys.erase(_HotKeys.begin() + index);
			return(TRUE);
		}
	}

	return(FALSE);
}


//------------------------------------------------------------------------------
// Trivia the engine asks for on the way up.
//------------------------------------------------------------------------------


BOOL InitCommonControls(void)
{
	return(TRUE);
}


// Nothing on this target draws an HICON, so the result is a token distinct per
// request rather than an image.
HICON LoadIconA(HINSTANCE, LPCSTR name)
{
	static int _tokens = 0;

	if (name == nullptr) {
		return(nullptr);
	}

	_tokens++;
	return((HICON)(ULONG_PTR)(0x1C00 + _tokens));
}


//------------------------------------------------------------------------------
// Dialogs.
//------------------------------------------------------------------------------


// There is no system font, so the classic 8 by 16 stands in. windlg.cpp
// measures its reference template in the same units and scales every dialog by
// the ratio, so the value only has to be the one both sides agree on.
static int const DIALOG_BASE_UNIT_X = 8;
static int const DIALOG_BASE_UNIT_Y = 16;

// Windows's own name for the dialog class.
static char const * const DIALOG_CLASS_NAME = "#32770";

// An extended template's first double word; no classic template begins with it.
static DWORD const DIALOG_TEMPLATE_EX_SIGNATURE = 0xFFFF0001;

// Bounds a template data.cpp does not know the length of, an order of magnitude
// past the largest template shipped.
static unsigned int const DIALOG_TEMPLATE_LIMIT = 64u * 1024u;


// Where DialogBox's pump and EndDialog meet.
struct ModalDialog
{
	HWND Window;
	INT_PTR Result;
	bool Ended;
};

static std::vector<ModalDialog *> _ModalDialogs;


enum class TemplateNameType
{
	Empty,
	Ordinal,
	Text
};


// Stops at the end of the resource rather than reading past it.
struct DialogTemplateReader
{
	unsigned char const * Base;
	unsigned char const * Point;
	unsigned char const * End;
	bool Overrun;

	DialogTemplateReader(void const * data, unsigned int size) :
		Base((unsigned char const *)data),
		Point((unsigned char const *)data),
		End((unsigned char const *)data + size),
		Overrun(false)
	{
	}

	bool Has(unsigned int bytes) const
	{
		return(!Overrun && (unsigned int)(End - Point) >= bytes);
	}

	WORD Fetch_Word(void)
	{
		if (!Has(2)) {
			Overrun = true;
			return(0);
		}

		WORD value;
		memcpy(&value, Point, sizeof(value));
		Point += sizeof(value);
		return(value);
	}

	DWORD Fetch_Long(void)
	{
		DWORD low = Fetch_Word();
		DWORD high = Fetch_Word();
		return(low | (high << 16));
	}

	short Fetch_Short(void)
	{
		return((short)Fetch_Word());
	}

	void Skip(unsigned int bytes)
	{
		if (!Has(bytes)) {
			Overrun = true;
			Point = End;
			return;
		}

		Point += bytes;
	}

	// Measured from the start of the template, not from the buffer.
	void Align(void)
	{
		Skip((unsigned int)((4 - ((size_t)(Point - Base) & 3)) & 3));
	}

	TemplateNameType Fetch_Name(std::string & text, unsigned int & ordinal);
};


// A character outside ASCII is stood in for, because the engine reads the
// result as bytes.
TemplateNameType DialogTemplateReader::Fetch_Name(std::string & text, unsigned int & ordinal)
{
	text.clear();
	ordinal = 0;

	WORD character = Fetch_Word();

	if (character == 0) {
		return(TemplateNameType::Empty);
	}

	if (character == 0xFFFF) {
		ordinal = Fetch_Word();
		return(TemplateNameType::Ordinal);
	}

	while (character != 0 && !Overrun) {
		text.push_back((character < 0x80) ? (char)character : '?');
		character = Fetch_Word();
	}

	return(TemplateNameType::Text);
}


struct DialogTemplateHeader
{
	DWORD Style;
	DWORD ExStyle;
	unsigned int ItemCount;
	int X;
	int Y;
	int CX;
	int CY;
	std::string Class;
	std::string Title;
	bool HasClass;
};


struct DialogTemplateItem
{
	DWORD Style;
	DWORD ExStyle;
	int X;
	int Y;
	int CX;
	int CY;
	int ID;
	std::string Class;
	std::string Title;
};


static int Dialog_Units_To_Pixels_X(int units)
{
	return(units * DIALOG_BASE_UNIT_X / 4);
}


static int Dialog_Units_To_Pixels_Y(int units)
{
	return(units * DIALOG_BASE_UNIT_Y / 8);
}


static bool Read_Dialog_Header(DialogTemplateReader & reader, DialogTemplateHeader & header, bool & extended)
{
	extended = false;
	if (reader.Has(4)) {
		DWORD signature;
		memcpy(&signature, reader.Point, sizeof(signature));
		extended = (signature == DIALOG_TEMPLATE_EX_SIGNATURE);
	}

	if (extended) {
		reader.Skip(4);					// the version and the signature
		reader.Fetch_Long();			// the help identifier
		header.ExStyle = reader.Fetch_Long();
		header.Style = reader.Fetch_Long();
	} else {
		header.Style = reader.Fetch_Long();
		header.ExStyle = reader.Fetch_Long();
	}

	header.ItemCount = reader.Fetch_Word();
	header.X = reader.Fetch_Short();
	header.Y = reader.Fetch_Short();
	header.CX = reader.Fetch_Short();
	header.CY = reader.Fetch_Short();

	std::string scratch;
	unsigned int ordinal = 0;

	reader.Fetch_Name(scratch, ordinal);	// the menu, which nothing here can show

	header.HasClass = (reader.Fetch_Name(header.Class, ordinal) == TemplateNameType::Text);
	reader.Fetch_Name(header.Title, ordinal);

	if ((header.Style & DS_SETFONT) != 0) {
		reader.Fetch_Word();				// the point size
		if (extended) {
			reader.Fetch_Word();			// the weight
			reader.Skip(2);					// the italic flag and the character set
		}
		reader.Fetch_Name(scratch, ordinal);	// the face name
	}

	return(!reader.Overrun);
}


static bool Read_Dialog_Item(DialogTemplateReader & reader, bool extended, DialogTemplateItem & item)
{
	reader.Align();

	if (extended) {
		reader.Fetch_Long();			// the help identifier
		item.ExStyle = reader.Fetch_Long();
		item.Style = reader.Fetch_Long();
	} else {
		item.Style = reader.Fetch_Long();
		item.ExStyle = reader.Fetch_Long();
	}

	item.X = reader.Fetch_Short();
	item.Y = reader.Fetch_Short();
	item.CX = reader.Fetch_Short();
	item.CY = reader.Fetch_Short();

	// A classic template's unused identifier arrives as 65535, and Windows
	// widens it the same way; the front end compares against the widened form.
	item.ID = extended ? (int)reader.Fetch_Long() : (int)reader.Fetch_Word();

	unsigned int ordinal = 0;
	if (reader.Fetch_Name(item.Class, ordinal) == TemplateNameType::Ordinal) {
		char const * name = Win32_Stock_Control_Class(ordinal);
		item.Class = (name != nullptr) ? name : "";
	}

	reader.Fetch_Name(item.Title, ordinal);

	// The creation data's leading word counts itself.
	WORD creation = reader.Fetch_Word();
	if (creation > sizeof(WORD)) {
		reader.Skip(creation - sizeof(WORD));
	}

	return(!reader.Overrun);
}


// ownrdraw.cpp reads DWLP_DLGPROC to tell a dialog from a control. A dialog
// procedure reports whether it handled a message and leaves any result in
// DWLP_MSGRESULT.
static LRESULT CALLBACK Dialog_Window_Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	DLGPROC procedure = (DLGPROC)GetWindowLongPtrA(window, DWLP_DLGPROC);

	if (procedure != nullptr) {
		SetWindowLongPtrA(window, DWLP_MSGRESULT, 0);

		INT_PTR handled = procedure(window, message, wparam, lparam);
		if (handled != 0) {
			LONG_PTR result = GetWindowLongPtrA(window, DWLP_MSGRESULT);
			return((result != 0) ? (LRESULT)result : (LRESULT)handled);
		}
	}

	return(DefWindowProcA(window, message, wparam, lparam));
}


static void Register_Dialog_Class(void)
{
	static bool registered = false;
	if (registered) {
		return;
	}
	registered = true;

	WNDCLASSA windowclass;
	memset(&windowclass, 0, sizeof(windowclass));

	windowclass.style = CS_DBLCLKS;
	windowclass.lpfnWndProc = Dialog_Window_Proc;
	windowclass.cbWndExtra = DLGWINDOWEXTRA;
	windowclass.lpszClassName = DIALOG_CLASS_NAME;

	RegisterClassA(&windowclass);
}


static HWND First_Tab_Stop(HWND dialog)
{
	UserWindow * entry = Window_Of(dialog);
	if (entry == nullptr) {
		return(nullptr);
	}

	// Template order is the sibling chain read backwards.
	for (unsigned int index = entry->Children.size(); index > 0; index--) {
		UserWindow * child = entry->Children[index - 1];

		if ((child->Style & WS_TABSTOP) == 0) continue;
		if (!child->Visible || !child->Enabled) continue;

		return(Handle_Of(child));
	}

	return(nullptr);
}


DWORD GetDialogBaseUnits(void)
{
	return(MAKELONG(DIALOG_BASE_UNIT_X, DIALOG_BASE_UNIT_Y));
}


HWND CreateDialogIndirectParamA(HINSTANCE instance, LPCDLGTEMPLATE dialogtemplate, HWND parent,
	DLGPROC dialogproc, LPARAM initparam)
{
	if (dialogtemplate == nullptr) {
		return(nullptr);
	}

	unsigned int size = Fetch_Resource_Size(dialogtemplate);
	if (size == 0) {
		size = DIALOG_TEMPLATE_LIMIT;
	}

	Register_Dialog_Class();
	Win32_Register_Stock_Controls();

	DialogTemplateReader reader(dialogtemplate, size);
	DialogTemplateHeader header;
	bool extended = false;

	if (!Read_Dialog_Header(reader, header, extended)) {
		return(nullptr);
	}

	char const * classname = header.HasClass ? header.Class.c_str() : DIALOG_CLASS_NAME;

	// Built hidden so that WM_INITDIALOG can move and populate it unseen.
	HWND dialog = CreateWindowExA(header.ExStyle, classname, header.Title.c_str(),
		header.Style & ~(DWORD)WS_VISIBLE,
		Dialog_Units_To_Pixels_X(header.X), Dialog_Units_To_Pixels_Y(header.Y),
		Dialog_Units_To_Pixels_X(header.CX), Dialog_Units_To_Pixels_Y(header.CY),
		parent, nullptr, instance, nullptr);

	if (dialog == nullptr) {
		return(nullptr);
	}

	SetWindowLongPtrA(dialog, DWLP_DLGPROC, (LONG_PTR)dialogproc);

	for (unsigned int index = 0; index < header.ItemCount; index++) {
		DialogTemplateItem item;

		if (!Read_Dialog_Item(reader, extended, item)) {
			break;
		}

		if (item.Class.empty()) {
			continue;
		}

		CreateWindowExA(item.ExStyle, item.Class.c_str(), item.Title.c_str(), item.Style | WS_CHILD,
			Dialog_Units_To_Pixels_X(item.X), Dialog_Units_To_Pixels_Y(item.Y),
			Dialog_Units_To_Pixels_X(item.CX), Dialog_Units_To_Pixels_Y(item.CY),
			dialog, (HMENU)(ULONG_PTR)item.ID, instance, nullptr);
	}

	HWND focus = First_Tab_Stop(dialog);

	if (SendMessageA(dialog, WM_INITDIALOG, (WPARAM)focus, initparam) != 0 && focus != nullptr) {
		SetFocus(focus);
	}

	if ((header.Style & WS_VISIBLE) != 0) {
		ShowWindow(dialog, SW_SHOW);
	}

	return(dialog);
}


// The language library is the only resource image, so a template that lives in
// the executable is not found; windlg.cpp carries a fallback for that.
HWND CreateDialogParamA(HINSTANCE instance, LPCSTR templatename, HWND parent, DLGPROC dialogproc,
	LPARAM initparam)
{
	void const * dialogtemplate = Fetch_Resource(templatename, (LPCSTR)RT_DIALOG);
	if (dialogtemplate == nullptr) {
		return(nullptr);
	}

	return(CreateDialogIndirectParamA(instance, (LPCDLGTEMPLATE)dialogtemplate, parent, dialogproc, initparam));
}


INT_PTR DialogBoxParamA(HINSTANCE instance, LPCSTR templatename, HWND parent, DLGPROC dialogproc,
	LPARAM initparam)
{
	HWND dialog = CreateDialogParamA(instance, templatename, parent, dialogproc, initparam);
	if (dialog == nullptr) {
		return(-1);
	}

	ModalDialog modal;
	modal.Window = dialog;
	modal.Result = 0;
	modal.Ended = false;
	_ModalDialogs.push_back(&modal);
	Phase_Register_Layers("dialog", [](void) { return((int)_ModalDialogs.size()); });
	Phase_Changed();

	ShowWindow(dialog, SW_SHOW);
	SetFocus(dialog);

	// The pump waits on the page rather than spinning, or it holds a core for as long as
	// the dialog is up: there is nothing to do between one frame and the next.
	while (!modal.Ended && IsWindow(dialog)) {
		Browser_Service();
		Win32_User_Service();

		MSG message;
		while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
			if (IsDialogMessageA(dialog, &message)) {
				continue;
			}
			TranslateMessage(&message);
			DispatchMessageA(&message);
		}

		Browser_Yield();
	}

	for (unsigned int index = 0; index < _ModalDialogs.size(); index++) {
		if (_ModalDialogs[index] == &modal) {
			_ModalDialogs.erase(_ModalDialogs.begin() + index);
			break;
		}
	}
	Phase_Changed();

	if (IsWindow(dialog)) {
		DestroyWindow(dialog);
	}

	return(modal.Result);
}


BOOL EndDialog(HWND dialog, INT_PTR result)
{
	for (unsigned int index = 0; index < _ModalDialogs.size(); index++) {
		if (_ModalDialogs[index]->Window == dialog) {
			_ModalDialogs[index]->Result = result;
			_ModalDialogs[index]->Ended = true;
			return(TRUE);
		}
	}

	// A modeless dialog has no pump waiting on it, so ending it is closing it.
	return(DestroyWindow(dialog));
}


HWND GetNextDlgTabItem(HWND dialog, HWND control, BOOL previous)
{
	UserWindow * entry = Window_Of(dialog);
	if (entry == nullptr || entry->Children.empty()) {
		return(nullptr);
	}

	// Template order, which the sibling chain holds backwards.
	std::vector<UserWindow *> order(entry->Children.rbegin(), entry->Children.rend());

	int start = -1;
	for (unsigned int index = 0; index < order.size(); index++) {
		if (Handle_Of(order[index]) == control) {
			start = (int)index;
			break;
		}
	}

	int count = (int)order.size();
	int step = (previous != FALSE) ? -1 : 1;

	for (int distance = 1; distance <= count; distance++) {
		int index = ((start + step * distance) % count + count) % count;
		UserWindow * candidate = order[(unsigned int)index];

		if ((candidate->Style & WS_TABSTOP) == 0) continue;
		if (!candidate->Visible || !candidate->Enabled) continue;

		return(Handle_Of(candidate));
	}

	return(nullptr);
}


HWND GetNextDlgGroupItem(HWND, HWND, BOOL) { return(WIN32_STUB((HWND)nullptr)); }


BOOL IsDialogMessageA(HWND dialog, LPMSG message)
{
	if (dialog == nullptr || message == nullptr) {
		return(FALSE);
	}

	if (message->hwnd != dialog && !IsChild(dialog, message->hwnd)) {
		return(FALSE);
	}

	if (message->message == WM_KEYDOWN) {
		LRESULT code = SendMessageA(message->hwnd, WM_GETDLGCODE, 0, 0);

		switch (message->wParam) {
			case VK_TAB:
				if ((code & DLGC_WANTTAB) == 0) {
					HWND next = GetNextDlgTabItem(dialog, message->hwnd,
						Browser_Key_Is_Down(VK_SHIFT) ? TRUE : FALSE);
					if (next != nullptr) {
						SetFocus(next);
					}
					return(TRUE);
				}
				break;

			case VK_RETURN:
				if ((code & DLGC_WANTALLKEYS) == 0) {
					SendMessageA(dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
					return(TRUE);
				}
				break;

			case VK_ESCAPE:
				SendMessageA(dialog, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
				return(TRUE);

			default:
				break;
		}
	}

	TranslateMessage(message);
	DispatchMessageA(message);
	return(TRUE);
}


//------------------------------------------------------------------------------
// The page's input, as messages.
//------------------------------------------------------------------------------


// browser.cpp posts keys to the keyboard buffer, which is the main window's
// keyboard path, so only a window with a parent is fed keys as messages;
// anything else would double them. A mouse message goes wherever it lands.
static bool Takes_Keys(UserWindow const * window)
{
	return(window != nullptr && window->Parent != nullptr && window->Enabled);
}


// Answering WM_GETDLGCODE can move the focus again, so the answer holds only
// while the window still has it.
static bool Wants_Characters(UserWindow * window)
{
	if (!Takes_Keys(window)) {
		return(false);
	}

	LRESULT code = SendMessageA(Handle_Of(window), WM_GETDLGCODE, 0, 0);
	return(((code & DLGC_WANTCHARS) != 0) && (_Focus == window));
}


// A soft keyboard follows the page's focus, so the engine's focus is relayed.
// The hall of fame focuses no control and asks for a keyboard itself.
static void Update_Text_Input(void)
{
	if (Wants_Characters(_Focus)) {
		Browser_Begin_Text_Input();
	} else {
		Browser_End_Text_Input();
	}
}


// The character is posted with no key press around it: a press would be
// translated into a second character, and the dialog's own keys are claimed
// from a press, so a composed return does not stand in for pressing OK.
void Win32_User_Post_Character(char character)
{
	UserWindow * focus = _Focus;

	if (!Wants_Characters(focus)) {
		return;
	}

	// The repeat count TranslateMessage would have carried.
	Queue_Message(Handle_Of(focus), WM_CHAR, (WPARAM)(unsigned char)character, 1);
}


static WPARAM Mouse_Button_Flags(void)
{
	WPARAM flags = 0;

	if (Browser_Key_Is_Down(VK_LBUTTON)) flags |= MK_LBUTTON;
	if (Browser_Key_Is_Down(VK_RBUTTON)) flags |= MK_RBUTTON;
	if (Browser_Key_Is_Down(VK_MBUTTON)) flags |= MK_MBUTTON;

	unsigned short modifiers = Browser_Event_Modifiers();
	if ((modifiers & WWKEY_SHIFT_BIT) != 0) flags |= MK_SHIFT;
	if ((modifiers & WWKEY_CTRL_BIT) != 0) flags |= MK_CONTROL;

	return(flags);
}


static bool Deliver_Mouse(UINT message, WPARAM wparam, POINT screen)
{
	UserWindow * target = Window_Of(GetCapture());
	if (target == nullptr) {
		target = Window_From_Screen_Point(screen);
	}

	if (target == nullptr || !target->Enabled) {
		return(false);
	}

	POINT origin = Screen_Origin(target);
	return(Queue_Message(Handle_Of(target), message, wparam,
		MAKELONG((short)(screen.x - origin.x), (short)(screen.y - origin.y))));
}


// A press and its release arrive within one engine pass, so button state read
// once a pass loses the click; the drain sees each event once. A claimed event
// is not put in the keyboard buffer, because the window it reaches does that.
static bool Consume_Browser_Event(unsigned short key, int x, int y, bool is_mouse, bool is_release)
{
	if (!is_mouse) {
		return(false);
	}

	UINT message;
	WPARAM button;

	switch (key) {
		case VK_LBUTTON:
			message = is_release ? WM_LBUTTONUP : WM_LBUTTONDOWN;
			button = MK_LBUTTON;
			break;

		case VK_RBUTTON:
			message = is_release ? WM_RBUTTONUP : WM_RBUTTONDOWN;
			button = MK_RBUTTON;
			break;

		case VK_MBUTTON:
			message = is_release ? WM_MBUTTONUP : WM_MBUTTONDOWN;
			button = MK_MBUTTON;
			break;

		default:
			return(false);
	}

	// The page reports frame coordinates; a mouse message carries window
	// pixels, which is what msgroute.cpp is written against.
	POINT screen;
	screen.x = x;
	screen.y = y;
	Game_Point_To_Window(screen);

	// The flags are the state the message is delivered in, this button too.
	WPARAM flags = Mouse_Button_Flags();
	if (is_release) {
		flags &= ~button;
	} else {
		flags |= button;
	}

	// A window tracks the cursor from the moves it is sent, so the move to this
	// point precedes the press.
	if (screen.x != _LastMouse.x || screen.y != _LastMouse.y) {
		_LastMouse = screen;
		Deliver_Mouse(WM_MOUSEMOVE, flags, screen);
	}

	return(Deliver_Mouse(message, flags, screen));
}


//------------------------------------------------------------------------------
// The caret.
//------------------------------------------------------------------------------
//
// Nothing here draws: the blink invalidates the owner, and whoever paints the
// window asks Win32_Caret_Visible where the bar goes.

static HWND _CaretOwner = nullptr;
static POINT _CaretPos = {0, 0};
static int _CaretWidth = 0;
static int _CaretHeight = 0;

// Windows counts rather than toggles, and a new caret starts hidden.
static int _CaretHidden = 1;

static DWORD _CaretPhaseAt = 0;
static bool _CaretOn = false;


UINT GetCaretBlinkTime(void)
{
	return(530);
}


static void Invalidate_Caret(void)
{
	if (_CaretOwner == nullptr) return;

	RECT bar;
	bar.left = _CaretPos.x;
	bar.top = _CaretPos.y;
	bar.right = _CaretPos.x + (_CaretWidth > 0 ? _CaretWidth : 1);
	bar.bottom = _CaretPos.y + _CaretHeight;

	InvalidateRect(_CaretOwner, &bar, FALSE);
}


BOOL CreateCaret(HWND window, HBITMAP, int width, int height)
{
	if (window == nullptr) return(FALSE);

	// A width of zero asks for the system's own, which is one pixel here.
	_CaretOwner = window;
	_CaretWidth = (width > 0) ? width : 1;
	_CaretHeight = (height > 0) ? height : 1;
	_CaretHidden = 1;
	_CaretOn = false;
	_CaretPhaseAt = GetTickCount();

	return(TRUE);
}


BOOL DestroyCaret(void)
{
	if (_CaretOwner == nullptr) return(FALSE);

	bool const was_on = _CaretOn;
	HWND const owner = _CaretOwner;

	_CaretOn = false;
	if (was_on) Invalidate_Caret();

	_CaretOwner = nullptr;
	_CaretHidden = 1;
	(void)owner;

	return(TRUE);
}


BOOL SetCaretPos(int x, int y)
{
	if (_CaretOwner == nullptr) return(FALSE);
	if (_CaretPos.x == x && _CaretPos.y == y) return(TRUE);

	if (_CaretOn) Invalidate_Caret();

	_CaretPos.x = x;
	_CaretPos.y = y;

	// A moved caret is shown, so it cannot blink out from under someone typing.
	_CaretPhaseAt = GetTickCount();
	if (_CaretHidden <= 0) {
		_CaretOn = true;
		Invalidate_Caret();
	}

	return(TRUE);
}


BOOL ShowCaret(HWND window)
{
	if (_CaretOwner == nullptr || (window != nullptr && window != _CaretOwner)) return(FALSE);

	if (_CaretHidden > 0) _CaretHidden--;

	if (_CaretHidden == 0) {
		_CaretOn = true;
		_CaretPhaseAt = GetTickCount();
		Invalidate_Caret();
	}

	return(TRUE);
}


BOOL HideCaret(HWND window)
{
	if (_CaretOwner == nullptr || (window != nullptr && window != _CaretOwner)) return(FALSE);

	_CaretHidden++;

	if (_CaretOn) {
		_CaretOn = false;
		Invalidate_Caret();
	}

	return(TRUE);
}


bool Win32_Caret_Visible(HWND window, RECT * where)
{
	if (_CaretOwner == nullptr || window != _CaretOwner || _CaretHidden > 0 || !_CaretOn) {
		return(false);
	}

	if (where != nullptr) {
		where->left = _CaretPos.x;
		where->top = _CaretPos.y;
		where->right = _CaretPos.x + _CaretWidth;
		where->bottom = _CaretPos.y + _CaretHeight;
	}

	return(true);
}


static void Service_Caret(void)
{
	if (_CaretOwner == nullptr || _CaretHidden > 0) return;

	DWORD const now = GetTickCount();
	if (now - _CaretPhaseAt < GetCaretBlinkTime()) return;

	_CaretPhaseAt = now;
	_CaretOn = !_CaretOn;
	Invalidate_Caret();
}


static void Service_Mouse(void)
{
	POINT screen;
	screen.x = Browser_Mouse_X();
	screen.y = Browser_Mouse_Y();
	Game_Point_To_Window(screen);

	if (screen.x == _LastMouse.x && screen.y == _LastMouse.y) {
		return;
	}

	_LastMouse = screen;
	Deliver_Mouse(WM_MOUSEMOVE, Mouse_Button_Flags(), screen);
}


static void Service_Hot_Keys(unsigned short key, unsigned short modifiers)
{
	UINT pressed = 0;
	if ((modifiers & WWKEY_ALT_BIT) != 0) pressed |= MOD_ALT;
	if ((modifiers & WWKEY_CTRL_BIT) != 0) pressed |= MOD_CONTROL;
	if ((modifiers & WWKEY_SHIFT_BIT) != 0) pressed |= MOD_SHIFT;

	for (unsigned int index = 0; index < _HotKeys.size(); index++) {
		if (_HotKeys[index].Key != key || _HotKeys[index].Modifiers != pressed) {
			continue;
		}

		Queue_Message(Handle_Of(_HotKeys[index].Window), WM_HOTKEY_MESSAGE,
			(WPARAM)_HotKeys[index].ID, MAKELONG(pressed, key));
	}
}


static void Service_Keyboard(void)
{
	unsigned short modifiers = Browser_Key_Modifiers();
	bool alt = ((modifiers & WWKEY_ALT_BIT) != 0);

	for (int key = 0; key < 256; key++) {
		bool down = Browser_Key_Is_Down((unsigned short)key);
		if (down == (_LastKeyDown[key] != 0)) {
			continue;
		}

		_LastKeyDown[key] = down ? 1 : 0;

		if (key == VK_LBUTTON || key == VK_RBUTTON || key == VK_MBUTTON) {
			continue;
		}

		if (down) {
			Service_Hot_Keys((unsigned short)key, modifiers);
		}

		if (!Takes_Keys(_Focus)) {
			continue;
		}

		UINT message;
		if (alt) {
			message = down ? WM_SYSKEYDOWN : WM_SYSKEYUP;
		} else {
			message = down ? WM_KEYDOWN : WM_KEYUP;
		}

		// The repeat count, and the transition bits a key release carries.
		LPARAM lparam = down ? 1 : (LPARAM)(1 | (1L << 30) | (1L << 31));

		Queue_Message(Handle_Of(_Focus), message, (WPARAM)key, lparam);
	}
}


void Win32_User_Service(void)
{
	if (!_InputStarted) {
		_InputStarted = true;
		memset(_LastKeyDown, 0, sizeof(_LastKeyDown));
		_LastMouse.x = Browser_Mouse_X();
		_LastMouse.y = Browser_Mouse_Y();
		Game_Point_To_Window(_LastMouse);
		Browser_Set_Event_Hook(Consume_Browser_Event);
	}

	Service_Mouse();
	Service_Keyboard();
	Service_Caret();
}


//------------------------------------------------------------------------------
// The message box.
//------------------------------------------------------------------------------


// confirm() stops the page and a browser may suppress it, so the box is laid
// out in the page and the wait is the engine's yield. Without the yield
// scaffold the question is logged and the box reported cancelled.
int MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT type)
{
	char const * body = (text != nullptr) ? text : "";
	char const * title = (caption != nullptr) ? caption : "OpenTS";

	fprintf(stderr, "OpenTS message box [%s]: %s\n", title, body);
	fflush(stderr);

	if (!Browser_Yield_Is_Available()) {
		return(WIN32_UNSUPPORTED("MessageBox: asking the player without the yield scaffold", IDCANCEL));
	}

	PhaseScope phase("alert", body);

	int cancelled;
	switch (type & 0x0000000FL) {
		case MB_OKCANCEL:			cancelled = IDCANCEL; break;
		case MB_ABORTRETRYIGNORE:	cancelled = IDIGNORE; break;
		case MB_YESNOCANCEL:		cancelled = IDCANCEL; break;
		case MB_YESNO:				cancelled = IDNO; break;
		case MB_RETRYCANCEL:		cancelled = IDCANCEL; break;
		default:					cancelled = IDOK; break;
	}

	int answer = Browser_Message_Box(title, body, (int)(type & 0x0000000FL));
	if (answer == 0) {
		return(WIN32_UNSUPPORTED("MessageBox: asking the player without a host to draw on", cancelled));
	}

	// A box the host took away reports what Windows does for a dismissed box.
	return(answer > 0 ? answer : cancelled);
}


// Everything this block carries beyond MessageBox is a Windows resource or help
// hook a page cannot answer, and a block of another size is refused rather than
// read as if it were this one's.
int MessageBoxIndirectA(MSGBOXPARAMSA const * parameters)
{
	if (parameters == nullptr || parameters->cbSize != sizeof(MSGBOXPARAMSA)) {
		return(WIN32_UNSUPPORTED("MessageBoxIndirect: a parameter block of another shape", 0));
	}

	return(MessageBoxA(parameters->hwndOwner, parameters->lpszText, parameters->lpszCaption,
		parameters->dwStyle));
}


//------------------------------------------------------------------------------
// Keyboard state.
//------------------------------------------------------------------------------


SHORT GetKeyState(int) { return(WIN32_STUB(0)); }
SHORT GetAsyncKeyState(int) { return(WIN32_STUB(0)); }
BOOL GetKeyboardState(PBYTE) { return(WIN32_STUB(FALSE)); }
UINT MapVirtualKeyA(UINT, UINT) { return(WIN32_STUB(0)); }
int ToAscii(UINT, UINT, BYTE const *, LPWORD, UINT) { return(WIN32_STUB(0)); }
int GetKeyNameTextA(LONG, LPSTR buffer, int size) { if (buffer != nullptr && size > 0) buffer[0] = '\0'; return(WIN32_STUB(0)); }
int TranslateAcceleratorA(HWND, HACCEL, LPMSG) { return(WIN32_STUB(0)); }


//------------------------------------------------------------------------------
// Menus.
//------------------------------------------------------------------------------


// No window here has a menu bar, so NULL is the answer rather than a gap.
HMENU GetMenu(HWND) { return(nullptr); }
HMENU GetSystemMenu(HWND, BOOL) { return(WIN32_STUB((HMENU)nullptr)); }
BOOL DeleteMenu(HMENU, UINT, UINT) { return(WIN32_STUB(FALSE)); }
BOOL EnableMenuItem(HMENU, UINT, UINT) { return(WIN32_STUB(FALSE)); }


//------------------------------------------------------------------------------
// Window scroll bars.
//------------------------------------------------------------------------------


// A window's own scroll information, not the control's from Scroll_Bar_Proc.
BOOL GetScrollInfo(HWND, int, LPSCROLLINFO) { return(WIN32_STUB(FALSE)); }
int SetScrollInfo(HWND, int, LPCSCROLLINFO, BOOL) { return(WIN32_STUB(0)); }


//------------------------------------------------------------------------------
// Context help.
//------------------------------------------------------------------------------


DWORD GetWindowContextHelpId(HWND) { return(WIN32_STUB(0)); }
BOOL WinHelpA(HWND, LPCSTR, UINT, ULONG_PTR) { return(WIN32_STUB(FALSE)); }

#endif	// OPENTS_WIN32_SUBSTITUTE
