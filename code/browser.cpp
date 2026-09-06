/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// A page function that does not return starves input, layout, and paint, so
// every engine wait reaches Browser_Yield through Windows_Message_Handler or
// Call_Back. With OPENTS_WASM_JSPI the yield suspends the stack against an
// animation frame; without it the wait is only counted and the page freezes.

#include "always.h"

#include "browser.h"

#if defined(__EMSCRIPTEN__)

#include "_keyboar.h"
#include "_map.h"
#include "_rect.h"
#include "_tactica.h"
#include "dbgprint.h"
#include "conquer.h"
#include "globals.h"
#include "offline.h"
#include "httpsource.h"
#include "keyboard.h"
#include "misc.h"
#include "movies.h"
#include "msgroute.h"
#include "rect.h"
#include "surface.h"
#include "tactical.h"
#include "video.h"
#include "vidscale.h"
#include "win.h"
#include "win32user.h"
#include "wwmouse.h"

#include "facing.hh"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <cmath>
#include <cstring>


// Must match the canvas id in the shell page.
static char const * const CANVAS_SELECTOR = "#canvas";

// The shortest gap between two returns to the page; the engine's own timers
// decide the frame rate.
static const double YIELD_INTERVAL = 1000.0 / 60.0;

// Page callbacks queue here and Browser_Service drains in engine context, so
// a callback never writes the keyboard buffer part way through a read of it.
static const int EVENT_QUEUE_SIZE = 128;

struct BrowserEvent
{
	unsigned short Key;
	short X;
	short Y;
	bool IsMouse;
	bool IsRelease;

	// A touch during a movie is delivered as escape rather than as a button.
	bool IsTouch;

	// The modifiers held when the page reported it; the engine acts on it
	// later.
	unsigned short Modifiers;
};

static BrowserEvent _Events[EVENT_QUEUE_SIZE];
static int _EventHead = 0;
static int _EventTail = 0;

static BrowserEventHook _EventHook = nullptr;

static bool _Initialized = false;

// Only main is a promising export, so a suspend is only legal inside it;
// static initialization must return normally.
static bool _EngineEntered = false;

static int _CanvasWidth = 0;
static int _CanvasHeight = 0;
static int _CanvasCSSWidth = 0;
static int _CanvasCSSHeight = 0;

static int _ScreenWidth = 0;
static int _ScreenHeight = 0;

static BrowserDisplayPolicy _DisplayPolicy = BROWSER_DISPLAY_NATIVE;
static int _DisplayWidth = 0;
static int _DisplayHeight = 0;

static bool _Hidden = false;

static int _MouseX = 0;
static int _MouseY = 0;

// The engine reads a pointer held against an edge as a request to keep
// scrolling, so the position stays pinned to the edge the pointer left through
// until it returns or the page loses the keyboard.
static bool _MouseOutside = false;

// Follows whichever of mouse and finger reported last; a finger leaves no
// resting position for the edge scroll, tooltips, or placement cursor to read.
static bool _MouseHovering = false;

static unsigned short _Modifiers = 0;

// The modifiers of the event being drained, presented while its window
// message is dispatched.
static unsigned short _EventModifiers = 0;

static unsigned char _KeyDown[256];

// The character each key produced when it went down, per shift state; only
// the page knows the keyboard layout, so To_ASCII reads these back.
static char _Ascii[256];
static char _ShiftedAscii[256];

static double _LastYield = 0.0;
static unsigned int _FrameSerial = 0;
static unsigned int _BlockingWaits = 0;

#if defined(OPENTS_WASM_JSPI)

// A hidden tab is given no animation frames, so it falls back to a timer; a
// lockstep peer that stops stalls everyone else. A page holding the gate
// keeps the engine parked here until it hands out a frame.
EM_ASYNC_JS(void, Browser_Await_Frame, (int hidden), {
	if (hidden || typeof requestAnimationFrame !== "function") {
		await new Promise(function (resolve) { setTimeout(resolve, 0); });
	} else {
		await new Promise(function (resolve) { requestAnimationFrame(resolve); });
	}
	var gate = Module.OpenTS_FrameGate;
	if (gate && gate.held) {
		await gate.take();
	}
});

#endif


char const * Browser_Canvas_Selector(void)
{
	return(CANVAS_SELECTOR);
}


static void Read_Page_Configuration(void)
{
	int scaled = EM_ASM_INT({
		var value = (new URLSearchParams(location.search).get("display") || "").toLowerCase();
		return (value === "" || value === "native" || value === "window") ? 0 : 1;
	});

	_DisplayPolicy = (scaled != 0) ? BROWSER_DISPLAY_SCALED : BROWSER_DISPLAY_NATIVE;

	_DisplayWidth = EM_ASM_INT({
		var parts = /^(\\d+)x(\\d+)$/.exec((new URLSearchParams(location.search).get("display") || "").toLowerCase());
		return parts ? parseInt(parts[1], 10) : 0;
	});

	_DisplayHeight = EM_ASM_INT({
		var parts = /^(\\d+)x(\\d+)$/.exec((new URLSearchParams(location.search).get("display") || "").toLowerCase());
		return parts ? parseInt(parts[2], 10) : 0;
	});

	_ScreenWidth = EM_ASM_INT({ return (window.screen && window.screen.width) ? (window.screen.width | 0) : 0; });
	_ScreenHeight = EM_ASM_INT({ return (window.screen && window.screen.height) ? (window.screen.height | 0) : 0; });
}


// Matches the drawing buffer to the laid out box in device pixels, so the
// page does not composite through a second rescale; true when the size changed.
static bool Measure_Canvas(void)
{
	double csswidth = 0.0;
	double cssheight = 0.0;

	if (emscripten_get_element_css_size(CANVAS_SELECTOR, &csswidth, &cssheight) != EMSCRIPTEN_RESULT_SUCCESS) {
		return(false);
	}

	double ratio = emscripten_get_device_pixel_ratio();

	int cssw = (int)(csswidth + 0.5);
	int cssh = (int)(cssheight + 0.5);
	int width = (int)(csswidth * ratio + 0.5);
	int height = (int)(cssheight * ratio + 0.5);

	if (cssw <= 0 || cssh <= 0 || width <= 0 || height <= 0) {
		return(false);
	}

	if (width == _CanvasWidth && height == _CanvasHeight && cssw == _CanvasCSSWidth && cssh == _CanvasCSSHeight) {
		return(false);
	}

	_CanvasWidth = width;
	_CanvasHeight = height;
	_CanvasCSSWidth = cssw;
	_CanvasCSSHeight = cssh;

	emscripten_set_canvas_element_size(CANVAS_SELECTOR, width, height);
	return(true);
}


// Runs from a page callback while the engine may be suspended mid wait, so it
// touches no engine state; a full queue drops the event.
static void Queue_Event(BrowserEvent const & event)
{
	int next = (_EventTail + 1) % EVENT_QUEUE_SIZE;

	if (next == _EventHead) {
		return;
	}

	_Events[_EventTail] = event;
	_Events[_EventTail].Modifiers = _Modifiers;
	_EventTail = next;
}


