#!/usr/bin/env python3
"""Package the dependency-free debug extension without npm/vsce or downloads."""
import json
from pathlib import Path
import zipfile
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parent

def main():
    manifest = json.loads((ROOT / 'vscode/package.json').read_text(encoding='utf-8'))
    version = manifest['version']
    output = ROOT / 'dist' / f"scratch-llvm-debugger-{version}.vsix"
    output.parent.mkdir(exist_ok=True)
    identity = f"{manifest['publisher']}.{manifest['name']}"
    vsix = f'''<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011">
 <Metadata><Identity Language="en-US" Id="{escape(identity)}" Version="{escape(version)}" Publisher="{escape(manifest['publisher'])}"/><DisplayName>{escape(manifest['displayName'])}</DisplayName><Description xml:space="preserve">{escape(manifest['description'])}</Description><Tags>debugger,scratch,llvm,turbowarp</Tags><Categories>Debuggers</Categories><Properties><Property Id="Microsoft.VisualStudio.Code.Engine" Value="^1.85.0"/><Property Id="Microsoft.VisualStudio.Code.ExtensionDependencies" Value=""/><Property Id="Microsoft.VisualStudio.Code.ExtensionPack" Value=""/><Property Id="Microsoft.VisualStudio.Code.LocalizedLanguages" Value=""/></Properties></Metadata>
 <Installation><InstallationTarget Id="Microsoft.VisualStudio.Code"/></Installation><Dependencies/>
 <Assets><Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true"/></Assets>
</PackageManifest>'''
    content_types = '''<?xml version="1.0" encoding="utf-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="json" ContentType="application/json"/><Default Extension="cjs" ContentType="application/javascript"/><Default Extension="js" ContentType="application/javascript"/><Default Extension="md" ContentType="text/markdown"/><Default Extension="vsixmanifest" ContentType="text/xml"/><Default Extension="py" ContentType="text/plain"/><Default Extension="txt" ContentType="text/plain"/></Types>'''
    required = ['scratch-debug.cjs', 'llvmdbg.cjs', 'websocket.cjs', 'engine.js']
    for name in required:
        if not (ROOT / name).is_file():
            raise SystemExit(f'Missing debugger runtime: {name}')
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('extension.vsixmanifest', vsix)
        archive.writestr('[Content_Types].xml', content_types)
        for file in (ROOT / 'vscode').rglob('*'):
            if file.is_file():
                archive.write(file, 'extension/' + file.relative_to(ROOT / 'vscode').as_posix())
        for file in ROOT.iterdir():
            if file.is_file() and file.suffix in ('.cjs', '.js', '.py') and file.name != 'pack_extension.py':
                archive.write(file, 'extension/runtime/' + file.name)
        if (ROOT / 'README.md').exists():
            archive.write(ROOT / 'README.md', 'extension/README.md')
    print(output)

if __name__ == '__main__':
    main()
