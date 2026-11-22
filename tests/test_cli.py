"""test_cli.py - Unit test suite for PowerTool CLI interface.
Verifies --help, --status, --govern on, --govern off, and error handling.
Usage: python tests/test_cli.py
"""
import os
import sys
import subprocess
import time

EXE = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "powertool.exe"))

def run_cli(*args):
    res = subprocess.run([EXE, *args], capture_output=True, text=True)
    return res.returncode, res.stdout.strip(), res.stderr.strip()

def test_help():
    print("[1] Testing --help and -h...")
    code, out, _ = run_cli("--help")
    assert code == 0, f"Expected 0, got {code}"
    assert "PowerTool CLI" in out, "Help text missing header"
    assert "--govern" in out, "Help text missing --govern"
    assert "--status" in out, "Help text missing --status"

    code_h, out_h, _ = run_cli("-h")
    assert code_h == 0
    assert out_h == out
    print("    PASS: --help verified")

def test_invalid_args():
    print("[2] Testing invalid arguments...")
    code, out, _ = run_cli("--unknown-flag")
    assert code == 1, f"Expected exit code 1, got {code}"
    assert "unrecognized option" in out, "Error message missing"

    code, out, _ = run_cli("--govern")
    assert code == 1
    assert "requires 'on' or 'off'" in out

    code, out, _ = run_cli("--govern", "maybe")
    assert code == 1
    assert "invalid argument" in out
    print("    PASS: invalid args rejected correctly")

def test_status():
    print("[3] Testing --status...")
    code, out, _ = run_cli("--status")
    assert code == 0, f"Expected 0, got {code}"
    assert "PowerTool:" in out, f"Unexpected status output: {out}"
    assert "App Govern:" in out, f"Status output missing App Govern: {out}"
    print(f"    Status output: {out}")
    print("    PASS: --status verified")

def test_live_govern_toggle():
    print("[4] Testing live in-process govern toggle...")
    # 1. Turn ON
    code, out, _ = run_cli("--govern", "on")
    assert code == 0, f"Failed govern on: {out}"
    assert "turned ON" in out or "App Govern = ON" in out
    
    code, out_st, _ = run_cli("--status")
    assert "App Govern: ON" in out_st

    # 2. Turn OFF
    code, out, _ = run_cli("--govern", "off")
    assert code == 0, f"Failed govern off: {out}"
    assert "turned OFF" in out or "App Govern = OFF" in out

    code, out_st, _ = run_cli("--status")
    assert "App Govern: OFF" in out_st
    print("    PASS: live toggle ON and OFF verified")

if __name__ == "__main__":
    print(f"Running PowerTool CLI tests against {EXE}")
    assert os.path.exists(EXE), f"Binary missing: {EXE}"
    try:
        test_help()
        test_invalid_args()
        test_status()
        test_live_govern_toggle()
        print("\n==========================================")
        print(" ALL POWERTOOL CLI UNIT TESTS PASSED ")
        print("==========================================")
    except AssertionError as e:
        print(f"\nTEST FAILED: {e}")
        sys.exit(1)