// The DOM code names the physical key rather than the character, which is
// what the hotkey tables want; VK_NONE for a key with no counterpart.
static unsigned short Virtual_Key_For_Code(char const * code)
{
	static struct {
		char const * Code;
		unsigned short Key;
	} const _named[] = {
		{ "Escape", VK_ESCAPE },		{ "Backspace", VK_BACK },		{ "Tab", VK_TAB },
		{ "Enter", VK_RETURN },			{ "NumpadEnter", VK_RETURN },	{ "Space", VK_SPACE },
		{ "ShiftLeft", VK_SHIFT },		{ "ShiftRight", VK_SHIFT },
		{ "ControlLeft", VK_CONTROL },	{ "ControlRight", VK_CONTROL },
		{ "AltLeft", VK_MENU },			{ "AltRight", VK_MENU },
		{ "CapsLock", VK_CAPITAL },		{ "NumLock", VK_NUMLOCK },		{ "ScrollLock", VK_SCROLL },
		{ "Pause", VK_PAUSE },			{ "PrintScreen", VK_SNAPSHOT },
		{ "Insert", VK_INSERT },		{ "Delete", VK_DELETE },
		{ "Home", VK_HOME },			{ "End", VK_END },
		{ "PageUp", VK_PRIOR },			{ "PageDown", VK_NEXT },
		{ "ArrowLeft", VK_LEFT },		{ "ArrowUp", VK_UP },
		{ "ArrowRight", VK_RIGHT },		{ "ArrowDown", VK_DOWN },
		{ "NumpadMultiply", VK_MULTIPLY },	{ "NumpadAdd", VK_ADD },
		{ "NumpadSubtract", VK_SUBTRACT },	{ "NumpadDecimal", VK_DECIMAL },
		{ "NumpadDivide", VK_DIVIDE },
		{ "Minus", VK_NONE_BD },		{ "Equal", VK_NONE_BB },
		{ "BracketLeft", VK_NONE_DB },	{ "BracketRight", VK_NONE_DD },
		{ "Backslash", VK_NONE_DC },	{ "Semicolon", VK_NONE_BA },
		{ "Quote", VK_NONE_DE },		{ "Backquote", VK_NONE_C0 },
		{ "Comma", VK_NONE_BC },		{ "Period", VK_NONE_BE },
		{ "Slash", VK_NONE_BF },
		{ "ContextMenu", VK_NONE_5D },
		{ "MetaLeft", VK_NONE_5B },		{ "MetaRight", VK_NONE_5C },
	};

	if (code == nullptr || code[0] == '\0') {
		return(VK_NONE);
	}

	if (strncmp(code, "Key", 3) == 0 && code[3] >= 'A' && code[3] <= 'Z' && code[4] == '\0') {
		return((unsigned short)(VK_A + (code[3] - 'A')));
	}

	if (strncmp(code, "Digit", 5) == 0 && code[5] >= '0' && code[5] <= '9' && code[6] == '\0') {
		return((unsigned short)(VK_0 + (code[5] - '0')));
	}

	if (strncmp(code, "Numpad", 6) == 0 && code[6] >= '0' && code[6] <= '9' && code[7] == '\0') {
		return((unsigned short)(VK_NUMPAD0 + (code[6] - '0')));
	}

	if (code[0] == 'F' && code[1] >= '1' && code[1] <= '9') {
		int number = code[1] - '0';
		if (code[2] >= '0' && code[2] <= '9' && code[3] == '\0') {
			number = number * 10 + (code[2] - '0');
		} else if (code[2] != '\0') {
			number = 0;
		}
		if (number >= 1 && number <= 24) {
			return((unsigned short)(VK_F1 + number - 1));
		}
	}

	for (unsigned index = 0; index < sizeof(_named) / sizeof(_named[0]); index++) {
		if (strcmp(code, _named[index].Code) == 0) {
			return(_named[index].Key);
		}
	}

	return(VK_NONE);
}


static void Note_Modifiers(bool shift, bool ctrl, bool alt)
{
	_Modifiers = 0;
	if (shift) _Modifiers |= WWKEY_SHIFT_BIT;
	if (ctrl) _Modifiers |= WWKEY_CTRL_BIT;
	if (alt) _Modifiers |= WWKEY_ALT_BIT;

	Browser_Apply_Modifiers(_Modifiers);
}


// The page reports CSS pixels and the drawing buffer is in device pixels; the
// engine's own scaling conversion then finishes the job.
static void Canvas_Point_To_Game(double cssx, double cssy, int & x, int & y)
{
	double ratio = emscripten_get_device_pixel_ratio();

	POINT point;
	point.x = (LONG)(cssx * ratio + 0.5);
	point.y = (LONG)(cssy * ratio + 0.5);

	Window_Point_To_Game(point);
	Clamp_To_Game(point);

	x = (int)point.x;
	y = (int)point.y;
}


static EM_BOOL Key_Callback(int type, EmscriptenKeyboardEvent const * event, void *)
{
	Note_Modifiers(event->shiftKey != 0, event->ctrlKey != 0, event->altKey != 0);

	unsigned short key = Virtual_Key_For_Code(event->code);
	if (key == VK_NONE) {
		return(EM_FALSE);
	}

	// A one character key value is the layout's answer for a printable key; the
	// named keys below carry a character on Windows as well.
	char character = '\0';

	if (event->key[0] != '\0' && event->key[1] == '\0') {
		character = event->key[0];
	} else {
		switch (key & 0xFF) {
			case VK_RETURN:	character = '\r';	break;
			case VK_BACK:	character = '\b';	break;
			case VK_TAB:	character = '\t';	break;
			case VK_ESCAPE:	character = 27;		break;
			default:							break;
		}
	}

	if (character != '\0') {
		if ((_Modifiers & WWKEY_SHIFT_BIT) != 0) {
			_ShiftedAscii[key & 0xFF] = character;
		} else {
			_Ascii[key & 0xFF] = character;
		}
	}

	bool release = (type == EMSCRIPTEN_EVENT_KEYUP);

	// The engine does not want key repeats.
	if (!release && event->repeat != 0) {
		return(EM_TRUE);
	}

	_KeyDown[key & 0xFF] = release ? 0 : 1;

	BrowserEvent queued;
	queued.Key = key;
	queued.X = 0;
	queued.Y = 0;
	queued.IsMouse = false;
	queued.IsRelease = release;
	queued.IsTouch = false;
	Queue_Event(queued);

	// Unclaimed, Tab, the function keys, and the arrows scroll or navigate the
	// page.
	return(EM_TRUE);
}


static EM_BOOL Mouse_Callback(int type, EmscriptenMouseEvent const * event, void *)
{
	Note_Modifiers(event->shiftKey != 0, event->ctrlKey != 0, event->altKey != 0);

	// A mouse rests wherever it stopped, so it restores a hover a finger took
	// away.
	_MouseHovering = true;

	Canvas_Point_To_Game(event->targetX, event->targetY, _MouseX, _MouseY);

	if (type == EMSCRIPTEN_EVENT_MOUSEMOVE) {
		return(EM_TRUE);
	}

	unsigned short key;
	switch (event->button) {
		case 1:
			key = VK_MBUTTON;
			break;

		case 2:
			key = VK_RBUTTON;
			break;

		default:
			key = VK_LBUTTON;
			break;
	}

	bool release = (type == EMSCRIPTEN_EVENT_MOUSEUP);
	_KeyDown[key] = release ? 0 : 1;

	BrowserEvent queued;
	queued.Key = key;
	queued.X = (short)_MouseX;
	queued.Y = (short)_MouseY;
	queued.IsMouse = true;
	queued.IsRelease = release;
	queued.IsTouch = false;
	Queue_Event(queued);

	// WM_LBUTTONDBLCLK reaches the engine as a second press and release.
	if (type == EMSCRIPTEN_EVENT_DBLCLICK) {
		queued.IsRelease = true;
		Queue_Event(queued);
	}

	return(EM_TRUE);
}


// The page reports distance travelled rather than notches, so scrolls
// accumulate here and Service_Wheel steps the build list once per notch's
// worth, or pans the map when the pointer is over the tactical view.

// One notch in each delta mode a browser may report, so a whole turn of a
// wheel leaves nothing over.
static const double WHEEL_NOTCH = 100.0;
static const double WHEEL_LINE = WHEEL_NOTCH / 3.0;
static const double WHEEL_PAGE = WHEEL_NOTCH * 3.0;

// WHEEL_DELTA; only the sign is read.
static const int WHEEL_MESSAGE_DELTA = 120;

// Travel not yet reported, in CSS pixels; a scroll that moves onto or off
// the tactical view starts afresh.
static double _WheelPendingX = 0.0;
static double _WheelPendingY = 0.0;
static bool _WheelOverTactical = false;

static bool Touch_Tactical_Ready(void);
static void Request_Map_Pan(double cssdx, double cssdy);


