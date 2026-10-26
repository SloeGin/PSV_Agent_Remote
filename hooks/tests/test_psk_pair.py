import re
import stat
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import psk_pair  # noqa: E402

LINE = re.compile(r"^psv:[0-9a-f]{64}\n$")


def test_writes_both_files_same_key(tmp_path):
    psk_pair.generate(tmp_path, "psv", force=False)
    a = (tmp_path / "psk.txt").read_text()
    b = (tmp_path / "mosquitto.psk").read_text()
    assert a == b and LINE.match(a)


def test_files_are_0600(tmp_path):
    psk_pair.generate(tmp_path, "psv", force=False)
    for name in ("psk.txt", "mosquitto.psk"):
        assert stat.S_IMODE((tmp_path / name).stat().st_mode) == 0o600


def test_refuses_overwrite(tmp_path):
    psk_pair.generate(tmp_path, "psv", force=False)
    first = (tmp_path / "psk.txt").read_text()
    with pytest.raises(SystemExit):
        psk_pair.generate(tmp_path, "psv", force=False)
    assert (tmp_path / "psk.txt").read_text() == first
    psk_pair.generate(tmp_path, "psv", force=True)
    assert (tmp_path / "psk.txt").read_text() != first


def test_never_prints_key(tmp_path, capsys):
    psk_pair.main([str(tmp_path)])
    key = (tmp_path / "psk.txt").read_text().strip().split(":")[1]
    out = capsys.readouterr()
    assert key not in out.out and key not in out.err
    assert "Do not send these files through chat or email." in out.out
