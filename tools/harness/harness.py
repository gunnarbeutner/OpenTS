#!/usr/bin/env python3
"""The one way to run the WebAssembly build of the engine in a browser.

Serving a build over range requests, starting a browser and taking it down
again, waiting for the engine to reach a state worth acting on, sending input in
the game's own coordinates, and comparing two screenshots are the same job every
time, and every hand-written version of it has gone wrong in the same ways: a
port two runs both chose, a profile directory nobody removed, a browser that
outlived the run that started it.  So it is done once, here.

Run ``harness.py doctor`` first; ``harness.py run --help`` lists the steps.
This is a developer tool and it reads the game data off the developer's own
asset tree, so it is deliberately not wired into CTest.
"""

import argparse
import contextlib
import json
import os
from pathlib import Path
import re
import shlex
import signal
import sys
import time
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import cdp                                       # noqa: E402
import chrome                                    # noqa: E402
import imaging                                   # noqa: E402
import serving                                   # noqa: E402
import session as session_module                 # noqa: E402
from session import HarnessError                 # noqa: E402


ROOT = Path(__file__).resolve().parents[2]

# Where OpenTS-Assets writes the publishable web tree, unless OPENTS_ASSETS
# says otherwise; the same variable the container is pointed at it with.
DEFAULT_ASSETS = "~/OpenTS-Assets/build/web"

STEP_HELP = """\
steps, given in order with --do (or one to a line with --script):

  wait <what> [seconds]   a phase: none, menu, movie, loading, game, dialog, alert
                          a milestone: module, main, frame, init, scenario, idle
                          not:<state>, any:<state>|<state>, event:<name>,
                          ui:<target>, log:<regex>, frames:+N, js:<expression>,
                          or a number of seconds
  sleep <seconds>
  try <step>              a step whose failure is reported, not fatal
  ui [path]               what is on screen, as JSON

  a position is <x> <y> in game coordinates, or a target: @NAME for a menu
  item, @N for a menu item, window or gadget by number, text:STRING for a
  window or gadget by what it shows, unit:TYPE for one of the player's own
  objects on the map

  move <position> [mods]
  click <position> [button] [mods]
                          left, middle or right; press and release
  down <position> [button] [mods]
                          press without releasing
  up <position> [button] [mods]
                          release
  drag <x1> <y1> <x2> <y2> [button] [mods]
                          mods are ctrl, alt, shift or meta, joined with +
  wheel <x> <y> <dx> <dy>
  tap <position>          a touch down and up in one place
  touch <down|move|up> <x> <y> [<x> <y> ...]
                          every finger still on the glass, in order
  key <name>              escape, enter, f1, a, 1, ctrl+s ...
  keydown <name>          press without releasing, so a modifier can be held
  keyup <name>            release a key an earlier keydown left held
  text <string>           typed one key at a time

  every input step then waits for the engine to read it and draw a frame,
  up to --settle seconds, and reports whether it did

  skip-movies             escape out of films until a phase that stays
  to-menu [tibsun|firestorm]
                          through the films and the disc chooser to a main menu
  hold                    park the engine at its next frame wait
  step [N]                let a held engine through N frame waits (1 by default)
  release                 let the engine run again

  profile start [us]      begin a sampling CPU profile, at this interval
  profile stop <path>     end it and write a .cpuprofile
  shot <path>             a PNG screenshot
  state [path]            OpenTS_State and every module counter, as JSON
  log [path]              everything the page and the engine printed
  diff <a> <b> [pixels]   compare two screenshots, failing over a pixel budget
  eval <javascript>       an escape hatch; its value is printed
  expect <javascript>     the same, but a false value fails the run
"""


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def say(text):
    print(text, flush=True)


def is_trailer(word):
    """Whether a word is a button name or a modifier chord rather than part of a target."""

    lowered = word.lower()
    if lowered in session_module.BUTTON_NAMES:
        return True
    return all(part in session_module.MODIFIERS for part in lowered.split("+"))


