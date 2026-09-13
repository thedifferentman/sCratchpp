#!/usr/bin/env python3
"""Diagnose LLDB-DAP, optionally prepare its private Windows Python 3.11 runtime.

No system installation, registry change, or launch-time download is performed.
Linux/macOS use the Python runtime supplied with their LLDB distribution.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PYTHON = ROOT / "build/debugger/python311"
PYTHON_URL = "https://www.python.org/ftp/python/3.11.9/python-3.11.9-embed-amd64.zip"
# Downloaded from python.org; MD5 cross-checked against its official release page.
# SHA256 additionally pins that exact upstream archive for subsequent downloads.
PYTHON_SHA256 = "009d6bf7e3b2ddca3d784fa09f90fe54336d5b60f0e0f305c37f400bf83cfd3b"
PYTHON_MD5 = "6d9aa08531d48fcc261ba667e2df17c4"


def find_dap(explicit=None):
    if explicit:
        found = shutil.which(explicit) or explicit
        candidate = Path(found).expanduser().resolve()
        if not candidate.is_file():
            raise ValueError(f"LLDB-DAP not found: {explicit}")
        return candidate
    for name in ["lldb-dap", "lldb-vscode", *[f"lldb-dap-{n}" for n in range(22, 16, -1)]]:
        found = shutil.which(name)
        if found:
            return Path(found).resolve()
    if sys.platform == "win32":
        candidate = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "LLVM/bin/lldb-dap.exe"
        if candidate.is_file():
            return candidate
    raise ValueError("LLDB-DAP is missing. Install an LLVM distribution containing lldb-dap "
                     "(or lldb-vscode), then pass --lldb-dap /path/to/lldb-dap.")


def prepare_python(directory):
    if sys.platform != "win32" or platform.machine().lower() not in {"amd64", "x86_64"}:
        raise ValueError("--prepare-python is only for Windows x86-64 LLDB requiring python311.dll. "
                         "Use your LLDB distribution's matching Python runtime on other platforms.")
    cache = directory.parent / "downloads"
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / PYTHON_URL.rsplit("/", 1)[1]
    if not archive.is_file() or hashlib.sha256(archive.read_bytes()).hexdigest() != PYTHON_SHA256:
        print(f"Downloading {PYTHON_URL}", flush=True)
        request = urllib.request.Request(PYTHON_URL, headers={"User-Agent": "scratch-llvm-debugger-bootstrap/1"})
        with urllib.request.urlopen(request, timeout=120) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != PYTHON_SHA256:
            raise ValueError("Python download SHA256 mismatch; refusing to extract it")
        archive.write_bytes(data)
    directory.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as package:
        for member in package.infolist():
            destination = (directory / member.filename).resolve()
            if not destination.is_relative_to(directory.resolve()):
                raise ValueError("Unexpected path in Python archive")
        package.extractall(directory)
    (directory / "scratch-debugger-python.json").write_text(json.dumps({
        "version": "3.11.9", "url": PYTHON_URL, "sha256": PYTHON_SHA256,
        "upstream_release_md5": PYTHON_MD5,
        "release": "https://www.python.org/downloads/release/python-3119/",
    }, indent=2) + "\n", encoding="utf-8")
    return directory


def environment(python_directory):
    env = os.environ.copy()
    # Inherit no unrelated Python installation into LLDB's embedded interpreter.
    env.pop("PYTHONHOME", None)
    env.pop("PYTHONPATH", None)
    if python_directory and sys.platform == "win32":
        env["PATH"] = str(python_directory) + os.pathsep + env.get("PATH", "")
    return env


def dap_initialize(executable, env):
    message = json.dumps({"seq": 1, "type": "request", "command": "initialize",
                          "arguments": {"adapterID": "scratch-llvm", "clientID": "bootstrap"}}).encode()
    wire = f"Content-Length: {len(message)}\r\n\r\n".encode() + message
    process = subprocess.Popen([str(executable)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, env=env)
    try:
        # EOF normally causes LLDB-DAP to finish. Some distributions remain alive;
        # preserve their initialize response before terminating the diagnostic.
        output, errors = process.communicate(wire, timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        output, errors = process.communicate()
    if not re.search(rb'"command"\s*:\s*"initialize"', output) or not re.search(rb'"success"\s*:\s*true', output):
        raise ValueError(f"LLDB-DAP initialize failed (exit {process.returncode}). "
                         + errors.decode("utf-8", errors="replace")[-2000:])
    return {"initialize": "passed", "stderr": errors.decode("utf-8", errors="replace").strip()}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lldb-dap", help="Installed lldb-dap/lldb-vscode executable")
    parser.add_argument("--python-dir", type=Path, default=DEFAULT_PYTHON,
                        help="Private Windows Python directory; also the explicit preparation destination")
    parser.add_argument("--prepare-python", action="store_true",
                        help="Explicitly download and unpack official Python 3.11.9 embed x64 on Windows")
    options = parser.parse_args(argv)
    if sys.platform == "win32":
        # A missing DLL should be a diagnostic, not an interactive system dialog.
        import ctypes
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    dap = find_dap(options.lldb_dap)
    directory = options.python_dir.expanduser().resolve()
    if options.prepare_python:
        prepare_python(directory)
    if not directory.is_dir():
        directory = None
    env = environment(directory)
    lldb = dap.with_name("lldb.exe" if sys.platform == "win32" else "lldb")
    report = {"lldb_dap": str(dap), "lldb_python": str(directory) if directory else None}
    if lldb.is_file():
        with tempfile.TemporaryDirectory(prefix="scratch-lldb-probe-") as temporary:
            probe = Path(temporary) / "python.json"
            code = ("script import json, encodings, lldb; "
                    f"open({str(probe)!r}, 'w').write(json.dumps({{'python': 'ok'}}))")
            result = subprocess.run([str(lldb), "--batch", "-o", "version", "-o", code],
                                    env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
            python_ok = probe.is_file() and json.loads(probe.read_text()) == {"python": "ok"}
        text = result.stdout.decode("utf-8", errors="replace")
        errors = result.stderr.decode("utf-8", errors="replace")
        if result.returncode or not python_ok:
            hint = " On Windows LLVM builds requiring Python 3.11, retry with --prepare-python." if sys.platform == "win32" else ""
            raise ValueError(f"LLDB/Python probe failed (exit {result.returncode}).{hint}\n{text}\n{errors}")
        report["lldb"] = text.split("(lldb) script", 1)[0].strip()
        report["python_imports"] = "json, encodings, lldb: passed"
    report.update(dap_initialize(dap, env))
    print(json.dumps(report, indent=2, ensure_ascii=False))
    if directory and sys.platform == "win32":
        print("\nVS Code launch.json: " + json.dumps({"lldbDap": str(dap), "lldbPython": str(directory)}))
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Debugger setup: {error}", file=sys.stderr)
        sys.exit(1)
