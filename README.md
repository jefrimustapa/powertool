# powertool

A small Windows tray app that periodically ends processes whose executable path is on a blacklist.

## Build

Requires MinGW-w64 GCC. Run `build.bat`.

## Use

Run `powertool.exe` (starts in the tray; double-click the icon for Settings).
Settings: Start with Windows, Enable, check interval in seconds, and the blacklist of full exe paths (matched case-insensitively against each running process's image path).
Stored in `%APPDATA%\PowerTool\settings.ini`. The Keep awake tab has an optional "Force sleep when the laptop lid is closed" checkbox: PowerTool watches the lid switch and puts the PC to sleep when it closes (it must be running at that time). Windows services whose executable is on the blacklist are also stopped and set to Disabled (PowerTool remembers their original start type and restores it when you turn off "Enable ending blacklisted processes" or remove the entry; a restored service is not started until Windows or you start it) (PowerTool's manifest requires administrator rights, so it prompts for UAC elevation on launch). Only processes the current user can terminate are affected; PowerTool itself and System32 files cannot be blacklisted.

## Keep awake tab

"Keep screen and system awake" blocks the screensaver, display-off and sleep (`SetThreadExecutionState`). "Also simulate input" taps F15 every N seconds to reset the real input-idle counter (Teams status, lock timers).