def mouse_trailer(words):
    """Reads the button and the held modifiers a mouse step may end with."""

    button = "left"
    modifiers = 0

    for word in words:
        lowered = word.lower()
        if lowered in session_module.BUTTON_NAMES:
            button = lowered
            continue
        for part in lowered.split("+"):
            if part not in session_module.MODIFIERS:
                raise HarnessError("%r is neither a button nor a modifier" % word)
            modifiers |= session_module.MODIFIERS[part]

    return button, modifiers


def resolve_assets(named):
    """The asset tree to serve, or None when the one looked for is not there."""

    given = named or os.environ.get("OPENTS_ASSETS") or DEFAULT_ASSETS
    tree = Path(os.path.expanduser(given))

    if (tree / "assets.json").is_file():
        return str(tree.resolve())

    # Only a tree that was asked for by name is worth stopping over.
    if named or os.environ.get("OPENTS_ASSETS"):
        raise HarnessError("no assets.json in %s" % tree)

    return None


def describe_assets(tree):
    """Reads the tree's pointer and manifest: the hash, and what is named and missing."""

    pointer = json.loads((Path(tree) / "assets.json").read_text("utf-8"))
    manifest_path = Path(tree) / pointer["manifest"]
    manifest = json.loads(manifest_path.read_text("utf-8"))

    files = manifest.get("files", [])
    missing = [entry.get("path") for entry in files
               if entry.get("path") and not (Path(tree) / entry["path"]).is_file()]

    return {
        "hash": pointer.get("sha256"),
        "files": len(files),
        "missing": missing,
        "profiles": sorted((pointer.get("profiles") or {}).keys()),
    }


def build_ini(settings):
    """Turns ``Section.Key=Value`` settings into the text of a SUN.INI."""

    sections = {}
    order = []

    for setting in settings:
        name, _, value = setting.partition("=")
        section, _, key = name.partition(".")

        if not _ or not section or not key:
            raise SystemExit("--ini wants Section.Key=Value, not %r" % setting)

        if section not in sections:
            sections[section] = []
            order.append(section)
        sections[section].append((key, value))

    lines = []
    for section in order:
        lines.append("[%s]" % section)
        for key, value in sections[section]:
            lines.append("%s=%s" % (key, value))
        lines.append("")

    return "\n".join(lines)


def page_url(server, options):
    """Builds the address of the page, with everything the run asked of it."""

    query = []

    if options.scenario:
        query.append(("scenario", options.scenario))
    if options.campaign:
        query.append(("campaign", options.campaign))
    if options.playmovie:
        query.append(("playmovie", options.playmovie))
    if options.display:
        query.append(("display", options.display))
    for argument in options.arg:
        query.append(("arg", argument))
    for extra in options.query:
        name, _, value = extra.partition("=")
        query.append((name, value))

    return server.url("/index.html") + ("?" + urllib.parse.urlencode(query) if query else "")


def game_size(options):
    """The frame the engine will be running at, when the run pinned one."""

    if options.display and "x" in options.display:
        width, _, height = options.display.partition("x")
        return [int(width), int(height)]
    return None


def read_steps(options):
    steps = []

    if options.script:
        text = sys.stdin.read() if options.script == "-" else Path(options.script).read_text("utf-8")
        for line in text.splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                steps.append(line)

    steps.extend(options.do)
    return steps


def duration(text):
    return float(text[:-1]) if text.endswith("s") else float(text)


# ---------------------------------------------------------------------------
# doctor
# ---------------------------------------------------------------------------

