import importlib.util
from pathlib import Path


def test_extension_bundle_only_copies_python_sources_and_cleans_stale_runtime(tmp_path):
    script = Path(__file__).resolve().parents[2] / 'vscode-extension/scripts/prepare_runtime.py'
    spec = importlib.util.spec_from_file_location('prepare_runtime', script)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    repo, extension = tmp_path / 'repo', tmp_path / 'extension'
    package = repo / 'hooks/psv_remote'
    (package / 'adapters').mkdir(parents=True)
    (package / '__pycache__').mkdir()
    (extension / 'scripts').mkdir(parents=True)
    (extension / 'runtime').mkdir()
    (extension / 'runtime/stale.log').write_text('old runtime data')
    (extension / 'scripts/launcher.py').write_text('# launcher')
    (repo / 'hooks/psv_codex.py').write_text('# entry')
    (repo / 'hooks/psv_kiro.py').write_text('# kiro entry')
    (package / '__init__.py').write_text('# package')
    (package / 'adapters/codex.py').write_text('# adapter')
    (package / '__pycache__/old.py').write_text('# stale')
    (package / 'linked.py').symlink_to(repo / 'hooks/psv_codex.py')
    for name in ['config.toml', 'private.key', 'runtime.log', 'state.sqlite', '.env']:
        (package / name).write_text('private fixture')
    runtime = module.copy_sources(repo, extension)
    assert (extension / 'dist').is_dir()
    files = sorted(str(p.relative_to(runtime)) for p in runtime.rglob('*') if p.is_file())
    assert files == ['launcher.py', 'psv_codex.py', 'psv_kiro.py', 'psv_remote/__init__.py', 'psv_remote/adapters/codex.py']