static EM_BOOL Wheel_Callback(int, EmscriptenWheelEvent const * event, void *)
{
	Note_Modifiers(event->mouse.shiftKey != 0, event->mouse.ctrlKey != 0, event->mouse.altKey != 0);

	// A wheel implies a pointer resting under it.
	_MouseHovering = true;

	Canvas_Point_To_Game(event->mouse.targetX, event->mouse.targetY, _MouseX, _MouseY);

	double scale = 1.0;
	switch (event->deltaMode) {
		case DOM_DELTA_LINE:	scale = WHEEL_LINE;		break;
		case DOM_DELTA_PAGE:	scale = WHEEL_PAGE;		break;
		default:											break;
	}

	double travelx = event->deltaX * scale;
	double travely = event->deltaY * scale;

	bool tactical = TacticalRect.Is_Point_Within(Point2D(_MouseX, _MouseY));
	if (tactical != _WheelOverTactical) {
		_WheelOverTactical = tactical;
		_WheelPendingX = 0.0;
		_WheelPendingY = 0.0;
	}

	// A reversal discards a remainder too small to have moved anything.
	if ((travelx > 0.0 && _WheelPendingX < 0.0) || (travelx < 0.0 && _WheelPendingX > 0.0)) {
		_WheelPendingX = 0.0;
	}
	if ((travely > 0.0 && _WheelPendingY < 0.0) || (travely < 0.0 && _WheelPendingY > 0.0)) {
		_WheelPendingY = 0.0;
	}

	_WheelPendingX += travelx;
	_WheelPendingY += travely;
	return(EM_TRUE);
}


// The map takes the travel whole; the build list takes it a notch at a time as
// WM_MOUSEWHEEL, which msgroute.cpp expects in screen coordinates and posted to
// the main window so that routing picks the window under it.
static void Service_Wheel(void)
{
	if (MainWindow == NULL) {
		_WheelPendingX = 0.0;
		_WheelPendingY = 0.0;
		return;
	}

	if (_WheelPendingX != 0.0 || _WheelPendingY != 0.0) {
		if (Touch_Tactical_Ready() && TacticalRect.Is_Point_Within(Point2D(_MouseX, _MouseY))) {
			Request_Map_Pan(-_WheelPendingX, -_WheelPendingY);
			_WheelPendingX = 0.0;
			_WheelPendingY = 0.0;
			return;
		}
	}

	// The build list has no second axis.
	_WheelPendingX = 0.0;

	while (_WheelPendingY >= WHEEL_NOTCH || _WheelPendingY <= -WHEEL_NOTCH) {

		bool away = (_WheelPendingY > 0.0);
		_WheelPendingY += away ? -WHEEL_NOTCH : WHEEL_NOTCH;

		// A page counts scrolling away from the player as positive and a window
		// message counts it as negative.
		short delta = (short)(away ? -WHEEL_MESSAGE_DELTA : WHEEL_MESSAGE_DELTA);

		POINT screen;
		screen.x = _MouseX;
		screen.y = _MouseY;
		Game_Point_To_Window(screen);

		PostMessage(MainWindow, WM_MOUSEWHEEL, MAKEWPARAM(0, (unsigned short)delta),
			MAKELPARAM((short)screen.x, (short)screen.y));
	}
}


// A soft keyboard appears only while a focusable element holds the focus, so
// an off picture input is focused while the engine waits on typed text. Keys
// still reach the window listener; only characters typed without a key event
// are read off the element, in engine context.

static bool _TextInputWanted = false;


static void Focus_Text_Input(void)
{
	EM_ASM({
		var el = document.getElementById("opents-text");

		if (!el) {
			el = document.createElement("input");
			el.id = "opents-text";
			el.type = "text";
			el.opentsQueue = [];
			el.opentsPrev = "";
			el.setAttribute("autocomplete", "off");
			el.setAttribute("autocorrect", "off");
			el.setAttribute("autocapitalize", "off");
			el.setAttribute("spellcheck", "false");
			/* "done" asks Android for a key that only dismisses the keyboard,
			   which the page never hears about; "go" asks for one that submits
			   the form. */
			el.setAttribute("enterkeyhint", "go");
			el.setAttribute("aria-hidden", "true");

			/* Displayed but out of the picture: an undisplayed element cannot
			   focus. */
			el.setAttribute("style", "position:fixed;left:0;top:0;width:1px;height:1px;" +
				"opacity:0;border:0;padding:0;margin:0;background:transparent;" +
				"color:transparent;caret-color:transparent;z-index:-1;");

			/* A soft keyboard's action key carries an empty code, so the
			   window's key handler never sees it; a physical return is only
			   noted here. */
			el.addEventListener("keydown", function (e) {
				if (e.key !== "Enter" && e.keyCode !== 13) {
					return;
				}

				if (e.code === "Enter" || e.code === "NumpadEnter") {
					el.opentsPhysicalReturn = Date.now();
					return;
				}

				el.opentsSoftReturn = Date.now();
				el.opentsQueue.push(13);
				e.preventDefault();
			});

			/* The field is compared with what it held rather than emptied,
			   because emptying it breaks a composition the keyboard is still
			   editing. */
			el.opentsSync = function () {
				var value = el.value;
				var prev = el.opentsPrev;

				var common = 0;
				while (common < value.length && common < prev.length
					&& value.charCodeAt(common) === prev.charCodeAt(common)) {
					common++;
				}

				for (var back = prev.length; back > common; back--) {
					el.opentsQueue.push(8);
				}
				for (var index = common; index < value.length; index++) {
					el.opentsQueue.push(value.charCodeAt(index));
				}

				el.opentsPrev = value;

				/* Trimmed from the front and never to nothing: a backspace is
				   only ever reported as text going missing. */
				if (value.length > 64) {
					var kept = value.slice(-32);
					el.value = kept;
					el.opentsPrev = kept;
				}
			};

			el.addEventListener("input", function () { el.opentsSync(); });
			el.addEventListener("compositionend", function () { el.opentsSync(); });

			/* On Android the action key is only a form submit: no key event
			   and no line break. */
			var form = document.createElement("form");
			form.id = "opents-text-form";
			form.setAttribute("action", "#");
			form.setAttribute("style", "position:fixed;left:0;top:0;width:1px;height:1px;" +
				"opacity:0;z-index:-1;");

			/* Skipped where a return was just accounted for, so one press is
			   never two. */
			form.addEventListener("submit", function (e) {
				e.preventDefault();

				var now = Date.now();
				if (now - (el.opentsPhysicalReturn || 0) < 200) return;
				if (now - (el.opentsSoftReturn || 0) < 200) return;

				el.opentsSoftReturn = now;
				el.opentsQueue.push(13);
			});

			form.appendChild(el);
			document.body.appendChild(form);
		}

		el.value = "";
		el.opentsPrev = "";
		try {
			el.focus({ preventScroll: true });
		} catch (e) {
			el.focus();
		}
	});
}


static int Fetch_Text_Character(void)
{
	return(EM_ASM_INT({
		var el = document.getElementById("opents-text");
		if (!el || !el.opentsQueue || el.opentsQueue.length === 0) {
			return 0;
		}
		return el.opentsQueue.shift() | 0;
	}));
}


// To_ASCII reads a table written as a key went down, so a character with no
// key of its own borrows the US layout key that produces it; VK_NONE otherwise.
static unsigned short Virtual_Key_For_Character(int character)
{
	if (character >= 'a' && character <= 'z') return((unsigned short)(VK_A + (character - 'a')));
	if (character >= 'A' && character <= 'Z') return((unsigned short)(VK_A + (character - 'A')));
	if (character >= '0' && character <= '9') return((unsigned short)(VK_0 + (character - '0')));

	switch (character) {
		case ' ':	return(VK_SPACE);
		case '\r':	return(VK_RETURN);
		case '\n':	return(VK_RETURN);
		case '\b':	return(VK_BACK);
		case '\t':	return(VK_TAB);
		case 27:	return(VK_ESCAPE);
		case '-':	return(VK_NONE_BD);
		case '=':	return(VK_NONE_BB);
		case '[':	return(VK_NONE_DB);
		case ']':	return(VK_NONE_DD);
		case '\\':	return(VK_NONE_DC);
		case ';':	return(VK_NONE_BA);
		case '\'':	return(VK_NONE_DE);
		case '`':	return(VK_NONE_C0);
		case ',':	return(VK_NONE_BC);
		case '.':	return(VK_NONE_BE);
		case '/':	return(VK_NONE_BF);
		default:	break;
	}

	return(VK_NONE);
}


static void Service_Text_Input(void)
{
	if (!_TextInputWanted) {
		return;
	}

	for (int character = Fetch_Text_Character(); character != 0; character = Fetch_Text_Character()) {

		// The queue below reaches no dialog control, so a field is handed the
		// character directly.
		Win32_User_Post_Character((char)character);

		unsigned short key = Virtual_Key_For_Character(character);
		if (key == VK_NONE) {
			continue;
		}

		// Recorded under both shift states because the key is only a carrier.
		_Ascii[key & 0xFF] = (char)character;
		_ShiftedAscii[key & 0xFF] = (char)character;

		BrowserEvent queued;
		queued.Key = key;
		queued.X = 0;
		queued.Y = 0;
		queued.IsMouse = false;
		queued.IsRelease = false;
		queued.IsTouch = false;
		Queue_Event(queued);

		queued.IsRelease = true;
		Queue_Event(queued);
	}
}