def command_doctor(options):
    problems = 0

    say("python           %s" % sys.version.split()[0])
    if sys.version_info < (3, 9):
        say("                 ! 3.9 or newer is wanted")
        problems += 1

    browser = chrome.find_browser(options.browser)
    if browser:
        say("browser          %s" % browser)
        say("                 %s" % chrome.version(browser))
    else:
        say("browser          ! no Chrome, Chromium or Edge found; set OPENTS_CHROME")
        problems += 1

    try:
        tree = resolve_assets(options.assets)
    except HarnessError as error:
        tree = None
        say("assets           ! %s" % error)
        problems += 1
    if tree:
        say("assets           %s" % tree)
        try:
            found = describe_assets(tree)
            say("                 manifest %s, %d files, profiles %s" %
                (found["hash"], found["files"], ", ".join(found["profiles"]) or "none"))
            if found["missing"]:
                say("                 ! %d named file(s) missing, such as %s" %
                    (len(found["missing"]), found["missing"][0]))
                problems += 1
        except (OSError, ValueError, KeyError) as error:
            say("                 ! the tree's pointer or manifest cannot be read: %s" % error)
            problems += 1
    elif not options.assets:
        say("assets           ! no assets.json under %s; set OPENTS_ASSETS or pass --assets" %
            DEFAULT_ASSETS)
        problems += 1

    if options.bin:
        directory = Path(options.bin)
        page = directory / "index.html"
        modules = sorted(p.name for p in directory.glob("*.wasm") if p.name.startswith("Game"))
        say("build            %s" % directory)
        say("                 index.html %s" % ("present" if page.is_file() else "! missing"))
        say("                 modules    %s" % (", ".join(modules) or "! none"))
        if not page.is_file() or not modules:
            problems += 1
    else:
        say("build            (none given; pass --bin to check one)")

    found = chrome.strays()
    if found:
        say("strays           ! %d browser(s) left over from an earlier run" % len(found))
        for stray in found:
            say("                   pid %d  %s" % (stray["pid"], stray["profile"]))
        if options.reap:
            ended = chrome.reap(found)
            say("                 reaped %s" % (", ".join(str(one) for one in ended) or "nothing"))
        else:
            say("                 run: %s doctor --reap" % os.path.relpath(__file__, ROOT))
            problems += 1
    else:
        say("strays           none")

    say("")
    say("ready" if problems == 0 else "%d thing(s) to fix" % problems)
    return 0 if problems == 0 else 1


# ---------------------------------------------------------------------------
# serve
# ---------------------------------------------------------------------------

def command_serve(options):
    tree = resolve_assets(options.assets)

    with contextlib.closing(serving.BuildServer(options.bin, tree)) as server:
        say("serving %s on %s" % (server.root, server.origin))
        if tree:
            say("  assets.json and assets/ -> %s" % tree)
        say("open %s" % server.url("/index.html"))
        say("ctrl-c to stop")
        try:
            while True:
                time.sleep(3600)
        except KeyboardInterrupt:
            say("")

    return 0


# ---------------------------------------------------------------------------
# diff
# ---------------------------------------------------------------------------

def command_diff(options):
    answer = imaging.compare(imaging.read(options.first), imaging.read(options.second),
                             options.threshold)
    say(json.dumps(answer, indent=2, sort_keys=True))
    return 0 if answer["identical"] or options.budget is None or \
        answer.get("differing", 0) <= options.budget else 1


# ---------------------------------------------------------------------------
# run
# ---------------------------------------------------------------------------

INPUT_STEPS = ("move", "click", "down", "up", "drag", "wheel", "tap", "touch",
               "key", "keydown", "keyup", "text")


