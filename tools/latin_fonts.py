"""Build the Latin UI fonts for the Vita app (CJK text falls back to font.otf).

    pip install fonttools
    python tools/latin_fonts.py JetBrainsMono-Regular.ttf JetBrainsMono-Medium.ttf "SpaceGrotesk[wght].ttf" vita/assets

Sources: https://github.com/JetBrains/JetBrainsMono and https://github.com/google/fonts (ofl/spacegrotesk),
both under the SIL Open Font License.
"""

import sys
from pathlib import Path

from fontTools import subset
from fontTools.pens.recordingPen import DecomposingRecordingPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

# Basic Latin, Latin-1, general punctuation, arrows and the few symbols the UI draws.
UNICODES = [*range(0x20, 0x7F), *range(0xA0, 0x100), *range(0x2010, 0x2028), *range(0x2190, 0x2200), 0x25CF, 0x2713]


def decompose(font: TTFont) -> None:
    """Flatten composite glyphs into plain outlines.

    The Vita's FreeType ignores a component's transform: Space Grotesk builds
    “ and ‘ from a comma rotated 180°, and on the Vita they came out as ” and ’.
    """
    glyf, glyphs = font["glyf"], font.getGlyphSet()
    for name in font.getGlyphOrder():
        if not glyf[name].isComposite():
            continue
        rec = DecomposingRecordingPen(glyphs)
        glyphs[name].draw(rec)
        pen = TTGlyphPen(None)
        rec.replay(pen)
        glyf[name] = pen.glyph()
        glyf[name].recalcBounds(glyf)


# Characters these fonts lack, drawn with a glyph that looks the same. Claude
# writes U+2212 MINUS SIGN for negative numbers; the en dash matches it.
ALIASES = {0x2212: 0x2013}


def add_aliases(font: TTFont) -> None:
    for table in font["cmap"].tables:
        if not table.isUnicode():
            continue
        for cp, like in ALIASES.items():
            if cp not in table.cmap and like in table.cmap:
                table.cmap[cp] = table.cmap[like]


def subset_to(font: TTFont, dst: Path) -> None:
    opts = subset.Options()
    opts.layout_features = []
    opts.notdef_outline = True
    opts.name_IDs = ["*"]
    sub = subset.Subsetter(opts)
    sub.populate(unicodes=UNICODES)
    sub.subset(font)
    decompose(font)
    add_aliases(font)
    font.save(dst)


def main(mono: str, mono_medium: str, grotesk_variable: str, out_dir: str) -> None:
    out = Path(out_dir)
    subset_to(TTFont(mono), out / "mono.ttf")
    subset_to(TTFont(mono_medium), out / "mono-medium.ttf")
    for weight, name in ((400, "sans.ttf"), (500, "sans-medium.ttf")):
        static = instancer.instantiateVariableFont(TTFont(grotesk_variable), {"wght": weight})
        subset_to(static, out / name)


if __name__ == "__main__":
    if sys.argv[1:2] == ["--fixup"]:  # apply the fix-ups to already-built fonts in place
        for path in sys.argv[2:]:
            f = TTFont(path)
            decompose(f)
            add_aliases(f)
            f.save(path)
    else:
        main(*sys.argv[1:5])
