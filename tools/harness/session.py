"""One run of the game in a browser: states, input, and what can be observed.

The engine reports which loop it is waiting in (``OpenTS_Phase``), what is on
screen (``OpenTS_UI``), whether it still holds input it has not read, and hands
the page an event for every phase change and marker.  This turns those into
named states, into targets a click can name instead of a position, and into
input expressed in the game's own coordinates, so that nobody translates a
click by hand again.
"""

import base64
import json
import re
import time

import imaging


START = time.monotonic()


class HarnessError(Exception):
    """A step could not be carried out, or a wait never came true."""


# The phases the engine reports as the top of its stack.
PHASES = ("none", "menu", "movie", "loading", "game", "dialog", "alert")

# The phases that stay put until the player acts: where a sequence of films
# has ended, and where "skip-movies" stops.
RESTING = ("menu", "game", "dialog", "alert", "loading")

# What "wait" takes besides a phase: the page's own milestones, the markers
# the engine hands the page, and the aliases kept for older scripts.
STATES = ("module", "main", "frame", "init", "scenario", "playing", "idle") + PHASES

BUTTON_NAMES = {"left": "left", "middle": "middle", "right": "right", "none": "none"}
BUTTON_MASK = {"left": 1, "right": 2, "middle": 4, "none": 0}

MODIFIERS = {"alt": 1, "ctrl": 2, "control": 2, "meta": 4, "cmd": 4, "shift": 8}

# The names a step may use, against the DOM code the engine reads and the Windows
# virtual key Chrome should report. Letters, digits and function keys are derived.
NAMED_KEYS = {
    "escape": ("Escape", "Escape", 27),
    "esc": ("Escape", "Escape", 27),
    "enter": ("Enter", "Enter", 13),
    "return": ("Enter", "Enter", 13),
    "tab": ("Tab", "Tab", 9),
    "space": ("Space", " ", 32),
    "backspace": ("Backspace", "Backspace", 8),
    "delete": ("Delete", "Delete", 46),
    "insert": ("Insert", "Insert", 45),
    "home": ("Home", "Home", 36),
    "end": ("End", "End", 35),
    "pageup": ("PageUp", "PageUp", 33),
    "pagedown": ("PageDown", "PageDown", 34),
    "up": ("ArrowUp", "ArrowUp", 38),
    "down": ("ArrowDown", "ArrowDown", 40),
    "left": ("ArrowLeft", "ArrowLeft", 37),
    "right": ("ArrowRight", "ArrowRight", 39),
    "shift": ("ShiftLeft", "Shift", 16),
    "ctrl": ("ControlLeft", "Control", 17),
    "alt": ("AltLeft", "Alt", 18),
}


def key_event(name):
    """Turns a key name into the fields Chrome and the engine both want."""

    lowered = name.lower()

    if lowered in NAMED_KEYS:
        code, key, virtual = NAMED_KEYS[lowered]
        return code, key, virtual, ""

    if len(name) == 1 and name.isalpha():
        upper = name.upper()
        return "Key" + upper, name, ord(upper), name

    if len(name) == 1 and name.isdigit():
        return "Digit" + name, name, ord(name), name

    match = re.fullmatch(r"[fF]([1-9]|1[0-9]|2[0-4])", name)
    if match:
        number = int(match.group(1))
        return "F%d" % number, "F%d" % number, 111 + number, ""

    if len(name) == 1:
        punctuation = {
            "-": ("Minus", 189), "=": ("Equal", 187), "[": ("BracketLeft", 219),
            "]": ("BracketRight", 221), "\\": ("Backslash", 220), ";": ("Semicolon", 186),
            "'": ("Quote", 222), "`": ("Backquote", 192), ",": ("Comma", 188),
            ".": ("Period", 190), "/": ("Slash", 191), " ": ("Space", 32),
        }
        if name in punctuation:
            code, virtual = punctuation[name]
            return code, name, virtual, name

    raise HarnessError("no key is named %r" % name)