class Runner:
    """Carries out the step list against one live session."""

    def __init__(self, page, options):
        self.page = page
        self.options = options
        self.out = Path(options.out)
        self.results = []
        self.failure = None

    def path(self, name):
        candidate = Path(name)
        return str(candidate if candidate.is_absolute() else self.out / candidate)

    def run(self, steps):
        for index, step in enumerate(steps, 1):
            words = shlex.split(step)
            if not words:
                continue

            started = time.monotonic()
            say("[%2d] %s" % (index, step))
            try:
                answer = self.dispatch(words[0], words[1:], step)
            except (HarnessError, cdp.ProtocolError) as error:
                elapsed = time.monotonic() - started
                self.results.append({"step": step, "seconds": round(elapsed, 3),
                                     "error": str(error)})
                self.failure = self.explain(step, error)
                raise
            elapsed = time.monotonic() - started

            if answer is not None:
                say("     %s" % json.dumps(answer, sort_keys=True)[:800])
            say("     %.2fs" % elapsed)

            self.results.append({"step": step, "seconds": round(elapsed, 3), "result": answer})

    def explain(self, step, error):
        """Gathers what the screen held when a step failed, as far as the page will say."""

        context = {"step": step, "error": str(error)}

        with contextlib.suppress(Exception):
            snapshot = self.page.snapshot()
            context["phase"] = snapshot.get("phases")
            context["frames"] = snapshot.get("frames")
            context["pending"] = snapshot.get("pending")
        with contextlib.suppress(Exception):
            view = self.page.ui()
            context["ui"] = view
            context["screen"] = self.page.describe(view)
        with contextlib.suppress(Exception):
            shot = self.page.screenshot(self.path("failure.png"))
            context["screenshot"] = shot["path"]

        say("")
        say("FAILED: %s" % error)
        if context.get("screen"):
            say("        on screen: %s" % context["screen"])
        if context.get("screenshot"):
            say("        screenshot: %s" % context["screenshot"])

        return context

    # -- positions and settling ------------------------------------------

    def point(self, words):
        """Reads a position off the front of a step: a target, or x and y."""

        if not words:
            raise HarnessError("a position is wanted: x y, @NAME, @N or text:STRING")

        if session_module.is_target(words[0]):
            # A target's text may hold spaces; it ends where a button or a
            # modifier word begins.
            count = 1
            while count < len(words) and not is_trailer(words[count]):
                count += 1
            x, y, item = self.page.resolve_target(" ".join(words[:count]))
            return x, y, words[count:], item

        if len(words) < 2:
            raise HarnessError("a position is x y, @NAME, @N or text:STRING")

        return int(words[0]), int(words[1]), words[2:], None

    def settled(self, before, answer=None):
        """Waits for what was sent to be read and drawn, and folds the outcome in."""

        answer = dict(answer or {})
        if not self.options.settle:
            return answer or None

        outcome = self.page.settle(before, self.options.settle)
        answer.update(outcome)
        if not outcome["drained"]:
            say("     ! not read within %gs (phase %s)" % (self.options.settle, outcome["phase"]))
        return answer

    def frames_now(self):
        return self.page.snapshot().get("frames", 0)

    # -- macros -----------------------------------------------------------

    def skip_movies(self, timeout):
        """Escapes out of each film until the engine rests somewhere."""

        deadline = time.monotonic() + timeout
        skipped = 0
        while True:
            snapshot = self.page.snapshot()
            phase = snapshot.get("phase")
            if snapshot.get("fault"):
                raise HarnessError("the engine stopped: %s" % snapshot["fault"])
            if phase in session_module.RESTING:
                return {"skipped": skipped, "phase": snapshot.get("phases")}
            if phase == "movie":
                self.page.key("escape")
                skipped += 1
                self.page.wait("not:movie", min(10.0, max(1.0, deadline - time.monotonic())))
            if time.monotonic() >= deadline:
                raise HarnessError("no resting phase within %gs; the engine is in %s" %
                                   (timeout, snapshot.get("phases")))
            time.sleep(0.2)

    def to_menu(self, side, timeout):
        """Reaches a main menu the way a player does: past the films and the disc chooser."""

        deadline = time.monotonic() + timeout
        self.skip_movies(timeout)
        self.page.wait("menu", max(1.0, deadline - time.monotonic()))

        view = self.page.ui()
        section = (view.get("menu") or {}).get("section")
        badge = "GMENU_FIRESTORM" if side == "firestorm" else "GMENU_TIBSUN"

        if section == "MainMenu":
            x, y, _ = self.page.resolve_target("@" + badge, view)
            before = self.frames_now()
            self.page.click(x, y)
            self.settled(before)
            self.page.wait("js:window.OpenTS_State.phase === 'menu' && window.OpenTS_State.phaseDetail !== 'MainMenu'",
                           max(1.0, deadline - time.monotonic()))
            view = self.page.ui()
            section = (view.get("menu") or {}).get("section")

        return {"section": section, "items": [item.get("name") or item.get("id")
                                              for item in (view.get("menu") or {}).get("items", [])
                                              if item.get("visible", True)]}

    # -- steps ------------------------------------------------------------

    def dispatch(self, name, words, step):
        page = self.page
        options = self.options

        if name == "wait":
            if not words:
                raise HarnessError("wait wants something to wait for")
            # A target may hold spaces, so only a trailing number is the timeout.
            timeout = options.timeout
            if len(words) > 1 and re.fullmatch(r"\d+(\.\d+)?", words[-1]):
                timeout = float(words[-1])
                words = words[:-1]
            target = " ".join(words)
            if target[0].isdigit():
                time.sleep(duration(target))
                return None
            snapshot = page.wait(target, timeout)
            return {"phase": snapshot.get("phases"), "frames": snapshot.get("frames"),
                    "lines": len(page.log)}

        if name == "sleep":
            time.sleep(duration(words[0]))
            return None

        if name == "try":
            if not words:
                raise HarnessError("try wants a step to attempt")
            inner = step.split(None, 1)[1]
            try:
                answer = self.dispatch(words[0], words[1:], inner)
            except HarnessError as error:
                say("     skipped: %s" % error)
                return {"skipped": str(error)}
            return {"done": answer}

        if name == "ui":
            view = page.ui()
            if words:
                with open(self.path(words[0]), "w", encoding="utf-8") as handle:
                    json.dump(view, handle, indent=2, sort_keys=True)
            say("     %s" % page.describe(view))
            return {"phase": view.get("stack"),
                    "menu": (view.get("menu") or {}).get("section"),
                    "windows": len(view.get("windows", [])),
                    "gadgets": len(view.get("gadgets", [])),
                    "path": self.path(words[0]) if words else None}

        if name == "move":
            x, y, rest, item = self.point(words)
            button, modifiers = mouse_trailer(rest)
            before = self.frames_now()
            page.move(x, y, modifiers)
            return self.settled(before, {"at": [x, y], "target": item})

        if name in ("click", "down", "up"):
            x, y, rest, item = self.point(words)
            button, modifiers = mouse_trailer(rest)
            before = self.frames_now()
            {"click": page.click, "down": page.press, "up": page.release}[name](
                x, y, button, modifiers)
            return self.settled(before, {"at": [x, y], "target": item})

        if name == "drag":
            x1, y1, x2, y2 = (int(word) for word in words[:4])
            button, modifiers = mouse_trailer(words[4:])
            mask = session_module.BUTTON_MASK.get(button, 1)
            before = self.frames_now()
            page.move(x1, y1, modifiers)
            page.press(x1, y1, button, modifiers)
            page.mouse("mouseMoved", (x1 + x2) // 2, (y1 + y2) // 2, button, 0, mask, modifiers)
            page.mouse("mouseMoved", x2, y2, button, 0, mask, modifiers)
            page.release(x2, y2, button, modifiers)
            return self.settled(before)

        if name == "wheel":
            before = self.frames_now()
            page.wheel(int(words[0]), int(words[1]), float(words[2]), float(words[3]))
            return self.settled(before)

        if name == "tap":
            x, y, rest, item = self.point(words)
            before = self.frames_now()
            page.touch("touchStart", [(x, y)])
            page.touch("touchEnd", [(x, y)])
            return self.settled(before, {"at": [x, y], "target": item})

        if name == "touch":
            kind = {"down": "touchStart", "move": "touchMove", "up": "touchEnd"}[words[0]]
            numbers = [int(word) for word in words[1:]]
            if len(numbers) % 2 != 0:
                raise HarnessError("touch wants a pair of coordinates per finger")
            points = list(zip(numbers[0::2], numbers[1::2]))
            before = self.frames_now()
            page.touch(kind, points)
            return self.settled(before)

        if name in ("key", "keydown", "keyup"):
            parts = words[0].split("+")
            modifiers = 0
            for part in parts[:-1]:
                modifiers |= session_module.MODIFIERS[part.lower()]
            before = self.frames_now()
            {"key": page.key, "keydown": page.key_down, "keyup": page.key_up}[name](
                parts[-1], modifiers)
            return self.settled(before)

        if name == "text":
            before = self.frames_now()
            for character in " ".join(words):
                page.key(character)
            return self.settled(before)

        if name == "skip-movies":
            timeout = float(words[0]) if words else options.timeout
            return self.skip_movies(timeout)

        if name == "to-menu":
            side = words[0].lower() if words else "tibsun"
            if side not in ("tibsun", "firestorm"):
                raise HarnessError("to-menu takes tibsun or firestorm, not %r" % words[0])
            timeout = float(words[1]) if len(words) > 1 else options.timeout
            return self.to_menu(side, timeout)

        if name == "hold":
            return page.gate_hold()

        if name == "step":
            count = int(words[0]) if words else 1
            timeout = float(words[1]) if len(words) > 1 else options.timeout
            return page.gate_step(count, timeout)

        if name == "release":
            return page.gate_release()

        if name == "profile":
            if not words or words[0] not in ("start", "stop"):
                raise HarnessError("profile takes start or stop <path>")
            if words[0] == "start":
                return page.profile_start(int(words[1]) if len(words) > 1 else 100)
            if len(words) < 2:
                raise HarnessError("profile stop needs a path to write")
            return page.profile_stop(self.path(words[1]))

        if name == "shot":
            return page.screenshot(self.path(words[0]))

        if name == "state":
            snapshot = page.state(self.path(words[0]) if words else None)
            return snapshot

        if name == "log":
            count = page.write_log(self.path(words[0]) if words else None)
            if not words:
                for line in page.log:
                    say("     %8.3f  %-9s %s" % (line["at"], line["level"], line["text"]))
            return {"lines": count}

        if name == "diff":
            answer = imaging.compare(imaging.read(self.path(words[0])),
                                     imaging.read(self.path(words[1])))
            budget = int(words[2]) if len(words) > 2 else None
            if budget is not None and answer.get("differing", 1) > budget:
                raise HarnessError("%s and %s differ in %d pixels, over the budget of %d" %
                                   (words[0], words[1], answer["differing"], budget))
            return answer

        if name == "eval":
            return page.evaluate(step.split(None, 1)[1])

        if name == "expect":
            expression = step.split(None, 1)[1]
            value = page.evaluate(expression)
            if not value:
                raise HarnessError("expect %s was %r" % (expression, value))
            return value

        raise HarnessError("no step is named %r" % name)


def command_run(options):
    steps = read_steps(options)
    tree = resolve_assets(options.assets)
    extras = [str(Path(os.path.expanduser(one)).resolve()) for one in options.asset]

    if not tree and not extras:
        say("no asset tree found under %s; set OPENTS_ASSETS, or pass --assets or --asset" %
            DEFAULT_ASSETS)
        return 2

    browser_path = chrome.find_browser(options.browser)
    if not browser_path:
        say("no browser found; set OPENTS_CHROME or pass --browser")
        return 2

    ini = build_ini(options.ini) if options.ini else None
    integer_scaling = any(setting.lower().startswith("video.integerscaling=y") or
                          setting.lower().startswith("video.integerscaling=true") or
                          setting.lower().startswith("video.integerscaling=1")
                          for setting in options.ini)

    Path(options.out).mkdir(parents=True, exist_ok=True)

    report = {
        "steps": [],
        "ok": False,
    }
    started = time.monotonic()
    status = 0

    # SIGTERM has to land in the main thread as an exception, or the teardown
    # below never runs and the browser outlives the run that started it.
    def terminated(*_):
        raise KeyboardInterrupt()

    previous = signal.signal(signal.SIGTERM, terminated)

    with contextlib.ExitStack() as stack:
        stack.callback(signal.signal, signal.SIGTERM, previous)

        server = serving.BuildServer(options.bin, tree, extras)
        stack.callback(server.close)
        say("serving %s on %s" % (server.root, server.origin))
        if tree:
            say("assets   %s" % tree)

        browser = chrome.Browser(browser_path,
                                 *(int(part) for part in options.window.split("x")),
                                 scale=options.scale,
                                 headless=not options.headed,
                                 autoplay=options.autoplay)
        stack.callback(browser.close)
        say("browser  %s (%s), profile %s" %
            (os.path.basename(browser_path), "headed" if options.headed else "headless",
             browser.profile))

        runner = None
        page = None
        try:
            target = browser.connection.call("Target.createTarget", {"url": "about:blank"})
            attached = browser.connection.call("Target.attachToTarget",
                                               {"targetId": target["targetId"], "flatten": True})
            page = session_module.Session(browser.connection, target["targetId"],
                                          attached["sessionId"], verbose=options.verbose)

            width, height = (int(part) for part in options.window.split("x"))
            page.call("Emulation.setDeviceMetricsOverride", {
                "width": width, "height": height,
                "deviceScaleFactor": options.scale, "mobile": False,
            })
            # Nothing in the engine or the page asks whether the device has a
            # touch screen, so leaving this on costs a mouse-only run nothing
            # and is what makes a touch step land.
            page.call("Emulation.setTouchEmulationEnabled",
                      {"enabled": options.touch, "maxTouchPoints": 5})

            if options.throttle:
                kbit, _, latency = options.throttle.partition("@")
                throughput = float(kbit) * 1000.0 / 8.0
                page.call("Network.enable")
                page.call("Network.emulateNetworkConditions", {
                    "offline": False,
                    "downloadThroughput": throughput,
                    "uploadThroughput": throughput,
                    "latency": float(latency) if latency else 0.0,
                })
                say("throttle %s kbit/s%s" %
                    (kbit, (", %sms latency" % latency) if latency else ""))

            boot = (session_module.BOOT_SCRIPT
                    .replace("__INI__", json.dumps(ini) if ini else "null")
                    .replace("__GAME__", json.dumps(game_size(options)))
                    .replace("__INTEGER__", "true" if integer_scaling else "false"))
            page.call("Page.addScriptToEvaluateOnNewDocument", {"source": boot})

            url = page_url(server, options)
            say("page     %s" % url)
            if ini:
                say("SUN.INI  %s" % " ".join(ini.split()))
            say("")

            page.open(url)

            runner = Runner(page, options)
            try:
                runner.run(steps)
                report["ok"] = True
            finally:
                report["steps"] = runner.results
                if runner.failure is not None:
                    report["failure"] = runner.failure

            if options.hold:
                say("")
                say("holding; ctrl-c or enter to finish")
                with contextlib.suppress(EOFError, KeyboardInterrupt):
                    input()

        except KeyboardInterrupt:
            say("\ninterrupted")
            status = 130
        except (HarnessError, cdp.ProtocolError) as error:
            if runner is None or runner.failure is None:
                say("")
                say("FAILED: %s" % error)
            status = 1
        finally:
            report["seconds"] = round(time.monotonic() - started, 3)
            report["served"] = server.summary()
            report["requests"] = server.raw()
            with contextlib.suppress(Exception):
                report["log"] = page.log
                report["events"] = page.events
                report["state"] = page.last

    if options.report:
        Path(options.report).write_text(json.dumps(report, indent=2, sort_keys=True), "utf-8")
        say("report   %s" % options.report)

    say("")
    say("torn down: browser ended, port %d closed, profile removed" % server.port)

    return status


# ---------------------------------------------------------------------------
# argument parsing
# ---------------------------------------------------------------------------

def add_common(parser):
    parser.add_argument("--assets", metavar="DIR",
                        help="the OpenTS-Assets web tree to serve, holding assets.json and "
                             "assets/ (default: $OPENTS_ASSETS, else %s)" % DEFAULT_ASSETS)
    parser.add_argument("--browser", help="the browser to launch (default: the first found)")


def parse(argv):
    parser = argparse.ArgumentParser(
        prog="harness.py",
        description=__doc__.splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)

    doctor = commands.add_parser("doctor", help="report what is missing")
    doctor.add_argument("--bin", help="a build directory to check as well")
    doctor.add_argument("--reap", action="store_true",
                        help="end browsers an earlier run left behind")
    add_common(doctor)
    doctor.set_defaults(handler=command_doctor)

    serve = commands.add_parser("serve", help="serve a build for a browser of one's own")
    serve.add_argument("--bin", required=True, help="the build's bin directory")
    add_common(serve)
    serve.set_defaults(handler=command_serve)

    diff = commands.add_parser("diff", help="compare two screenshots")
    diff.add_argument("first")
    diff.add_argument("second")
    diff.add_argument("--threshold", type=int, default=0,
                      help="a channel difference this large or less counts as the same")
    diff.add_argument("--budget", type=int,
                      help="fail when more than this many pixels differ")
    diff.set_defaults(handler=command_diff)

    run = commands.add_parser("run", help="serve a build, drive it, and tear it down",
                              epilog=STEP_HELP,
                              formatter_class=argparse.RawDescriptionHelpFormatter)
    run.add_argument("--bin", required=True, help="the build's bin directory")
    add_common(run)
    run.add_argument("--scenario", help="start this mission directly, as ?scenario=")
    run.add_argument("--campaign", help="start this campaign, as ?campaign=")
    run.add_argument("--playmovie", help="play this movie in place of the startup "
                     "sequence, then fall through to the menu, as ?playmovie=")
    run.add_argument("--asset", action="append", default=[], metavar="PATH",
                     help="serve this extra file at its basename, ahead of the build directory -- "
                     "a hand-built manifest.json and the archives or films it names; repeatable")
    run.add_argument("--display", help="native, scaled, or WIDTHxHEIGHT")
    run.add_argument("--arg", action="append", default=[],
                     help="an engine switch, passed through as ?arg=; repeatable")
    run.add_argument("--query", action="append", default=[],
                     help="another query parameter, as name=value; repeatable")
    run.add_argument("--ini", action="append", default=[], metavar="SECTION.KEY=VALUE",
                     help="a SUN.INI setting, written where the engine looks for one")
    run.add_argument("--window", default="1280x800", help="the page's size (default: %(default)s)")
    run.add_argument("--throttle", metavar="KBIT/S[@MS]",
                     help="cap the connection to this many kbit/s, symmetric, from the very "
                     "first request -- '4000' for 4 Mbit/s, '4000@40' to also add 40ms of "
                     "latency; unset runs at whatever the network actually offers")
    run.add_argument("--scale", type=float, default=1.0,
                     help="device pixel ratio (default: %(default)s)")
    run.add_argument("--headed", action="store_true", help="show the browser window")
    run.add_argument("--no-autoplay", dest="autoplay", action="store_false",
                     help="leave the browser's autoplay policy alone, so the page starts "
                          "silent until it is interacted with, as a visitor's does")
    run.add_argument("--no-touch", dest="touch", action="store_false",
                     help="do not present the page with a touch screen")
    run.add_argument("--hold", action="store_true", help="keep the run open after the last step")
    run.add_argument("--settle", type=float, default=3.0, metavar="SECONDS",
                     help="how long an input step waits for the engine to read it and draw "
                          "(default: %(default)s); 0 sends and moves on")
    run.add_argument("--timeout", type=float, default=180.0, metavar="SECONDS",
                     help="the default a wait gives up after")
    run.add_argument("--out", default=".", help="where a relative step path lands")
    run.add_argument("--report", help="write a JSON record of the run here")
    run.add_argument("--verbose", action="store_true", help="print the page's output as it arrives")
    run.add_argument("--do", action="append", default=[], metavar="STEP",
                     help="a step, carried out in the order given; repeatable")
    run.add_argument("--script", help="a file of steps, one to a line, or - for standard input")
    run.set_defaults(handler=command_run)

    return parser.parse_args(argv)


def main(argv=None):
    options = parse(argv if argv is not None else sys.argv[1:])
    try:
        return options.handler(options)
    except KeyboardInterrupt:
        say("\ninterrupted")
        return 130
    except (HarnessError, cdp.ProtocolError, FileNotFoundError, imaging.ImageError,
            RuntimeError, ValueError) as error:
        say("%s" % error)
        return 1


if __name__ == "__main__":
    sys.exit(main())