// One finger is the left button and two fingers are the view; where the finger
// is decides nothing, and the engine answers as it would to a mouse. The
// callbacks run while the engine may be suspended, so they only write scalars
// and queue events for Browser_Service. The manual page on touch controls
// owns the gestures.

// In CSS pixels and milliseconds, whatever the display's pixel ratio.
static const double TOUCH_SLOP = 10.0;
static const double TOUCH_HOLD = 450.0;

// What a rest waits when something selectable is right under it, and what it waits when
// there is only something within reach of a grown radius. Such a press has little else it
// could mean, so it does not wait out the time an ambiguous one has to.
static const double TOUCH_HOLD_NEAR = 200.0;
static const double TOUCH_HOLD_FAR = 350.0;

// When the rest armed, and where the hand is now, so a still hand grows the reach and a
// moving one sizes it.
static double _RadiusArmedAt = 0.0;
static int _RadiusHandX = 0;
static int _RadiusHandY = 0;
static bool _RadiusDragging = false;

// Set once a rest has been answered without arming, so it is not answered again and the lift
// that ends it does not also land as a click.
static bool _HoldSpent = false;

enum BrowserGesture {
	GESTURE_NONE,		// Nothing is on the glass.
	GESTURE_UNDECIDED,	// One finger down, not yet travelled or rested enough.
	GESTURE_DRAG,		// One finger is carrying the left button.
	GESTURE_FLICK,		// One finger is scrolling the build list.
	GESTURE_PAN,		// One finger is carrying the view under it.
	GESTURE_RADIUS,		// One finger rested, and is now sizing a selection.
	GESTURE_MULTI,		// Two fingers are moving the view.
	GESTURE_SPENT,		// The gesture is over, but a finger is still down.
};

static BrowserGesture _Gesture = GESTURE_NONE;
static long _GestureID = 0;
static double _GestureTime = 0.0;
static double _GestureStartX = 0.0;
static double _GestureStartY = 0.0;
static double _GestureLastX = 0.0;
static double _GestureLastY = 0.0;

// A gesture presses the left button at most once and releases it at most once.
static bool _GesturePressed = false;

static double _MultiTime = 0.0;
static double _MultiStartX = 0.0;
static double _MultiStartY = 0.0;
static double _MultiLastX = 0.0;
static double _MultiLastY = 0.0;
static bool _MultiMoved = false;

// Carried as a fraction so a slow drag does not lose sub pixel travel.
static double _PanPendingX = 0.0;
static double _PanPendingY = 0.0;

// Settled when the gesture starts, so a finger that wanders keeps scrolling
// the strip it began on.
static int _FlickColumn = -1;
static double _FlickPendingY = 0.0;



static void Canvas_Delta_To_Game(double cssdx, double cssdy, double & dx, double & dy)
{
	double ratio = emscripten_get_device_pixel_ratio();

	dx = cssdx * ratio;
	dy = cssdy * ratio;

	VideoScaleInfo const & scale = Video_Get_Scale_Info();
	if (scale.DestWidth > 0 && scale.DestHeight > 0) {
		dx = dx * (double)scale.GameWidth / (double)scale.DestWidth;
		dy = dy * (double)scale.GameHeight / (double)scale.DestHeight;
	}
}


// Decides whether a right button means anything; a mission whose input the
// engine is dropping still counts, because the engine drops the button too.
static bool Touch_Scenario_On_Screen(void)
{
	return(TacticalMap != nullptr && TacticalActive && ScenarioActive && GameActive
		&& !Movie_Is_Playing());
}


// A pan reaches the map directly rather than through messages the engine
// drops, so IgnoreInput is checked here the way ScrollClass checks it.
static bool Touch_Tactical_Ready(void)
{
	return(Touch_Scenario_On_Screen() && !IgnoreInput);
}


// True for a front end control, which draws itself pressed until the release;
// the main window is excluded because a press on the world is already an
// action. Uses the hit test the mouse messages are routed by.
static bool Touch_Control_Tracks_Press(int x, int y)
{
	if (MainWindow == NULL) {
		return(false);
	}

	POINT point;
	point.x = x;
	point.y = y;

	HWND window = Window_From_Logical_Point(point);
	return(window != NULL && window != MainWindow);
}


static void Queue_Touch_Button(unsigned short key, int x, int y, bool release)
{
	BrowserEvent queued;
	queued.Key = key;
	queued.X = (short)x;
	queued.Y = (short)y;
	queued.IsMouse = true;
	queued.IsRelease = release;
	queued.IsTouch = true;
	Queue_Event(queued);
}


static void Queue_Touch_Click(unsigned short key, int x, int y)
{
	Queue_Touch_Button(key, x, y, false);
	Queue_Touch_Button(key, x, y, true);
}


// Two fingers and a scroll over the tactical view both arrive here, so both
// move the map under the same gate.
static void Request_Map_Pan(double cssdx, double cssdy)
{
	double dx;
	double dy;
	Canvas_Delta_To_Game(cssdx, cssdy, dx, dy);

	_PanPendingX += dx;
	_PanPendingY += dy;
}


// No inertia: a pan that carries on after the finger has gone cannot be
// stopped over a unit.
static void Touch_Service_Pan(void)
{
	if (_PanPendingX == 0.0 && _PanPendingY == 0.0) {
		return;
	}

	if (!Touch_Tactical_Ready()) {
		_PanPendingX = 0.0;
		_PanPendingY = 0.0;
		return;
	}

	int stepx = (int)_PanPendingX;
	int stepy = (int)_PanPendingY;

	_PanPendingX -= (double)stepx;
	_PanPendingY -= (double)stepy;

	if (stepx > 0) {
		TacticalMap->Scroll_Map(FACING_W, stepx);
	} else if (stepx < 0) {
		TacticalMap->Scroll_Map(FACING_E, -stepx);
	}

	if (stepy > 0) {
		TacticalMap->Scroll_Map(FACING_N, stepy);
	} else if (stepy < 0) {
		TacticalMap->Scroll_Map(FACING_S, -stepy);
	}
}


// A flick is confined to a strip's own slots and to a point no control took
// the press on, so a slider drawn over the sidebar stays draggable; -1
// elsewhere.
static int Touch_Flick_Column(int x, int y)
{
	if (_GesturePressed) {
		return(-1);
	}

	return(Map.Column_At(Point2D(x, y)));
}


// A finger scrolls only the strip it is on, as the strips' own arrows do; the
// wheel scrolls both.
static void Touch_Service_Flick(void)
{
	if (_FlickColumn < 0) {
		return;
	}

	while (_FlickPendingY >= WHEEL_NOTCH || _FlickPendingY <= -WHEEL_NOTCH) {
		bool const away = (_FlickPendingY > 0.0);
		_FlickPendingY += away ? -WHEEL_NOTCH : WHEEL_NOTCH;

		Map.SidebarClass::Scroll(away, _FlickColumn);
	}
}


// Placed where the finger came down: the corner a rubber band anchors at and
// the control a drag started on.
static void Touch_Press(void)
{
	if (_GesturePressed) {
		return;
	}

	int x;
	int y;
	Canvas_Point_To_Game(_GestureStartX, _GestureStartY, x, y);
	_MouseX = x;
	_MouseY = y;

	_KeyDown[VK_LBUTTON] = 1;
	Queue_Touch_Button(VK_LBUTTON, x, y, false);
	_GesturePressed = true;
}


