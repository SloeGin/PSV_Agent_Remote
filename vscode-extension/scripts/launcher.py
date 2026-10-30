"""Packaged entry: use bundled pure Python wheels for this process and its children."""

import os
from pathlib import Path
import runpy
import sys

if sys.version_info < (3, 11):
    print('PSV Agent Remote requires Python 3.11 or newer.', file=sys.stderr)
    raise SystemExit(2)

root = Path(__file__).resolve().parent
paths = [str(root), *[str(wheel) for wheel in sorted((root / 'vendor').glob('*.whl'))]]
sys.path[:0] = paths
os.environ['PYTHONPATH'] = os.pathsep.join(paths + ([os.environ['PYTHONPATH']] if os.environ.get('PYTHONPATH') else []))
# `launcher.py kiro …` runs the Kiro bridge (started from the panel); anything else is psv_codex.py.
entry = 'psv_codex.py'
if sys.argv[1:2] == ['kiro']:
    entry = 'psv_kiro.py'
    del sys.argv[1]
sys.argv[0] = str(root / entry)
runpy.run_path(str(root / entry), run_name='__main__')