def is_target(word):
    """Whether a step word names something on screen rather than a position."""

    return word.startswith("@") or word.startswith("text:") or word.startswith("unit:")


BOOT_SCRIPT = r"""
(function () {
    var harness = {
        ini: __INI__,
        gameSize: __GAME__,
        integerScaling: __INTEGER__,
        wroteIni: false,
        iniError: null,
        hookError: null
    };

    window.OpenTS_Harness = harness;

    /* The engine's own clamp: a frame is a multiple of four, no larger than the
       size the window is followed to, and no smaller than the one the sidebar
       still fits in. Kept in step with Video_Clamp_Frame_Size. */
    function clamp(value, maximum, minimum) {
        if (value > maximum) value = maximum;
        value = value & ~3;
        if (value < minimum) value = minimum;
        return value;
    }

    /* Where the game's frame lands on the page, by the same arithmetic
       Update_Scale_Info uses, so a game coordinate converts the way the engine
       converts it back. */
    harness.geometry = function () {
        var canvas = document.getElementById("canvas");
        if (!canvas) return null;

        var rect = canvas.getBoundingClientRect();
        var ratio = window.devicePixelRatio || 1;
        var windowWidth = canvas.width || Math.round(rect.width * ratio);
        var windowHeight = canvas.height || Math.round(rect.height * ratio);

        /* The engine's own frame, when it will say. It is not always the canvas:
           a resolution change waits for the window to settle and for the engine
           to reach a point that can take one, and until then the frame is the
           size it was. Asking is the only way to convert correctly meanwhile. */
        var gameWidth = 0;
        var gameHeight = 0;
        try {
            if (Module && Module._OpenTS_Browser_Frame_Width) {
                gameWidth = Module._OpenTS_Browser_Frame_Width();
                gameHeight = Module._OpenTS_Browser_Frame_Height();
            }
        } catch (e) { gameWidth = 0; gameHeight = 0; }

        if (!(gameWidth > 0 && gameHeight > 0)) {
            gameWidth = harness.gameSize ? harness.gameSize[0] : Math.round(rect.width);
            gameHeight = harness.gameSize ? harness.gameSize[1] : Math.round(rect.height);
        }

        gameWidth = clamp(gameWidth, 2560, 640);
        gameHeight = clamp(gameHeight, 1600, 400);

        var scale = Math.min(windowWidth / gameWidth, windowHeight / gameHeight);
        if (harness.integerScaling && scale >= 1) scale = Math.floor(scale);

        var destWidth = Math.trunc(gameWidth * scale);
        var destHeight = Math.trunc(gameHeight * scale);

        return {
            css: {left: rect.left, top: rect.top, width: rect.width, height: rect.height},
            ratio: ratio,
            drawing: [windowWidth, windowHeight],
            game: [gameWidth, gameHeight],
            dest: [Math.floor((windowWidth - destWidth) / 2),
                   Math.floor((windowHeight - destHeight) / 2),
                   destWidth, destHeight]
        };
    };

    /* A game pixel's centre, in the page's own coordinates. The centre rather
       than the corner, so that the rounding on the way back cannot land the
       event on the neighbouring pixel. */
    harness.toPage = function (x, y) {
        var view = harness.geometry();
        if (!view) return null;

        return {
            x: view.css.left + (view.dest[0] + (x + 0.5) * view.dest[2] / view.game[0]) / view.ratio,
            y: view.css.top + (view.dest[1] + (y + 0.5) * view.dest[3] / view.game[1]) / view.ratio
        };
    };

    if (harness.ini === null) return;

    /* The settings file is a bare name, which the engine's file layer looks for
       beside the module before it asks a disc image, so writing one here is what
       an installed SUN.INI would have been. It goes in after the page's own
       preRun, which is where the save directory is mounted, and outside that
       directory, which the run's first syncfs would otherwise reconcile away. */
    function attach(module) {
        if (!module || module.__opentsHarnessAttached) return;
        module.__opentsHarnessAttached = true;
        module.preRun = module.preRun || [];
        module.preRun.push(function () {
            try {
                module.FS.writeFile("SUN.INI", harness.ini);
                harness.wroteIni = true;
            } catch (error) {
                harness.iniError = String(error);
            }
        });
    }

    try {
        var held;
        Object.defineProperty(window, "Module", {
            configurable: true,
            get: function () { return held; },
            set: function (value) { held = value; attach(value); }
        });
    } catch (error) {
        harness.hookError = String(error);
    }
}());
"""