// A resting finger reports nothing, so the timer is read here and the right
// button goes out while the finger is still down. Only a mission on screen,
// and not a control that took the press, spends a hold on it.
// The ground under a screen point, resolved the way the engine resolves a click: through the
// terrain rather than by flat projection, so a point over raised ground answers with the
// cell that is drawn there instead of the one behind it.
static bool Touch_Ground_Under(int screenx, int screeny, Coord & coord)
{
	Point2D pixel(screenx - TacticalRect.X, screeny - TacticalRect.Y);

	Cell cell;
	Coord flat;
	ObjectClass * object = nullptr;
	bool fog = false;
	bool shadow = false;

	if (!Map.Resolve_Point(pixel, cell, flat, object, fog, shadow)) {
		return(false);
	}

	// The cell is the one the player sees under the finger; the coordinate beside it is a
	// flat inverse projection that never puts back the pixels a raised tile was lifted by,
	// so it names ground to the north of what was touched.
	coord = Coord(cell);
	return(true);
}


// A hand that rests on without moving keeps taking in ground, so a large group does not
// have to be reached for; dragging outruns it whenever the hand is quicker.
static double const RADIUS_GROWTH_PER_SECOND = CELL_LEPTON_W * 2.5;

// How near the press something selectable has to stand for a rest to arm a selection at all.
// Wide enough to reach a loose group pressed at its edge, since a radius that arms over
// nothing costs only a gesture while one that refuses to arm costs the selection.
static int const HOLD_PROBE_REACH = CELL_LEPTON_W * 5;

// How near the press something has to stand for the rest to arm on the shorter wait.
static int const HOLD_NEAR_REACH = (CELL_LEPTON_W * 3) / 2;


static void Touch_Service_Hold(void)
{
	if (_Gesture == GESTURE_RADIUS) {

		// A hand that has taken hold of the reach keeps it: growing under a drag would
		// fight the hand for the same number.
		if (_RadiusDragging) {
			Coord hand;

			if (Touch_Ground_Under(_RadiusHandX, _RadiusHandY, hand)) {
				TacticalMap->Set_Select_Reach(Distance(TacticalMap->Select_Center(), hand));
			}

			return;
		}

		double const held = (emscripten_get_now() - _RadiusArmedAt) / 1000.0;
		TacticalMap->Set_Select_Reach((int)(RADIUS_GROWTH_PER_SECOND * held));
		return;
	}

	if (_Gesture != GESTURE_UNDECIDED || _GesturePressed || _HoldSpent) {
		return;
	}

	if (!Touch_Scenario_On_Screen()) {
		return;
	}

	double const resting = emscripten_get_now() - _GestureTime;

	// Nothing is shown before the rest completes. A hand that has only paused can still
	// carry the view away, and ground that lit up under it and then went out as the pan
	// began read as the game changing its mind.
	if (resting < TOUCH_HOLD_NEAR) {
		return;
	}

	int x;
	int y;
	Canvas_Point_To_Game(_GestureStartX, _GestureStartY, x, y);

	// Over the battlefield a rest arms a selection to be grown from where it landed, which
	// is what a hand can size without covering what it is choosing. Everywhere else a rest
	// is still the right button.
	if (TacticalRect.Is_Point_Within(Point2D(x, y))) {
		Coord ground;

		if (!Touch_Ground_Under(x, y, ground)) {
			if (resting < TOUCH_HOLD) return;
			_Gesture = GESTURE_SPENT;
			return;
		}

		// A press standing on units is answered soonest, one that a grown radius could
		// still reach a little later, and one out on open ground last of all, since there
		// it has a pan to lose and nothing to gain.
		bool const alongside = TacticalMap->Any_Selectable_Near(ground, HOLD_NEAR_REACH);

		if (resting < TOUCH_HOLD_FAR && !alongside) {
			return;
		}

		bool const reachable = alongside ||
			TacticalMap->Any_Selectable_Near(ground, HOLD_PROBE_REACH);

		if (resting < TOUCH_HOLD && !reachable) {
			return;
		}

		_MouseX = x;
		_MouseY = y;

		// What was selected goes as the rest completes rather than when the hand lifts, so
		// the hold reads as having done something at the moment it did it. A hold that then
		// grows nothing is how everything is dropped.
		if (!Browser_Key_Is_Down(VK_SHIFT)) {
			Unselect_All();
		}

		// A radius grown over ground with nothing on it can only ever take nothing, and
		// arming it would cost the hand the view it can still carry. The rest is answered
		// by the clearing above and the gesture is left open, so a drag from here pans.
		if (!reachable) {
			_HoldSpent = true;
			return;
		}

		_RadiusArmedAt = emscripten_get_now();
		_RadiusHandX = x;
		_RadiusHandY = y;
		_RadiusDragging = false;
		TacticalMap->Begin_Select_Radius(ground);
		_Gesture = GESTURE_RADIUS;
		return;
	}

	if (resting < TOUCH_HOLD) {
		return;
	}

	_MouseX = x;
	_MouseY = y;
	Queue_Touch_Click(VK_RBUTTON, x, y);
	_Gesture = GESTURE_SPENT;
}


static void Touch_Begin_Drag(void)
{
	Touch_Press();
	_Gesture = GESTURE_DRAG;
}


static void Touch_Release(double cssx, double cssy)
{
	if (!_GesturePressed) {
		return;
	}

	int x;
	int y;
	Canvas_Point_To_Game(cssx, cssy, x, y);
	_MouseX = x;
	_MouseY = y;

	_KeyDown[VK_LBUTTON] = 0;
	Queue_Touch_Button(VK_LBUTTON, x, y, true);
	_GesturePressed = false;
}


