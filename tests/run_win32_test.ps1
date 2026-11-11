# Run the real-SCM dummy-service test. MUST be run from an ELEVATED PowerShell.
# Creates/removes only services named PTGovDummy_<pid>; never touches Cato or other services.
$ErrorActionPreference = 'Stop'
$out = Join-Path $env:TEMP ("ptgov-build-" + [guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory $out | Out-Null
try {
  gcc -O2 -Wall "$PSScriptRoot\dummysvc.c" -o "$out\dummysvc.exe" -ladvapi32
  gcc -O2 -Wall -municode -mconsole "$PSScriptRoot\test_svc_win32.c" -o "$out\test_svc_win32.exe" -lshell32 -lcomdlg32 -ladvapi32 -luser32 -lpowrprof -lgdi32
  & "$out\test_svc_win32.exe" | Tee-Object -FilePath (Join-Path $PSScriptRoot 'win32_test_result.log')
  "exit=$LASTEXITCODE"
} finally { Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue }
Get-Service PTGovDummy_* -ErrorAction SilentlyContinue | ForEach-Object { "LEFTOVER: $($_.Name) -> sc delete $($_.Name)" }