SNAPSHOT = r"""
(function () {
    var state = window.OpenTS_State || {};
    var out = {
        page: !!window.OpenTS_State,
        started: !!state.started,
        frames: state.frames || 0,
        waits: state.waits || 0,
        syncs: state.syncs || 0,
        persistent: !!state.persistent,
        restored: !!state.restored,
        lines: (state.lines || []).length,
        phase: state.phase || "none",
        phases: state.phases || "none",
        detail: state.phaseDetail || "",
        serial: state.phaseSerial || 0,
        pending: state.pending || 0,
        events: (state.eventBase || 0) + ((state.events || []).length),
        module: typeof Module !== "undefined",
        /* The runtime is up once its exports are on the module object; the
           runtime's own calledRun is a local of the loader and not readable. */
        loaded: typeof Module !== "undefined" &&
                typeof Module._OpenTS_Browser_Frames === "function",
        title: document.title,
        gate: !!(document.getElementById("unsupported") &&
                 !document.getElementById("unsupported").hidden)
    };

    if (out.module) {
        /* Read from what the page collected rather than by looking through Module for
           something callable. Calling every export it can find means calling the ones that
           do work as well as the ones that report it, and the page is where the difference
           is known: OpenTS_Counters in wasm/page.js names them. */
        out.counters = (state.counters && typeof state.counters === "object") ?
            state.counters : {};

        var gate = Module.OpenTS_FrameGate;
        if (gate) {
            out.stepping = {held: !!gate.held, credits: gate.credits | 0, parked: !!gate.parked};
        }
    }

    if (window.OpenTS_Harness) {
        out.geometry = window.OpenTS_Harness.geometry();
        out.ini = {
            written: window.OpenTS_Harness.wroteIni,
            error: window.OpenTS_Harness.iniError,
            hook: window.OpenTS_Harness.hookError
        };
    }

    var fault = document.getElementById("fault");
    if (fault) out.fault = fault.textContent;

    return out;
}())
"""


