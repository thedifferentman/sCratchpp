"""Fetch pinned, header-only build inputs from official upstream repositories."""
from pathlib import Path
from urllib.request import Request, urlopen
import json
import time

ROOT = Path(__file__).resolve().parents[1]
TAG = 'llvmorg-22.1.8'

def fetch(url):
    for attempt in range(5):
        try:
            with urlopen(Request(url, headers={'User-Agent': 'scratch-llvm-build'}), timeout=30) as response:
                return response.read()
        except Exception:
            if attempt == 4:
                raise
            time.sleep(0.5 * (attempt + 1))

def llvm_dir(path):
    entries = json.loads(fetch(f'https://api.github.com/repos/llvm/llvm-project/contents/{path}?ref={TAG}'))
    for entry in entries:
        if entry['type'] == 'dir':
            llvm_dir(entry['path'])
        elif entry['name'].endswith('.h'):
            rel = Path(entry['path']).relative_to('llvm/include')
            dest = ROOT / 'third_party/llvm/include' / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            if not dest.exists():
                dest.write_bytes(fetch(entry['download_url']))

if __name__ == '__main__':
    targets = {
        'third_party/llvm/LICENSE.txt': f'https://raw.githubusercontent.com/llvm/llvm-project/{TAG}/llvm/LICENSE.TXT',
        'third_party/nlohmann/include/nlohmann/json.hpp': 'https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp',
        'third_party/nlohmann/LICENSE.txt': 'https://raw.githubusercontent.com/nlohmann/json/v3.12.0/LICENSE.MIT',
    }
    for name, url in targets.items():
        dest = ROOT / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        if not dest.exists():
            dest.write_bytes(fetch(url))
    llvm_dir('llvm/include/llvm-c')
    print('Fetched LLVM 22.1.8 C headers and nlohmann/json 3.12.0.')
