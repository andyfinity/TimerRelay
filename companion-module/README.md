> _This codebase was primarily written by Claude Opus 4.8 (Anthropic)._

# russellworks-timerrelay (Bitfocus Companion module)

Companion module for the TimerRelayV2 6-channel Wi-Fi relay controller.

## Develop / package

```bash
cd companion-module
npm install
npm run build      # produces a .tgz you can side-load into Companion
```

To develop against a running Companion in dev mode, point Companion's
"Developer modules path" at this folder, or use `npm run dev`.

See `companion/HELP.md` for user-facing docs. The module talks to the device
over its REST API (`GET /api/status`, `POST /api/relay`), the same API the
device's built-in web UI uses.
