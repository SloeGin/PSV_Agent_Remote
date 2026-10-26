"""Subset Noto Sans SC to GB2312 + ASCII + the UI's symbols for the Vita app.

    pip install fonttools
    python tools/subset_font.py NotoSansSC-Regular.otf vita/assets/font.otf

The source font is from https://github.com/notofonts/noto-cjk (SIL Open Font License).
"""

import sys

from fontTools import subset

EXTRA = "●○◀▶✓✕×□△…·—【】「」（）：，。、？！"


def gb2312_chars() -> str:
    chars = []
    for hi in range(0xA1, 0xF8):
        if 0xAA <= hi <= 0xAF:  # unassigned rows
            continue
        for lo in range(0xA1, 0xFF):
            try:
                chars.append(bytes([hi, lo]).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    return "".join(chars)


def main(src: str, dst: str) -> None:
    text = "".join(chr(c) for c in range(0x20, 0x7F)) + gb2312_chars() + EXTRA
    opts = subset.Options()
    opts.layout_features = []  # the renderer does no shaping
    opts.name_IDs = ["*"]
    opts.notdef_outline = True
    font = subset.load_font(src, opts)
    sub = subset.Subsetter(opts)
    sub.populate(text=text)
    sub.subset(font)
    subset.save_font(font, dst, opts)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
