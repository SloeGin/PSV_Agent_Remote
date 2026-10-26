from pathlib import Path

import pytest

TTFont = pytest.importorskip("fontTools.ttLib").TTFont

ASSETS = Path(__file__).resolve().parents[2] / "vita" / "assets"


def test_every_latin_font_has_a_minus_sign():
    # Claude writes U+2212 for negative numbers; without a glyph it showed as blank on the Vita.
    for name in ("sans.ttf", "sans-medium.ttf", "mono.ttf", "mono-medium.ttf"):
        assert 0x2212 in TTFont(ASSETS / name).getBestCmap(), name