static EM_BOOL Touch_Callback(int type, EmscriptenTouchEvent const * event, void *)
{
	double now = emscripten_get_now();

	// A finger on the glass rests nowhere; only a mouse event restores the
	// hover.
	_MouseHovering = false;

	// A page raises its own keyboard only in answer to a gesture.
	if (type == EMSCRIPTEN_EVENT_TOUCHSTART && _TextInputWanted) {
		Focus_Text_Input();
	}

	// Every finger the browser knows about is listed, the ones just lifted
	// included; the first two on the glass decide the gesture and a third is
	// ignored.
	bool ending = (type == EMSCRIPTEN_EVENT_TOUCHEND || type == EMSCRIPTEN_EVENT_TOUCHCANCEL);

	int count = 0;
	EmscriptenTouchPoint const * points[2] = { nullptr, nullptr };

	for (int index = 0; index < event->numTouches; index++) {
		EmscriptenTouchPoint const * point = &event->touches[index];

		if (ending && point->isChanged != 0) {
			continue;
		}

		if (count < 2) {
			points[count] = point;
		}
		count++;
	}

	if (type == EMSCRIPTEN_EVENT_TOUCHSTART) {

		if (count >= 2) {

			// A second finger takes over; a button one finger held is released
			// first.
			Touch_Release(_GestureLastX, _GestureLastY);

			_Gesture = GESTURE_MULTI;
			_MultiTime = now;
			_MultiStartX = (points[0]->targetX + points[1]->targetX) / 2.0;
			_MultiStartY = (points[0]->targetY + points[1]->targetY) / 2.0;
			_MultiLastX = _MultiStartX;
			_MultiLastY = _MultiStartY;
			_MultiMoved = false;
			return(EM_TRUE);
		}

		if (points[0] != nullptr) {
			_Gesture = GESTURE_UNDECIDED;
			_HoldSpent = false;
			_GestureID = points[0]->identifier;
			_GestureTime = now;
			_GestureStartX = points[0]->targetX;
			_GestureStartY = points[0]->targetY;
			_GestureLastX = _GestureStartX;
			_GestureLastY = _GestureStartY;
			_GesturePressed = false;
			_PanPendingX = 0.0;
			_PanPendingY = 0.0;

			// The landing point is reported at once so whatever follows the
			// pointer answers the finger; a control that tracks a press is
			// pressed at once, and Touch_Service_Hold leaves such a gesture
			// alone.
			int x;
			int y;
			Canvas_Point_To_Game(_GestureStartX, _GestureStartY, x, y);
			_MouseX = x;
			_MouseY = y;

			if (Touch_Control_Tracks_Press(x, y)) {
				Touch_Press();
			}
		}
		return(EM_TRUE);
	}

	if (type == EMSCRIPTEN_EVENT_TOUCHMOVE) {

		if (_Gesture == GESTURE_MULTI) {
			if (count >= 2) {
				double cx = (points[0]->targetX + points[1]->targetX) / 2.0;
				double cy = (points[0]->targetY + points[1]->targetY) / 2.0;

				if (fabs(cx - _MultiStartX) > TOUCH_SLOP || fabs(cy - _MultiStartY) > TOUCH_SLOP) {
					_MultiMoved = true;
				}

				Request_Map_Pan(cx - _MultiLastX, cy - _MultiLastY);

				_MultiLastX = cx;
				_MultiLastY = cy;
			}
			return(EM_TRUE);
		}

		EmscriptenTouchPoint const * finger = nullptr;
		for (int index = 0; index < event->numTouches; index++) {
			if (event->touches[index].identifier == _GestureID) {
				finger = &event->touches[index];
				break;
			}
		}
		if (finger == nullptr) {
			return(EM_TRUE);
		}

		double x = finger->targetX;
		double y = finger->targetY;

		// A finger that travels before it rests carries the left button from
		// where it landed; what that draws is the engine's business.
		if (_Gesture == GESTURE_UNDECIDED) {
			double travel = fabs(x - _GestureStartX) + fabs(y - _GestureStartY);
			if (travel > TOUCH_SLOP) {
				int landedx;
				int landedy;
				Canvas_Point_To_Game(_GestureStartX, _GestureStartY, landedx, landedy);

				int const column = Touch_Flick_Column(landedx, landedy);

				if (column >= 0) {
					_FlickColumn = column;
					_FlickPendingY = 0.0;
					_Gesture = GESTURE_FLICK;
				} else if (TacticalRect.Is_Point_Within(Point2D(landedx, landedy))) {

					// Over the battlefield a drag carries the view, which is what a map is
					// dragged for everywhere else; the button it used to carry drew a band
					// nobody asked for. Every other surface keeps the button.
					_Gesture = GESTURE_PAN;
				} else {
					Touch_Begin_Drag();
				}
			}
		}

		// The list follows the finger, and a notch costs what a wheel's does.
		if (_Gesture == GESTURE_FLICK) {
			_FlickPendingY -= (y - _GestureLastY);
		}

		if (_Gesture == GESTURE_PAN) {
			Request_Map_Pan(x - _GestureLastX, y - _GestureLastY);
		}

		// Only where the hand is; the service turns that into a reach. A hand that has
		// travelled is sizing the selection itself, so it stops growing on its own.
		if (_Gesture == GESTURE_RADIUS) {
			Canvas_Point_To_Game(x, y, _RadiusHandX, _RadiusHandY);

			if (fabs(x - _GestureStartX) + fabs(y - _GestureStartY) > TOUCH_SLOP) {
				_RadiusDragging = true;
			}
		}

		if (_Gesture == GESTURE_DRAG) {
			int gx;
			int gy;
			Canvas_Point_To_Game(x, y, gx, gy);
			_MouseX = gx;
			_MouseY = gy;
		}

		_GestureLastX = x;
		_GestureLastY = y;
		return(EM_TRUE);
	}

	if (type == EMSCRIPTEN_EVENT_TOUCHEND) {

		if (_Gesture == GESTURE_MULTI && count < 2) {

			// Two fingers that neither travelled nor lingered are the right
			// button as well.
			if (!_MultiMoved && (now - _MultiTime) < TOUCH_HOLD) {
				int x;
				int y;
				Canvas_Point_To_Game(_MultiStartX, _MultiStartY, x, y);
				_MouseX = x;
				_MouseY = y;
				Queue_Touch_Click(VK_RBUTTON, x, y);
			}
			_Gesture = GESTURE_SPENT;

		} else if (_Gesture == GESTURE_FLICK) {
			_FlickPendingY = 0.0;
			_Gesture = GESTURE_SPENT;

		} else if (_Gesture == GESTURE_RADIUS) {

			// The selection this replaces was already dropped when the rest completed.
			TacticalMap->Select_Radius();
			_Gesture = GESTURE_SPENT;

		} else if (_Gesture == GESTURE_PAN) {

			// The view was carried; no button was ever pressed, so none is released.
			_Gesture = GESTURE_SPENT;

		} else if (_Gesture == GESTURE_DRAG) {
			Touch_Release(_GestureLastX, _GestureLastY);
			_Gesture = GESTURE_SPENT;

		} else if (_Gesture == GESTURE_UNDECIDED) {

			// A tap is the whole click where it landed; one pressed as it
			// landed only wants its release, at the same spot. A rest already answered
			// dropped the selection, and clicking as well would only take the order that
			// click carries to the ground under it.
			if (!_HoldSpent) {
				Touch_Press();
				Touch_Release(_GestureStartX, _GestureStartY);
			}

			_Gesture = GESTURE_SPENT;
		}
	}

	if (type == EMSCRIPTEN_EVENT_TOUCHCANCEL) {
		if (_Gesture == GESTURE_RADIUS) {
			TacticalMap->End_Select_Radius();
			_Gesture = GESTURE_SPENT;
			return(EM_TRUE);
		}

		if (_Gesture == GESTURE_PAN) {
			_Gesture = GESTURE_SPENT;
			return(EM_TRUE);
		}

		Touch_Release(_GestureLastX, _GestureLastY);
		_Gesture = GESTURE_SPENT;
	}

	if (ending && count == 0) {
		_Gesture = GESTURE_NONE;
	}

	return(EM_TRUE);
}


static void Release_Mouse_Edge(void)
{
	if (!_MouseOutside) {
		return;
	}

	_MouseOutside = false;

	if (_MouseX <= 0) _MouseX = 1;
	if (_MouseY <= 0) _MouseY = 1;
	if (VideoModeWidth > 2 && _MouseX >= VideoModeWidth - 1) _MouseX = VideoModeWidth - 2;
	if (VideoModeHeight > 2 && _MouseY >= VideoModeHeight - 1) _MouseY = VideoModeHeight - 2;
}


static EM_BOOL Mouse_Boundary_Callback(int type, EmscriptenMouseEvent const * event, void *)
{
	if (type == EMSCRIPTEN_EVENT_MOUSEENTER) {
		_MouseOutside = false;
		_MouseHovering = true;
		return(EM_FALSE);
	}

	// The leave event carries the position the pointer left through; clamped to
	// the frame it sits on the edge the engine reads as a scroll request.
	Canvas_Point_To_Game(event->targetX, event->targetY, _MouseX, _MouseY);
	_MouseOutside = true;
	return(EM_FALSE);
}


// Losing the keyboard is as close as a tab comes to losing the screen.
static EM_BOOL Blur_Callback(int, EmscriptenFocusEvent const *, void *)
{
	Release_Mouse_Edge();
	return(EM_FALSE);
}


static EM_BOOL Visibility_Callback(int, EmscriptenVisibilityChangeEvent const * event, void *)
{
	_Hidden = (event->hidden != 0);

	if (_Hidden) {
		Release_Mouse_Edge();
	}
	return(EM_FALSE);
}


void Browser_Service(void)
{
	if (!_Initialized) {
		return;
	}

	// A background archive fetch delivers more than the reads ask for, so the
	// persisted store is drained once a frame rather than only from Read_At.
	Block_Source_Service();

	Offline_Service();

	if (Measure_Canvas()) {

		// Mouse messages are hit tested against the main window's rectangle
		// in canvas pixels, so a canvas that outgrew it would drop every
		// click past the old edge.
		if (MainWindow != NULL) {
			MoveWindow(MainWindow, 0, 0, _CanvasWidth, _CanvasHeight, FALSE);
		}

		// The presenter scales the frame it holds into the new window at once.
		Video_On_Resize(_CanvasWidth, _CanvasHeight);

		// The frame is sized in CSS pixels, or a fixed width sidebar would
		// halve on a display with two device pixels per CSS pixel.
		Video_Request_Frame_Size(_CanvasCSSWidth, _CanvasCSSHeight);
	}

	// Visibility stands in for window focus; a lockstep game paused on focus
	// loss would stall its peers.
	GameInFocus = !_Hidden;

	Service_Text_Input();

	while (_EventHead != _EventTail) {
		BrowserEvent const event = _Events[_EventHead];
		_EventHead = (_EventHead + 1) % EVENT_QUEUE_SIZE;

		// A touch during a movie is escape, posted to the keyboard buffer
		// alone so it cannot reach the window messages and open the options
		// dialog behind the movie.
		if (event.IsTouch && Movie_Is_Playing()) {
			if (Keyboard != nullptr) {
				Keyboard->Post_Key_Event(VK_ESCAPE, event.IsRelease);
			}
			continue;
		}

		_EventModifiers = event.Modifiers;

		if (_EventHook != nullptr && _EventHook(event.Key, event.X, event.Y, event.IsMouse, event.IsRelease)) {
			continue;
		}

		if (Keyboard != nullptr) {
			if (event.IsMouse) {
				Keyboard->Post_Mouse_Event(event.Key, event.X, event.Y, event.IsRelease);
			} else {
				Keyboard->Post_Key_Event(event.Key, event.IsRelease);
			}
		}
	}

	_EventModifiers = _Modifiers;

	Service_Wheel();

	Touch_Service_Hold();
	Touch_Service_Pan();
	Touch_Service_Flick();
}