class Session:
    """Drives one page: navigation, waiting, input, and observation."""

    def __init__(self, connection, target_id, session_id, *, verbose=False):
        self.connection = connection
        self.target_id = target_id
        self.session = session_id
        self.verbose = verbose

        self.log = []
        self.events = []
        self.exceptions = []
        self.last = {}
        self._event_cursor = 0

        connection.listen(self._event)
        self.call("Runtime.enable")
        self.call("Page.enable")
        self.call("Log.enable")

    # -- protocol ---------------------------------------------------------

    def call(self, method, params=None, timeout=60.0):
        return self.connection.call(method, params, session=self.session, timeout=timeout)

    def _event(self, message):
        if message.get("sessionId") != self.session:
            return

        method = message.get("method")
        params = message.get("params", {})

        if method == "Runtime.consoleAPICalled":
            pieces = []
            for argument in params.get("args", []):
                if "value" in argument:
                    pieces.append(str(argument["value"]))
                elif "description" in argument:
                    pieces.append(argument["description"])
            self._record(params.get("type", "log"), " ".join(pieces))
        elif method == "Log.entryAdded":
            entry = params.get("entry", {})
            self._record(entry.get("level", "log"), entry.get("text", ""))
        elif method == "Runtime.exceptionThrown":
            details = params.get("exceptionDetails", {})
            text = details.get("text", "")
            thrown = details.get("exception", {})
            text = "%s %s" % (text, thrown.get("description", thrown.get("value", "")))
            self.exceptions.append(text.strip())
            self._record("exception", text.strip())

    def _record(self, level, text):
        for line in text.splitlines() or [""]:
            self.log.append({"at": round(time.monotonic() - START, 3), "level": level, "text": line})
        if self.verbose:
            print("    | %s" % text.rstrip(), flush=True)

    def evaluate(self, expression, timeout=30.0):
        answer = self.call("Runtime.evaluate", {
            "expression": expression,
            "returnByValue": True,
            "awaitPromise": False,
        }, timeout=timeout)

        if answer.get("exceptionDetails"):
            details = answer["exceptionDetails"]
            thrown = details.get("exception", {})
            raise HarnessError("the page raised: %s" %
                               (thrown.get("description") or details.get("text")))

        return answer.get("result", {}).get("value")

    # -- navigation and state --------------------------------------------

    def open(self, url):
        self.call("Page.navigate", {"url": url})

    def snapshot(self):
        self.last = self.evaluate(SNAPSHOT) or {}
        self._collect_events()
        return self.last

    def _collect_events(self):
        """Copies the events the page has recorded since the last look."""

        count = self.last.get("events", 0)
        if count <= self._event_cursor:
            return

        fresh = self.evaluate(
            "JSON.stringify((window.OpenTS_State.events || []).slice(%d - window.OpenTS_State.eventBase))"
            % self._event_cursor)
        self._event_cursor = count

        for event in json.loads(fresh or "[]"):
            entry = {"at": round(time.monotonic() - START, 3),
                     "name": event.get("name", ""), "detail": event.get("detail", "")}
            self.events.append(entry)
            self._record("event", "%s %s" % (entry["name"], entry["detail"])
                         if entry["detail"] else entry["name"])

    def geometry(self):
        view = (self.last or {}).get("geometry")
        if view is None:
            view = self.evaluate("window.OpenTS_Harness && window.OpenTS_Harness.geometry()")
        if view is None:
            raise HarnessError("the page has no canvas yet, so there are no game coordinates")
        return view

    def _matched(self, pattern):
        return any(pattern.search(line["text"]) for line in self.log)

    def _saw_event(self, name):
        return any(event["name"] == name for event in self.events)

    def reached(self, state):
        snapshot = self.last

        if state.startswith("any:"):
            return any(self.reached(one) for one in state[4:].split("|") if one)
        if state.startswith("not:"):
            return not self.reached(state[4:])
        if state.startswith("event:"):
            return self._saw_event(state[6:])
        if state.startswith("ui:"):
            try:
                self.resolve_target(state[3:])
                return True
            except HarnessError:
                return False

        if state == "module":
            return bool(snapshot.get("loaded"))
        if state == "main":
            return bool(snapshot.get("started"))
        if state == "frame":
            return snapshot.get("frames", 0) >= 1
        if state == "init":
            return self._saw_event("init")
        if state == "scenario":
            return self._saw_event("scenario")
        if state == "playing":
            return snapshot.get("phase") == "game"
        if state in PHASES:
            return snapshot.get("phase") == state

        raise HarnessError("no state is named %r; the states are %s" %
                           (state, ", ".join(STATES)))

    def wait(self, target, timeout):
        """Waits for a named state, a log line, a frame count, or an expression."""

        deadline = time.monotonic() + timeout
        baseline = None

        while True:
            self.snapshot()

            if self.last.get("gate"):
                raise HarnessError("the page put up its gate screen: the module it wanted "
                                   "is not on the server")

            if target.startswith("log:"):
                done = self._matched(re.compile(target[4:]))
            elif target.startswith("frames:+"):
                if baseline is None:
                    baseline = self.last.get("frames", 0)
                done = self.last.get("frames", 0) >= baseline + int(target[8:])
            elif target.startswith("js:"):
                done = bool(self.evaluate(target[3:]))
            elif target == "idle":
                if baseline is None:
                    baseline = self.last.get("frames", 0)
                done = (self.last.get("pending", 0) == 0 and
                        self.last.get("frames", 0) > baseline)
            else:
                done = self.reached(target)

            if done:
                return self.last

            if self.last.get("fault"):
                raise HarnessError("the engine stopped: %s" % self.last["fault"])

            if time.monotonic() >= deadline:
                raise HarnessError(
                    "waiting for %r timed out after %gs (phase %s, frames %s, last line %r)" % (
                        target, timeout, self.last.get("phases"), self.last.get("frames"),
                        self.log[-1]["text"] if self.log else ""))

            time.sleep(0.2)

    def settle(self, frames_before, timeout):
        """Waits for the engine to read what was sent and draw once more.

        Returns whether it did; a step during a film or a load, which reads
        nothing, is reported rather than failed.
        """

        deadline = time.monotonic() + timeout
        started = time.monotonic()

        while True:
            self.snapshot()
            drained = (self.last.get("pending", 0) == 0 and
                       self.last.get("frames", 0) > frames_before)
            if drained or time.monotonic() >= deadline:
                return {
                    "drained": drained,
                    "frames": self.last.get("frames", 0) - frames_before,
                    "seconds": round(time.monotonic() - started, 3),
                    "phase": self.last.get("phases"),
                }
            time.sleep(0.05)

    # -- the interface on screen -----------------------------------------

    def ui(self):
        """What the engine says is on screen: the phase, the menu, the windows, the gadgets."""

        text = self.evaluate(
            "(typeof Module !== 'undefined' && Module._OpenTS_UI && Module.UTF8ToString) ? "
            "Module.UTF8ToString(Module._OpenTS_UI()) : null")
        if text is None:
            raise HarnessError("the engine is not up, so there is nothing on screen to describe")
        return json.loads(text)

    def resolve_target(self, spec, view=None):
        """Finds what a target names on screen and returns its centre and its record.

        ``@NAME`` is a menu item by its enumerator, ``@N`` a menu item, window
        or gadget by number, ``text:STRING`` a window or gadget by the text it
        shows, and ``unit:TYPE`` one of the player's own objects on the map by
        its type or its name. Only what is visible and enabled counts.
        """

        if view is None:
            view = self.ui()

        candidates = []
        menu = view.get("menu") or {}

        if spec.startswith("@"):
            name = spec[1:]
            number = int(name) if name.lstrip("-").isdigit() else None
            for item in menu.get("items", []):
                if (number is not None and item.get("id") == number) or \
                        (item.get("name") or "").lower() == name.lower():
                    candidates.append(("menu", item))
            if number is not None:
                for window in view.get("windows", []):
                    if window.get("id") == number:
                        candidates.append(("window", window))
                for gadget in view.get("gadgets", []):
                    if gadget.get("id") == number:
                        candidates.append(("gadget", gadget))
        elif spec.startswith("text:"):
            wanted = spec[5:].strip().lower()
            for window in view.get("windows", []):
                if (window.get("text") or "").strip().lower() == wanted:
                    candidates.append(("window", window))
            for gadget in view.get("gadgets", []):
                if (gadget.get("text") or "").strip().lower() == wanted:
                    candidates.append(("gadget", gadget))
        elif spec.startswith("unit:"):
            wanted = spec[5:].strip().lower()
            for thing in view.get("objects", []):
                if (thing.get("type") or "").lower() == wanted or \
                        (thing.get("name") or "").strip().lower() == wanted:
                    candidates.append((thing.get("kind", "object"), thing))
        else:
            raise HarnessError("a target is @NAME, @N, text:STRING or unit:TYPE, not %r" % spec)

        usable = [(kind, item) for kind, item in candidates
                  if item.get("enabled", True) and item.get("visible", True) and
                  item.get("rect", [0, 0, 0, 0])[2] > 0 and item.get("rect", [0, 0, 0, 0])[3] > 0]

        if not usable:
            if candidates:
                raise HarnessError("%s is on screen but not usable: %s" %
                                   (spec, json.dumps(candidates[0][1], sort_keys=True)))
            raise HarnessError("nothing on screen is %s; %s" % (spec, self.describe(view)))

        kind, item = usable[0]
        x, y, width, height = item["rect"]
        return x + width // 2, y + height // 2, dict(item, kind=kind)

    @staticmethod
    def describe(view):
        """One line saying what the screen holds, for a failure to explain itself."""

        parts = ["phase %s" % view.get("stack")]
        menu = view.get("menu")
        if menu:
            names = [item.get("name") or str(item.get("id")) for item in menu.get("items", [])
                     if item.get("visible", True)]
            parts.append("menu %s [%s]" % (menu.get("section"), ", ".join(names)))
        texts = [window["text"] for window in view.get("windows", []) if window.get("text")]
        if texts:
            parts.append("windows [%s]" % ", ".join(repr(text) for text in texts[:12]))
        gadgets = view.get("gadgets", [])
        if gadgets:
            captions = [gadget["text"] for gadget in gadgets if gadget.get("text")]
            parts.append("%d gadgets%s" % (len(gadgets),
                                            (" [%s]" % ", ".join(captions[:8])) if captions else ""))
        objects = view.get("objects", [])
        if objects:
            types = []
            for thing in objects:
                if thing.get("type") and thing["type"] not in types:
                    types.append(thing["type"])
            parts.append("%d objects [%s]" % (len(objects), ", ".join(types[:12])))
        return "; ".join(parts)

    # -- frame stepping ---------------------------------------------------

    def gate_hold(self):
        self.evaluate("Module.OpenTS_FrameGate.hold()")
        return self._stepping()

    def gate_release(self):
        self.evaluate("Module.OpenTS_FrameGate.release()")
        return self._stepping()

    def gate_step(self, count, timeout):
        """Lets the held engine through that many frame waits and parks it again."""

        self.snapshot()
        stepping = self.last.get("stepping") or {}
        if not stepping.get("held"):
            raise HarnessError("step wants the engine held first; use hold")

        before = self.last.get("frames", 0)
        self.evaluate("Module.OpenTS_FrameGate.grant(%d)" % count)

        deadline = time.monotonic() + timeout
        while True:
            self.snapshot()
            stepping = self.last.get("stepping") or {}
            frames = self.last.get("frames", 0)
            if stepping.get("parked") and frames >= before + count:
                return {"frames": frames - before, "total": frames, "parked": True}
            if time.monotonic() >= deadline:
                raise HarnessError("the engine took %d of %d frames in %gs and did not park "
                                   "(phase %s)" % (frames - before, count, timeout,
                                                   self.last.get("phases")))
            time.sleep(0.02)

    def _stepping(self):
        self.snapshot()
        return dict(self.last.get("stepping") or {}, frames=self.last.get("frames", 0))

    # -- input ------------------------------------------------------------

    def _page_point(self, x, y):
        view = self.geometry()
        left, top = view["css"]["left"], view["css"]["top"]
        ratio = view["ratio"]
        dest_x, dest_y, dest_w, dest_h = view["dest"]
        game_w, game_h = view["game"]

        return (left + (dest_x + (x + 0.5) * dest_w / game_w) / ratio,
                top + (dest_y + (y + 0.5) * dest_h / game_h) / ratio)

    def mouse(self, kind, x, y, button="left", clicks=1, buttons=0, modifiers=0):
        page_x, page_y = self._page_point(x, y)
        self.call("Input.dispatchMouseEvent", {
            "type": kind,
            "x": page_x,
            "y": page_y,
            "button": BUTTON_NAMES.get(button, "left"),
            "buttons": buttons,
            "clickCount": clicks,
            "modifiers": modifiers,
        })

    def move(self, x, y, modifiers=0):
        self.mouse("mouseMoved", x, y, button="none", clicks=0, modifiers=modifiers)

    def press(self, x, y, button="left", modifiers=0):
        self.mouse("mousePressed", x, y, button, 1, BUTTON_MASK.get(button, 1), modifiers)

    def release(self, x, y, button="left", modifiers=0):
        self.mouse("mouseReleased", x, y, button, 1, 0, modifiers)

    def click(self, x, y, button="left", modifiers=0):
        self.move(x, y, modifiers)
        self.press(x, y, button, modifiers)
        self.release(x, y, button, modifiers)

    def wheel(self, x, y, delta_x, delta_y):
        page_x, page_y = self._page_point(x, y)
        self.call("Input.dispatchMouseEvent", {
            "type": "mouseWheel",
            "x": page_x,
            "y": page_y,
            "button": "none",
            "buttons": 0,
            "deltaX": delta_x,
            "deltaY": delta_y,
        })

    def touch(self, kind, points):
        payload = []
        for index, (x, y) in enumerate(points):
            page_x, page_y = self._page_point(x, y)
            payload.append({"x": page_x, "y": page_y, "id": index, "radiusX": 1, "radiusY": 1,
                            "force": 1.0})

        self.call("Input.dispatchTouchEvent", {
            "type": kind,
            "touchPoints": [] if kind == "touchEnd" else payload,
        })

    def key_down(self, name, modifiers=0):
        code, key, virtual, text = key_event(name)
        common = {
            "code": code,
            "key": key,
            "windowsVirtualKeyCode": virtual,
            "nativeVirtualKeyCode": virtual,
            "modifiers": modifiers,
        }
        down = dict(common, type="keyDown" if text else "rawKeyDown")
        if text:
            down["text"] = text
            down["unmodifiedText"] = text
        self.call("Input.dispatchKeyEvent", down)

    def key_up(self, name, modifiers=0):
        code, key, virtual, text = key_event(name)
        self.call("Input.dispatchKeyEvent", {
            "type": "keyUp",
            "code": code,
            "key": key,
            "windowsVirtualKeyCode": virtual,
            "nativeVirtualKeyCode": virtual,
            "modifiers": modifiers,
        })

    def key(self, name, modifiers=0):
        self.key_down(name, modifiers)
        self.key_up(name, modifiers)

    # -- observation ------------------------------------------------------

    def screenshot(self, path):
        answer = self.call("Page.captureScreenshot", {"format": "png",
                                                      "captureBeyondViewport": False},
                           timeout=90.0)
        data = base64.b64decode(answer["data"])
        with open(path, "wb") as handle:
            handle.write(data)
        picture = imaging.decode(data)
        return {"path": path, "bytes": len(data), "size": [picture.width, picture.height]}

    def profile_start(self, interval=100):
        """Starts the sampling CPU profiler. The interval is in microseconds."""
        self.call("Profiler.enable")
        self.call("Profiler.setSamplingInterval", {"interval": interval})
        self.call("Profiler.start")
        return {"interval": interval}

    def profile_stop(self, path):
        """Writes what the profiler collected as a .cpuprofile and reports its shape."""
        answer = self.call("Profiler.stop", timeout=180.0)
        profile = answer["profile"]
        self.call("Profiler.disable")

        with open(path, "w") as handle:
            json.dump(profile, handle)

        samples = profile.get("samples") or []
        span = (profile.get("endTime", 0) - profile.get("startTime", 0)) / 1e6
        return {"path": path, "samples": len(samples), "seconds": round(span, 2),
                "nodes": len(profile.get("nodes") or [])}

    def state(self, path=None):
        snapshot = self.snapshot()
        if path:
            with open(path, "w", encoding="utf-8") as handle:
                json.dump(snapshot, handle, indent=2, sort_keys=True)
        return snapshot

    def write_log(self, path=None):
        if path:
            with open(path, "w", encoding="utf-8") as handle:
                for line in self.log:
                    handle.write("%8.3f  %-9s %s\n" % (line["at"], line["level"], line["text"]))
        return len(self.log)
