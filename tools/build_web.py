"""Build the browser application using the same Simulation.cpp as Qt.
Requires Emscripten 4.0.7+ on PATH. No Qt or npm dependencies required.
"""
from pathlib import Path
import hashlib
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / 'build-web'
out.mkdir(exist_ok=True)
compiler = shutil.which('em++') or shutil.which('em++.bat')
if not compiler:
    raise SystemExit('em++ not found; activate the Emscripten SDK environment first.')
subprocess.run([compiler, str(root/'src/Simulation.cpp'), str(root/'web/bridge.cpp'),
    '-I'+str(root/'web/compat'), '-I'+str(root/'src'), '-std=c++17', '-O3',
    '--bind', '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sALLOW_MEMORY_GROWTH=1',
    '-sENVIRONMENT=web,node', '-o', str(out/'physics.js')], check=True)
# Content-addressed entry assets prevent an updated HTML page from loading an
# older cached script (GitHub Pages caches assets for up to ten minutes).
html = (root/'web/index.html').read_text(encoding='utf-8')
for name in ['style.css', 'app.js']:
    data = (root/'web'/name).read_bytes()
    digest = hashlib.sha256(data).hexdigest()[:16]
    asset = Path(name)
    versioned = f'{asset.stem}.{digest}{asset.suffix}'
    (out/versioned).write_bytes(data)
    html = html.replace(f'./{name}', f'./{versioned}')
(out/'index.html').write_text(html, encoding='utf-8', newline='\n')
for name in ['geometry.json','app.png','app.ico']:
    shutil.copy2(root/'assets'/name, out/name)
for name in ['LICENSE','NOTICE.md']:
    shutil.copy2(root/name,out/name)
(out/'package.json').write_text('{"type":"module"}\n', encoding='utf-8')
(out/'.nojekyll').touch()
print(f'Built {out}. Serve with: python -m http.server 8080 --directory build-web')