void Browser_Set_Event_Hook(BrowserEventHook hook)
{
	_EventHook = hook;
}


void Browser_Yield(void)
{
	if (!_EngineEntered) {
		return;
	}

	_BlockingWaits++;

#if defined(OPENTS_WASM_JSPI)
	Browser_Await_Frame(_Hidden ? 1 : 0);
	_FrameSerial++;
#else
	// Nothing carries the wait: the engine keeps the thread and the page stops
	// answering. docs/WASM-PORT.md section 1.4 records what removes the need for the
	// scaffold.
	static bool _reported = false;
	if (!_reported) {
		_reported = true;
		DebugString("Browser: a wait was reached and the yield scaffold is not built in; the page will stop responding.\n");
	}
#endif

	_LastYield = emscripten_get_now();
	Browser_Service();
}


// The callback hook is reached far more often than the engine draws, and
// yielding on each would cost a frame apiece.
bool Browser_Yield_If_Due(void)
{
	if ((emscripten_get_now() - _LastYield) < YIELD_INTERVAL) {
		return(false);
	}

	Browser_Yield();
	return(true);
}


unsigned int Browser_Blocking_Wait_Count(void)
{
	return(_BlockingWaits);
}


bool Browser_Yield_Is_Available(void)
{
#if defined(OPENTS_WASM_JSPI)
	return(true);
#else
	return(false);
#endif
}


unsigned int Browser_Frame_Serial(void)
{
	return(_FrameSerial);
}


int Browser_Canvas_Width(void)
{
	return(_CanvasWidth);
}


int Browser_Canvas_Height(void)
{
	return(_CanvasHeight);
}


int Browser_Canvas_CSS_Width(void)
{
	return(_CanvasCSSWidth);
}


int Browser_Canvas_CSS_Height(void)
{
	return(_CanvasCSSHeight);
}


int Browser_Screen_Width(void)
{
	return(_ScreenWidth);
}


int Browser_Screen_Height(void)
{
	return(_ScreenHeight);
}


BrowserDisplayPolicy Browser_Display_Policy(void)
{
	return(_DisplayPolicy);
}


int Browser_Display_Width(void)
{
	return(_DisplayWidth);
}


int Browser_Display_Height(void)
{
	return(_DisplayHeight);
}


bool Browser_Is_Hidden(void)
{
	return(_Hidden);
}


unsigned short Browser_Key_Modifiers(void)
{
	return(_Modifiers);
}


bool Browser_Key_Is_Down(unsigned short vk_key)
{
	return(_KeyDown[vk_key & 0xFF] != 0);
}


// The engine acts on a queued click several frames after the page reported
// it, so the modifiers it was made under are presented while its message is
// dispatched, and the live ones return once the queue is empty.
void Browser_Apply_Modifiers(unsigned short modifiers)
{
	_KeyDown[VK_SHIFT] = ((modifiers & WWKEY_SHIFT_BIT) != 0) ? 1 : 0;
	_KeyDown[VK_CONTROL] = ((modifiers & WWKEY_CTRL_BIT) != 0) ? 1 : 0;
	_KeyDown[VK_MENU] = ((modifiers & WWKEY_ALT_BIT) != 0) ? 1 : 0;
}


unsigned short Browser_Event_Modifiers(void)
{
	return(_EventModifiers);
}


int Browser_Key_To_ASCII(unsigned short key)
{
	unsigned short vk_key = key & 0xFF;

	if ((key & WWKEY_SHIFT_BIT) != 0 && _ShiftedAscii[vk_key] != '\0') {
		return(_ShiftedAscii[vk_key]);
	}
	return(_Ascii[vk_key]);
}


int Browser_Mouse_X(void)
{
	return(_MouseX);
}


int Browser_Mouse_Y(void)
{
	return(_MouseY);
}


bool Browser_Mouse_Is_Hovering(void)
{
	return(_MouseHovering);
}


void Browser_Begin_Text_Input(void)
{
	_TextInputWanted = true;
	Focus_Text_Input();
}


void Browser_End_Text_Input(void)
{
	if (!_TextInputWanted) {
		return;
	}

	_TextInputWanted = false;

	EM_ASM({
		var el = document.getElementById("opents-text");
		if (el) {
			el.value = "";
			el.opentsPrev = "";
			el.opentsQueue = [];
			el.blur();
		}
	});
}


// A page has no cursor to query, so the position is that of the last event
// over the canvas.
class BrowserMouseClass : public WWMouseClass
{
	public:
		BrowserMouseClass(HWND window) : WWMouseClass(window) {}

		virtual int Get_Mouse_X(void) const override {return(Browser_Mouse_X());}
		virtual int Get_Mouse_Y(void) const override {return(Browser_Mouse_Y());}
		virtual Point2D Get_Mouse_Point(void) const override {return(Point2D(Browser_Mouse_X(), Browser_Mouse_Y()));}
		virtual bool Is_Hovering(void) const override {return(Browser_Mouse_Is_Hovering());}
};


Mouse * Browser_Create_Mouse(HWND window)
{
	return(new BrowserMouseClass(window));
}



// A box already up is replaced, since one question is waited on at a time.
int Browser_Message_Box(char const * caption, char const * text, int buttons)
{
	if (!Browser_Yield_Is_Available()) {
		return(0);
	}

	// Inside EM_ASM a comma outside parentheses splits the block and a macro
	// name is not expanded, so the identifiers are literals pinned by asserts.
	static_assert(IDOK == 1 && IDCANCEL == 2 && IDABORT == 3 && IDRETRY == 4, "message box results");
	static_assert(IDIGNORE == 5 && IDYES == 6 && IDNO == 7, "message box results");

	EM_ASM({
		var previous = document.getElementById("opents-messagebox");
		if (previous) previous.remove();

		var buttons = [];
		var add = function (label, id) { buttons.push(Array(label, id)); };

		if ($2 == 1) { add("OK", 1); add("Cancel", 2); }
		else if ($2 == 2) { add("Abort", 3); add("Retry", 4); add("Ignore", 5); }
		else if ($2 == 3) { add("Yes", 6); add("No", 7); add("Cancel", 2); }
		else if ($2 == 4) { add("Yes", 6); add("No", 7); }
		else if ($2 == 5) { add("Retry", 4); add("Cancel", 2); }
		else { add("OK", 1); }

		var box = document.createElement("div");
		box.id = "opents-messagebox";
		box.setAttribute("style",
			"position:fixed;inset:0;z-index:2147483647;display:flex;align-items:center;" +
			"justify-content:center;background:rgba(0,0,0,.6);" +
			"font:13px/1.5 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace");

		var panel = document.createElement("div");
		panel.setAttribute("style",
			"min-width:280px;max-width:min(560px,90vw);background:#14171c;color:#d8dbe0;" +
			"border:1px solid #2e343d;padding:16px 18px");

		var heading = document.createElement("div");
		heading.setAttribute("style", "font-weight:600;margin-bottom:10px");
		heading.textContent = UTF8ToString($0);

		var message = document.createElement("div");
		message.setAttribute("style", "white-space:pre-wrap;margin-bottom:16px");
		message.textContent = UTF8ToString($1);

		var row = document.createElement("div");
		row.setAttribute("style", "display:flex;gap:8px;justify-content:flex-end");

		buttons.forEach(function (entry) {
			var button = document.createElement("button");
			button.type = "button";
			button.textContent = entry[0];
			button.setAttribute("style",
				"font:inherit;padding:5px 14px;background:#222831;color:#d8dbe0;" +
				"border:1px solid #3a424d;cursor:pointer");
			button.addEventListener("click", function () { window.__opentsMessageBox = entry[1]; });
			row.appendChild(button);
		});

		panel.appendChild(heading);
		panel.appendChild(message);
		panel.appendChild(row);
		box.appendChild(panel);
		document.body.appendChild(box);

		window.__opentsMessageBox = 0;
	}, caption, text, buttons);

	int answer = 0;
	while (answer == 0) {
		Browser_Yield();
		answer = EM_ASM_INT({ return window.__opentsMessageBox | 0; });
	}

	EM_ASM({
		var box = document.getElementById("opents-messagebox");
		if (box) box.remove();
		window.__opentsMessageBox = 0;
	});

	return(answer);
}


