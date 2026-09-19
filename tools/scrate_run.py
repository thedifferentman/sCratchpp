"""Run or debug a scrate project using the existing TurboWarp/LLDB bridge."""
from pathlib import Path
import os
import shutil
import signal
import subprocess
import sys


def executable(value, root, default=None):
    value = str(value or default or "")
    if not value:
        raise ValueError("An executable must be configured")
    if Path(value).is_absolute() or "/" in value or "\\" in value:
        path = (root / Path(value).expanduser()).resolve()
        if not path.is_file():
            raise ValueError("Executable not found: " + str(path))
        return str(path)
    found = shutil.which(value)
    if not found:
        raise ValueError("Executable not found on PATH: " + value)
    return found


def debugger_entry(info, explicit=None):
    root, config = Path(info["root"]), info["config"]
    configured = explicit or os.environ.get("SCRATE_DEBUGGER") or config.get("debugger")
    candidates = []
    if configured:
        candidate = (root / Path(configured).expanduser()).resolve()
        candidates = [candidate / "scratch-debug.cjs" if candidate.is_dir() else candidate]
    else:
        compiler = info.get("compiler") or config.get("scratch_llvm") or os.environ.get("SCRATCH_LLVM")
        try:
            compiler = executable(compiler, root, "scratch-llvm")
        except ValueError:
            compiler = None
        if compiler:
            prefix = Path(compiler).parent
            candidates += [prefix / "debugger/scratch-debug.cjs",
                           prefix.parent / "share/scratch-llvm/debugger/scratch-debug.cjs",
                           prefix.parent.parent / "debugger/scratch-debug.cjs"]
        candidates += [Path(__file__).resolve().parent.parent / "debugger/scratch-debug.cjs",
                       root.parent / "debugger/scratch-debug.cjs"]
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise ValueError("Debugger runtime not found. Install the compiler's debugger files or set "
                     "toolchain.local.json debugger to scratch-debug.cjs (or its directory).")


def launch_project(manifest, *, debug=False, debug_build=False, offline=False, locked=False, registry=None,
                   cache_dir=None, no_build=False, dap=False, no_open=False, node=None,
                   debugger=None, turbowarp=None, lldb_dap=None, lldb_python=None,
                   connect_timeout=None, commands=()):
    from scrate_build import build_project, project_info
    if dap and not no_build:
        raise ValueError("DAP mode requires --no-build; run scrate build --debug first")
    if dap and (no_open or commands):
        raise ValueError("--no-open and --command are for the interactive debugger, not DAP mode")
    if connect_timeout is not None and connect_timeout < 1:
        raise ValueError("--connect-timeout must be positive")
    mode = debug or debug_build
    info = project_info(Path(manifest), debug=mode) if no_build else build_project(
        Path(manifest), debug=mode, offline=offline, locked=locked,
        registry=registry, cache_dir=cache_dir)
    root, config = Path(info["root"]), info["config"]
    entry = debugger_entry(info, debugger)
    command = [executable(node or config.get("node"), root, "node")]
    if dap:
        command += [str(entry), "--dap"]
        if not debug:
            command += ["--no-debug"]
    else:
        project = Path(info["project"])
        if not project.is_file():
            raise ValueError("Project is missing; run scrate build first: " + str(project))
        if debug:
            debug_map = Path(info["debug_map"])
            if not debug_map.is_file():
                raise ValueError("Debug map is missing; run scrate build --debug first: " + str(debug_map))
            cli = entry.parent / "cli.cjs"
            if not cli.is_file():
                raise ValueError("Debugger runtime has no cli.cjs; update the debugger installation")
            command += [str(cli), "--project", str(project), "--map", str(debug_map)]
            clang = info.get("clang") or config.get("clang") or os.environ.get("SCRATCH_CLANG")
            if clang:
                command += ["--clang", executable(clang, root)]
            for text in commands:
                command += ["--command", text]
        else:
            command += [str(entry), "--run", str(project)]
            desktop = turbowarp or config.get("turbowarp")
            if desktop and not no_open:
                command += ["--turbowarp", executable(desktop, root)]
        if no_open:
            command += ["--no-open"]
        if connect_timeout is not None:
            command += ["--connect-timeout", str(connect_timeout)]
    if debug:
        lldb = lldb_dap or config.get("lldb_dap") or os.environ.get("SCRATCH_LLDB_DAP")
        if lldb:
            command += ["--lldb-dap", executable(lldb, root)]
        python = lldb_python or config.get("lldb_python") or os.environ.get("SCRATCH_LLDB_PYTHON")
        if python:
            directory = (root / Path(python).expanduser()).resolve()
            if not directory.is_dir():
                raise ValueError("LLDB Python directory not found: " + str(directory))
            command += ["--lldb-python", str(directory)]
    # Inherit streams unchanged: DAP stdout must contain protocol frames only.
    # Explicit streams put redirected handles in Windows' child handle list.
    # With close_fds=True, implicit inheritance can lose VS Code's DAP pipes.
    child = subprocess.Popen(command, cwd=root, stdin=sys.stdin, stdout=sys.stdout, stderr=sys.stderr,
                             creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0)
    try:
        result = child.wait()
    except KeyboardInterrupt:
        if child.poll() is None:
            child.send_signal(signal.CTRL_BREAK_EVENT if os.name == "nt" else signal.SIGINT)
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
        return 130
    return result if result >= 0 else 128 - result
