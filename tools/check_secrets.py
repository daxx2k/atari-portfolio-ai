"""Report paths only: never print a matched credential."""
from pathlib import Path
import re,sys
root=Path(__file__).resolve().parents[1]
patterns=[re.compile(rb'sk-(?:proj-)?[A-Za-z0-9_-]{20,}'),re.compile(rb'gh[pousr]_[A-Za-z0-9]{20,}'),re.compile(rb'github_pat_[A-Za-z0-9_]{20,}')]
blocked={'aisetup.txt','aisetup.new','aisetup.bak','.env','secrets.json'}
failures=[]
for p in root.rglob('*'):
    if not p.is_file() or any(x in {'.git','build','dist','__pycache__'} for x in p.relative_to(root).parts): continue
    if p.name.lower() in blocked: failures.append(str(p.relative_to(root)));continue
    data=p.read_bytes()
    if any(x.search(data) for x in patterns): failures.append(str(p.relative_to(root)))
if failures:
    print('Credential audit failed for paths:', ', '.join(failures));sys.exit(1)
print('PASS: no private configuration files or credential patterns')
