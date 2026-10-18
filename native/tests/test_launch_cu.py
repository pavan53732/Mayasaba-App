import subprocess
import time
import os
import sys

PY = sys.executable
CU = r"C:\Users\pavan\.gemini\config\skills\computer-use\scripts\cu.py"
EXE = r"C:\Users\pavan\PROJECTS\Mayasaba-App\native\Mayasaba\bin\x64\Release\Mayasaba.exe"
BIN_DIR = r"C:\Users\pavan\PROJECTS\Mayasaba-App\native\Mayasaba\bin\x64\Release"
STORE = r"C:\Users\pavan\PROJECTS\_ui-verify\interaction_walkthrough\store.db"
WINDOW = "Engineering Control Room"

env = os.environ.copy()
env["MAYASABA_STORE_PATH"] = STORE

print(f"Launching {EXE} with store {STORE}...")
proc = subprocess.Popen([EXE], cwd=BIN_DIR, env=env)
try:
    print(f"Process PID: {proc.pid}")
    # Wait for window to appear
    found = False
    for i in range(15):
        time.sleep(1)
        r = subprocess.run([PY, CU, "windows"], capture_output=True, text=True)
        if WINDOW in r.stdout:
            print(f"Found window '{WINDOW}' after {i+1} seconds!")
            found = True
            break
        print(f"Waiting for window... ({i+1}s)")

    if not found:
        print("Window not found within 15s. Checking cu windows output:")
        r = subprocess.run([PY, CU, "windows"], capture_output=True, text=True)
        print(r.stdout)
    else:
        # Inspect UIA tree
        print("Focusing window...")
        subprocess.run([PY, CU, "focus", WINDOW], capture_output=True, text=True)
        time.sleep(1)
        print("Finding UIA elements...")
        r = subprocess.run([PY, CU, "uia", "--window", WINDOW, "--depth", "6"], capture_output=True, text=True)
        print(r.stdout[:2000])
finally:
    print(f"Terminating PID {proc.pid}...")
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except Exception:
        proc.kill()
    print("Cleaned up.")
