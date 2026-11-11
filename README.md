# powertool

A small Windows tray app that periodically ends processes whose executable path is on a blacklist.

## Build

Requires MinGW-w64 GCC. Run `build.bat`.

## Use

Run `powertool.exe` (starts in the tray; double-click the icon for Settings).
Settings: Start with Windows, Enable, check interval in seconds, and the blacklist of full exe paths (matched case-insensitively against each running process's image path).
Stored in `%APPDATA%\PowerTool\settings.ini`. The Keep awake tab has an optional "Force sleep when the laptop lid is closed" checkbox: PowerTool watches the lid switch and puts the PC to sleep when it closes (it must be running at that time). Windows services whose executable is on the blacklist are also stopped and set to Disabled (PowerTool saves each service's state from before it was first governed - start type incl. delayed-auto, and running/stopped - in the `[DisabledServices]` section of settings.ini, never overwriting it on re-enable. When you turn off "Enable ending blacklisted processes" or remove the entry it restores the start type and starts the service again only if it was running before; a service that was stopped stays stopped. Killed processes are not relaunched. A missing service or unreadable record is dropped, the service is left as-is, and the status line says so) (PowerTool's manifest requires administrator rights, so it prompts for UAC elevation on launch). Only processes the current user can terminate are affected; PowerTool itself and System32 files cannot be blacklisted.

## Keep awake tab

"Keep screen and system awake" blocks the screensaver, display-off and sleep (`SetThreadExecutionState`). "Also simulate input" taps F15 every N seconds to reset the real input-idle counter (Teams status, lock timers).
