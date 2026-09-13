"""Copy the native PE dependency closure from explicit SDK directories."""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

SYSTEM = set('advapi32 bcrypt crypt32 dbghelp dnsapi dwmapi gdi32 imm32 iphlpapi kernel32 kernelbase mpr msimg32 msvcrt ntdll ole32 oleaut32 powrprof psapi rpcrt4 secur32 setupapi shell32 shlwapi user32 userenv ucrtbase version winhttp wininet winmm ws2_32 wtsapi32'.split())

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--readobj', required=True)
    parser.add_argument('--search-dir', action='append', default=[], type=Path)
    parser.add_argument('--destination', required=True, type=Path)
    args = parser.parse_args()
    available = {}
    for directory in args.search_dir:
        if directory.is_dir():
            for file in directory.iterdir():
                if file.is_file() and file.suffix.lower() == '.dll':
                    available.setdefault(file.name.lower(), file.resolve())
    pending = [args.binary.resolve()]
    visited = set()
    args.destination.mkdir(parents=True, exist_ok=True)
    while pending:
        image = pending.pop()
        if image.name.lower() in visited:
            continue
        visited.add(image.name.lower())
        data = subprocess.check_output([args.readobj, '--coff-imports', str(image)], text=True, encoding='utf-8', errors='replace')
        for name in re.findall(r'^\s*Name:\s*(\S+\.dll)\s*$', data, re.M | re.I):
            key = name.lower()
            if key.startswith(('msvcp', 'vcruntime', 'msys-', 'cygwin')):
                raise RuntimeError(f'{image.name} depends on {name}; use the independent Clang SDK, not an MSVC/MSYS runtime')
            source = available.get(key)
            if source:
                target = args.destination / source.name
                if source != target.resolve() and (not target.exists() or target.stat().st_size != source.stat().st_size or target.stat().st_mtime_ns != source.stat().st_mtime_ns):
                    shutil.copy2(source, target)
                pending.append(source)
            elif key.startswith(('api-ms-', 'ext-ms-')) or Path(key).stem in SYSTEM:
                continue
            elif os.name == 'nt' and (Path(os.environ.get('SystemRoot', '')) / 'System32' / name).is_file():
                continue
            else:
                raise RuntimeError(f'Cannot resolve dependency {name} of {image}; add its SDK directory explicitly')
    print(f'Checked {len(visited)} native images; SDK runtime dependencies copied to {args.destination}')

if __name__ == '__main__':
    main()
