"""Build the 32-bit Syringe plugin for Mental Omega 3.3.6."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parent
toolchain = os.environ.get("MOE_MINGW")
if not toolchain and os.environ.get("LOCALAPPDATA"):
    portable = Path(os.environ["LOCALAPPDATA"]) / "MOEE-BuildTools" / "mingw32" / "bin"
    if (portable / "g++.exe").is_file():
        toolchain = str(portable)
env = os.environ.copy()
if toolchain:
    env["PATH"] = toolchain + os.pathsep + env.get("PATH", "")
compiler = shutil.which("g++", path=env.get("PATH"))
if not compiler:
    sys.exit("i686 MinGW-w64 g++ not found; set MOE_MINGW to its bin directory.")
target = subprocess.check_output([compiler, "-dumpmachine"], env=env, text=True).strip()
if target != "i686-w64-mingw32":
    sys.exit(f"Expected i686-w64-mingw32, got {target}")
out = root / "dist"
out.mkdir(exist_ok=True)
sources = [root / "src" / name for name in ("UnitMarks.cpp", "MarkRules.cpp")]
subprocess.run([
    compiler, "-O2", "-Wall", "-fno-exceptions", "-fno-rtti", "-shared",
    "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
    "-o", str(out / "SA_UnitMarks.dll"), *(str(source) for source in sources),
    "-nostdlib", "-lkernel32", "-luser32", "-lgcc", "-s",
    "-Wl,--no-insert-timestamp", "-Wl,--entry,_DllMain@12",
], cwd=root, env=env, check=True)
print(f"Built {out / 'SA_UnitMarks.dll'}")
