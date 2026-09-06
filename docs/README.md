# Developer documentation

The developer guides are split by subject:

- [Building OpenTS](BUILDING.md) — supported toolchain, commands, outputs,
  build identity, continuous integration, and the unsupported WebAssembly
  target with what has been run on it.
- [Style](STYLE.md) — source formatting, naming, C++ use, and comments.
- [History](HISTORY.md) — source lineage and reconstruction history.
- [Rationale](RATIONALE.md) — reconstruction tools, recovered structure, and
  non-obvious implementation choices.
- [Project direction](DIRECTION.md) — long-term architecture.
- [UI system design](UI_DESIGN.md) — proposed RmlUi and ImGui integration,
  screen-level interchangeable views, and the migration from OwnerDraw.
- [WebAssembly port design](WASM-PORT.md) — how the browser target is built:
  the Win32 substitute, the main-loop yield, the data path, and the subsystems
  behind them.
- [The browser harness](HARNESS.md) — the one way to run the WebAssembly build
  in a browser: serving it, driving it, observing it, and taking it down.
- [The saved game format](SAVE-FORMAT.md) — the layout of a `.SAV` file: its
  header, listing fields, compressed content, and object records.
- [The network relay](RELAY.md) — the WebSocket relay that stands in for a
  LAN when the browser build plays a network game.

See [CONTRIBUTING.md](../CONTRIBUTING.md) for contribution and review rules.
Player and modder documentation is under [manual/](../manual/README.md). When a
guide already covers a subject, link to it instead of copying the same facts.