// The document is looked for rather than assumed, because the test harness runs on a
// host with no page at all.
void Browser_Show_Cursor(void const *, char const * css, unsigned long const *, int, int, int, int)
{
	EM_ASM({
		var element = (typeof document === "undefined") ? null : document.querySelector(UTF8ToString($0));
		if (element) {
			element.style.cursor = UTF8ToString($1);
		}
	}, CANVAS_SELECTOR, css);
}


// The renderer names a canvas by CSS selector rather than by handle, so the selector goes
// where a platform hands over a window.
NativeWindow Browser_Native_Window(void)
{
	return(NativeWindow{ NATIVE_WINDOW_DEFAULT, nullptr, const_cast<char *>(CANVAS_SELECTOR) });
}


// The GDI call this stands in for asks for a 28 pixel Swiss cell averaging
// twenty wide, so the page's sans-serif is stretched until its average matches;
// the line's top lands on the given row, as TA_TOP places a cell.
EM_JS(void, Browser_Caption_Blend, (char const * text, void * destination, int stride,
	int surface_height, int rect_x, int top_y, int rect_width), {
	if (typeof document === 'undefined') return;

	var caption = Module.OpenTSCaption;
	if (!caption) {
		var canvas = document.createElement('canvas');
		caption = Module.OpenTSCaption = {
			canvas: canvas,
			context: canvas.getContext('2d', { willReadFrequently: true })
		};
	}

	var context = caption.context;
	if (!context || rect_width <= 0) return;

	var height = 40;
	if (caption.canvas.width !== rect_width || caption.canvas.height !== height) {
		caption.canvas.width = rect_width;
		caption.canvas.height = height;
	}

	context.setTransform(1, 0, 0, 1, 0, 0);
	context.clearRect(0, 0, rect_width, height);
	context.font = '25px sans-serif';
	var alphabet = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ';
	var stretch = 20 / (context.measureText(alphabet).width / alphabet.length);
	context.setTransform(stretch, 0, 0, 1, 0, 0);
	context.textAlign = 'center';
	context.textBaseline = 'top';
	context.fillStyle = '#ffffff';
	context.fillText(UTF8ToString(text), (rect_width / 2) / stretch, 0);

	// Only the antialiased edges carry intermediate values, so each covered
	// pixel is blended toward white by its coverage and repacked as 565.
	var pixels = context.getImageData(0, 0, rect_width, height).data;
	var target = (destination >>> 0) >>> 1;
	for (var y = 0; y < height; y++) {
		if (top_y + y >= surface_height) break;
		var sourceRow = y * rect_width * 4;
		var targetRow = target + (top_y + y) * stride + rect_x;
		for (var x = 0; x < rect_width; x++) {
			var alpha = pixels[sourceRow + x * 4 + 3];
			if (!alpha) continue;
			var pixel = HEAPU16[targetRow + x];
			var red = (pixel >> 8) & 0xf8;
			var green = (pixel >> 3) & 0xfc;
			var blue = (pixel << 3) & 0xf8;
			red += ((255 - red) * alpha / 255) | 0;
			green += ((255 - green) * alpha / 255) | 0;
			blue += ((255 - blue) * alpha / 255) | 0;
			HEAPU16[targetRow + x] = ((red & 0xf8) << 8) | ((green & 0xfc) << 3) | (blue >> 3);
		}
	}
});


void Browser_Draw_Caption(Surface & surface, Rect const & rect, char const * text)
{
	if (surface.Bytes_Per_Pixel() != 2) {
		return;
	}
	void * buffer = surface.Lock();
	if (buffer == NULL) {
		return;
	}
	Browser_Caption_Blend(text, buffer, surface.Stride() / 2, surface.Get_Height(),
		rect.X, rect.Y + rect.Height / 2, rect.Width);
	surface.Unlock();
}


extern "C" {


// Lets a page or an automated check tell a running engine from a stopped one.
EMSCRIPTEN_KEEPALIVE int OpenTS_Browser_Main_Menu(void)
{
	return(Main_Menu_Is_Up ? 1 : 0);
}


EMSCRIPTEN_KEEPALIVE int OpenTS_Browser_Frames(void)
{
	return((int)_FrameSerial);
}


EMSCRIPTEN_KEEPALIVE int OpenTS_Browser_Waits(void)
{
	return((int)_BlockingWaits);
}


}


int Browser_Pending_Events(void)
{
	return((_EventTail - _EventHead + EVENT_QUEUE_SIZE) % EVENT_QUEUE_SIZE);
}


extern "C" {


// The frame lags the canvas while a resolution change waits for the window to
// settle and for the engine to reach a point that can take one.
EMSCRIPTEN_KEEPALIVE int OpenTS_Browser_Frame_Width(void)
{
	return(VideoModeWidth);
}


EMSCRIPTEN_KEEPALIVE int OpenTS_Browser_Frame_Height(void)
{
	return(VideoModeHeight);
}


}


bool Browser_Init(void)
{
	if (_Initialized) {
		return(true);
	}

	_EngineEntered = true;
	_LastYield = emscripten_get_now();

	_Modifiers = 0;
	_EventModifiers = 0;
	memset(_KeyDown, 0, sizeof(_KeyDown));
	memset(_Ascii, 0, sizeof(_Ascii));
	memset(_ShiftedAscii, 0, sizeof(_ShiftedAscii));

	Read_Page_Configuration();

	if (!Measure_Canvas()) {
		DebugString("Browser: the canvas at %s has not been laid out.\n", CANVAS_SELECTOR);
		return(false);
	}

	EmscriptenVisibilityChangeEvent visibility;
	_Hidden = (emscripten_get_visibility_status(&visibility) == EMSCRIPTEN_RESULT_SUCCESS) && (visibility.hidden != 0);
	GameInFocus = !_Hidden;

	// The keyboard is taken from the window because a canvas only receives key
	// events while it holds the focus; the mouse is taken from the canvas.
	emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, Key_Callback);
	emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, Key_Callback);

	emscripten_set_mousemove_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Mouse_Callback);
	emscripten_set_mousedown_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Mouse_Callback);
	emscripten_set_mouseup_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Mouse_Callback);
	emscripten_set_dblclick_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Mouse_Callback);

	// Claimed, or the page scrolls itself.
	emscripten_set_wheel_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Wheel_Callback);

	// Every touch event is claimed, or the page scrolls, zooms, or selects text
	// under the gesture.
	emscripten_set_touchstart_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Touch_Callback);
	emscripten_set_touchmove_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Touch_Callback);
	emscripten_set_touchend_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Touch_Callback);
	emscripten_set_touchcancel_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Touch_Callback);

	emscripten_set_mouseleave_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Mouse_Boundary_Callback);
	emscripten_set_mouseenter_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Mouse_Boundary_Callback);

	emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, Blur_Callback);
	emscripten_set_visibilitychange_callback(nullptr, EM_TRUE, Visibility_Callback);

	// The right button is the engine's primary command, so the page's context
	// menu is suppressed.
	EM_ASM({
		var canvas = document.querySelector(UTF8ToString($0));
		if (canvas) {
			canvas.addEventListener("contextmenu", function (e) { e.preventDefault(); });
		}
	}, CANVAS_SELECTOR);

	_LastYield = emscripten_get_now();
	_Initialized = true;

	DebugString("Browser: canvas %s is %dx%d CSS pixels and %dx%d device pixels on a %dx%d screen; the frame %s the window; yield scaffold is %s.\n",
		CANVAS_SELECTOR, _CanvasCSSWidth, _CanvasCSSHeight, _CanvasWidth, _CanvasHeight, _ScreenWidth, _ScreenHeight,
		(_DisplayPolicy == BROWSER_DISPLAY_NATIVE) ? "follows" : "is scaled into",
		Browser_Yield_Is_Available() ? "built in" : "absent");

	return(true);
}

#endif	// __EMSCRIPTEN__
