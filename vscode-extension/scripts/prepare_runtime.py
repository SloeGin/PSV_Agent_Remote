"""Assemble release files from production sources; never copy local probes/config."""

from pathlib import Path
import shutil
import subprocess
import sys

def copy_sources(repo, extension):
    (extension / 'dist').mkdir(parents=True, exist_ok=True)
    runtime = extension / 'runtime'
    if runtime.exists():
        shutil.rmtree(runtime)
    runtime.mkdir()
    shutil.copy2(repo / 'hooks/psv_codex.py', runtime)
    shutil.copy2(repo / 'hooks/psv_kiro.py', runtime)
    source = repo / 'hooks/psv_remote'
    for path in source.rglob('*.py'):
        relative = path.relative_to(source)
        if path.is_symlink() or any(part.startswith('.') or part == '__pycache__' for part in relative.parts):
            continue
        target = runtime / 'psv_remote' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
    shutil.copy2(extension / 'scripts/launcher.py', runtime)
    (runtime / 'vendor').mkdir()
    return runtime


def main():
    extension = Path(__file__).resolve().parents[1]
    runtime = copy_sources(extension.parent, extension)
    subprocess.run([sys.executable, '-m', 'pip', 'download', '--only-binary=:all:',
                    '--platform', 'any', '--implementation', 'py', '--abi', 'none', '--python-version', '311',
                    '--dest', str(runtime / 'vendor'), 'paho-mqtt==2.1.0', 'websockets==15.0.1'], check=True)
    print('Packaged bridge and pinned pure Python wheels. No user configuration included.')


if __name__ == '__main__':
    main()
