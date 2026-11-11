# powertool

A small Windows tray app that periodically ends processes whose executable path is on a blacklist.

## Build

Requires MinGW-w64 GCC. Run `build.bat`.

## Use

Run `powertool.exe` (starts in the tray; double-click the icon for Settings).
Settings: Start with Windows, Enable, check interval in seconds, and the blacklist of full exe paths (matched case-insensitively against each running process's image path).
Stored in `%APPDATA%\PowerTool\settings.ini`. The Keep awake tab has an optional "Force sleep when the laptop lid is closed" checkbox: PowerTool watches the lid switch and puts the PC to sleep when it closes (it must be running at that time). Windows services whose executable is on the blacklist are also stopped and set to Disabled (PowerTool saves each service's state from before it was first governed - start type incl. delayed-auto, and running/stopped - in the `[DisabledServices]` section of settings.ini, never overwriting it on re-enable. When you turn off "Enable ending blacklisted processes" or remove the entry it restores the start type and starts the service again only if it was running before; a service that was stopped stays stopped. Killed processes are not relaunched. A service that is stopping (STOP_PENDING) counts as stopped; one that is starting counts as running. A missing service is dropped from the records and the status line says so. An unreadable record is dropped too; if that service is still Disabled it is set to Manual (not started) so it is not locked out, otherwise it is left as-is. Restore problems appear in the status line on the same scan) (PowerTool's manifest requires administrator rights, so it prompts for UAC elevation on launch). Only processes the current user can terminate are affected; PowerTool itself and System32 files cannot be blacklisted.

## Keep awake tab

"Keep screen and system awake" blocks the screensaver, display-off and sleep (`SetThreadExecutionState`). "Also simulate input" taps F15 every N seconds to reset the real input-idle counter (Teams status, lock timers).

## Tests

- `gcc -Wall test_svcstate.c -o test_svcstate.exe && test_svcstate.exe` - unit test of `svcstate.h`.
- `tests/test_svcstate_prop.c` - property test (fixed seed, 100k iterations); `sh tests/mutate.sh` - manual mutation run (28 mutants, all must be killed).
- `tests/run_win32_test.ps1` - real Windows service test using a throw-away dummy service (`PTGovDummy_<pid>`); run from an **elevated** PowerShell. Never touches other services.
