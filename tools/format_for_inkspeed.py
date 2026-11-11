#!/usr/bin/env python3
"""Format any text-based file into a .txt the inkSpeed RSVP reader can show.

inkSpeed (this firmware) is a streaming UTF-8 tokenizer, not a document
parser. It:

  * splits tokens on whitespace only (space, tab, CR, LF, FF, VT, NBSP)
  * treats a blank line (two or more newlines) as a paragraph pause
  * treats a token ending in . ! ? or … as a sentence end (Mr. pauses)
  * treats a token ending in , ; : – — as a clause
  * splits any token longer than 96 bytes into pieces
  * draws only these glyphs: ASCII, Latin-1 (U+00A0–U+00FF),
    – — ‘ ’ “ ” and …. Everything else is a missing-glyph box.

This script turns plain text, Markdown, HTML, RTF, and OCR dumps into a
single UTF-8 .txt that stays inside that set and keeps paragraph/sentence
structure the reader can pause on.

Usage:
    python3 format_for_inkspeed.py input.html
    python3 format_for_inkspeed.py scan.txt -o books/scan.txt
    python3 format_for_inkspeed.py chapter.md --keep-headings
    python3 format_for_inkspeed.py ocr.txt --no-dehyphenate

Stdlib only.
"""

from __future__ import annotations

import argparse
import html
import re
import sys
import unicodedata
from html.parser import HTMLParser
from pathlib import Path

# Glyphs the built-in faces actually have (from src/font_data.c).
_ASCII = set(range(0x20, 0x7F))
_LATIN1 = set(range(0xA0, 0x100))
_EXTRA = {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026}
SUPPORTED = _ASCII | _LATIN1 | _EXTRA

# Characters that are whitespace to the tokenizer. Keep only these as breaks.
TOKEN_WS = {" ", "\t", "\n", "\r", "\f", "\v", "\u00A0"}

# OCR / typesetting ligatures and lookalikes → something the font can draw.
LIGATURES = {
    "\uFB00": "ff", "\uFB01": "fi", "\uFB02": "fl", "\uFB03": "ffi",
    "\uFB04": "ffl", "\uFB05": "st", "\uFB06": "st",
    "\u0152": "OE", "\u0153": "oe", "\u00C6": "AE", "\u00E6": "ae",
    "\u0132": "IJ", "\u0133": "ij",
    "\u00DF": "ss",
}

# Dashes, quotes, spaces, bullets, ellipsis variants → supported forms.
PUNCT_MAP = {
    "\u2010": "-", "\u2011": "-", "\u2012": "-", "\u2015": "\u2014",
    "\u2212": "-", "\uFE58": "-", "\uFE63": "-", "\uFF0D": "-",
    "\u00AD": "",                       # soft hyphen: drop, dehyphenate handles the rest
    "\u2018": "\u2018", "\u2019": "\u2019", "\u201A": "\u2018", "\u201B": "\u2019",
    "\u201C": "\u201C", "\u201D": "\u201D", "\u201E": "\u201C", "\u201F": "\u201D",
    "\u00AB": '"', "\u00BB": '"',
    "\u2039": "'", "\u203A": "'",
    "\u201E": "\u201C",
    "\u2026": "\u2026", "\u22EF": "\u2026",
    "\u00B7": " ", "\u2022": "*", "\u2023": "*", "\u2043": "-",
    "\u25E6": "*", "\u25AA": "*", "\u25AB": "*", "\u25CF": "*",
    "\u00A0": " ", "\u2000": " ", "\u2001": " ", "\u2002": " ",
    "\u2003": " ", "\u2004": " ", "\u2005": " ", "\u2006": " ",
    "\u2007": " ", "\u2008": " ", "\u2009": " ", "\u200A": " ",
    "\u202F": " ", "\u205F": " ", "\u3000": " ",
    "\u200B": "", "\u200C": "", "\u200D": "", "\u2060": "", "\uFEFF": "",
    "\u00B0": " deg", "\u2122": "(TM)", "\u00AE": "(R)", "\u00A9": "(C)",
    "\u2020": "+", "\u2021": "++",
    "\u2032": "'", "\u2033": '"',
    "\u00D7": "x", "\u00F7": "/",
    "\u2190": "<-", "\u2192": "->", "\u2194": "<->",
    "\u2264": "<=", "\u2265": ">=", "\u2260": "!=",
    "\u00B1": "+/-",
}

# Superscript / subscript digits and a few letters.
SUPER_SUB = str.maketrans({
    "\u2070": "0", "\u00B9": "1", "\u00B2": "2", "\u00B3": "3",
    "\u2074": "4", "\u2075": "5", "\u2076": "6", "\u2077": "7",
    "\u2078": "8", "\u2079": "9",
    "\u2080": "0", "\u2081": "1", "\u2082": "2", "\u2083": "3",
    "\u2084": "4", "\u2085": "5", "\u2086": "6", "\u2087": "7",
    "\u2088": "8", "\u2089": "9",
    "\u207A": "+", "\u207B": "-", "\u207C": "=",
})

# Fullwidth ASCII (common in OCR of CJK-adjacent scans) → ASCII.
FULLWIDTH = str.maketrans({
    chr(0xFF01 + i): chr(0x21 + i) for i in range(0x7E - 0x21)
})

# A line that is only a page number, or "Page 12", or "- 12 -".
PAGE_NUM_RE = re.compile(
    r"^\s*(?:"
    r"(?:page|pg\.?|p\.?)\s*\d{1,4}"
    r"|(?:-|\u2013|\u2014|\*|\[|\()\s*\d{1,4}\s*(?:-|\u2013|\u2014|\*|\]|\))"
    r"|\d{1,4}"
    r")\s*$",
    re.IGNORECASE,
)

# Running header/footer: same short line repeated many times.
HYPHEN_BREAK_RE = re.compile(r"([A-Za-z])-\n([a-z])")
MULTI_BLANK_RE = re.compile(r"\n{3,}")
SPACE_RUN_RE = re.compile(r"[ \t]{2,}")
SPACE_BEFORE_PUNCT_RE = re.compile(r"\s+([,.;:!?])")
# OCR often inserts a space before an apostrophe inside a word: "it 's"
INNER_APOS_RE = re.compile(r"(\w)\s+(['\u2019])(\w)")


class _HTMLText(HTMLParser):
    """Pull readable text out of HTML. Block tags become paragraph breaks."""

    _BLOCK = {
        "p", "div", "br", "li", "tr", "h1", "h2", "h3", "h4", "h5", "h6",
        "blockquote", "pre", "section", "article", "header", "footer",
        "figcaption", "dt", "dd",
    }
    _SKIP = {"script", "style", "noscript", "svg", "head"}

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.parts: list[str] = []
        self._skip = 0

    def handle_starttag(self, tag: str, attrs) -> None:
        tag = tag.lower()
        if tag in self._SKIP:
            self._skip += 1
            return
        if self._skip:
            return
        if tag in self._BLOCK:
            self.parts.append("\n\n")
        if tag == "br":
            self.parts.append("\n")

    def handle_endtag(self, tag: str) -> None:
        tag = tag.lower()
        if tag in self._SKIP and self._skip:
            self._skip -= 1
            return
        if self._skip:
            return
        if tag in self._BLOCK:
            self.parts.append("\n\n")

    def handle_data(self, data: str) -> None:
        if not self._skip:
            self.parts.append(data)

    def text(self) -> str:
        return "".join(self.parts)


def read_bytes(path: Path) -> bytes:
    return path.read_bytes()


def decode_bytes(raw: bytes) -> str:
    """Best-effort decode of a text file. BOM wins; else UTF-8, then Latin-1."""
    if raw.startswith(b"\xef\xbb\xbf"):
        return raw[3:].decode("utf-8", errors="replace")
    if raw.startswith(b"\xff\xfe"):
        return raw[2:].decode("utf-16-le", errors="replace")
    if raw.startswith(b"\xfe\xff"):
        return raw[2:].decode("utf-16-be", errors="replace")
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        pass
    # cp1252 covers the bytes Latin-1 would turn into controls.
    try:
        return raw.decode("cp1252")
    except UnicodeDecodeError:
        return raw.decode("latin-1", errors="replace")


def strip_rtf(text: str) -> str:
    """Very small RTF stripper: drop control words, keep literal text."""
    if not text.lstrip().startswith("{\\rtf"):
        return text
    text = re.sub(r"\\'[0-9a-fA-F]{2}", lambda m: chr(int(m.group(0)[2:], 16)), text)
    text = re.sub(r"\\par\b", "\n\n", text)
    text = re.sub(r"\\line\b", "\n", text)
    text = re.sub(r"\\tab\b", " ", text)
    text = re.sub(r"\\[a-zA-Z]+-?\d* ?", "", text)
    text = text.replace("{", "").replace("}", "")
    return text


def strip_html(text: str) -> str:
    if "<" not in text or ">" not in text:
        return text
    if not re.search(r"<(html|body|p|div|br|span|h[1-6]|article)\b", text, re.I):
        return text
    parser = _HTMLText()
    try:
        parser.feed(text)
        parser.close()
    except Exception:
        return text
    return html.unescape(parser.text())


def strip_markdown(text: str) -> str:
    """Remove the markup that would otherwise be read aloud as tokens."""
    text = re.sub(r"```.*?```", "\n", text, flags=re.S)
    text = re.sub(r"`([^`]*)`", r"\1", text)
    text = re.sub(r"!\[[^\]]*\]\([^)]*\)", "", text)
    text = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"^#{1,6}\s*", "", text, flags=re.M)
    text = re.sub(r"^>\s?", "", text, flags=re.M)
    text = re.sub(r"^(\s*)[-*+]\s+", r"\1", text, flags=re.M)
    text = re.sub(r"^(\s*)\d+\.\s+", r"\1", text, flags=re.M)
    text = re.sub(r"(\*\*|__)(.*?)\1", r"\2", text)
    text = re.sub(r"(\*|_)(.*?)\1", r"\2", text)
    text = re.sub(r"^[-*_]{3,}\s*$", "", text, flags=re.M)
    return text


def map_char(ch: str, fold_unknown: bool) -> str:
    if ch in LIGATURES:
        return LIGATURES[ch]
    if ch in PUNCT_MAP:
        return PUNCT_MAP[ch]
    cp = ord(ch)
    if cp in SUPPORTED:
        return ch
    if ch in "\n\r\t":
        return ch
    # Drop the C0/C1 controls the font cannot draw and the tokenizer ignores.
    if cp < 0x20 or 0x7F <= cp <= 0x9F:
        return ""
    if fold_unknown:
        # NFKD splits "é" into "e" + accent; keep the base if it is supported.
        decomp = unicodedata.normalize("NFKD", ch)
        kept = []
        for piece in decomp:
            p = ord(piece)
            if p in SUPPORTED:
                kept.append(piece)
            elif unicodedata.category(piece).startswith("M"):
                continue
        if kept:
            return "".join(kept)
        # Last resort: ASCII name-ish placeholder would be noisy. Drop it.
        return ""
    return ""


def normalize_chars(text: str, fold_unknown: bool) -> str:
    text = text.translate(SUPER_SUB).translate(FULLWIDTH)
    return "".join(map_char(ch, fold_unknown) for ch in text)


def dehyphenate(text: str) -> str:
    """Rejoin words an OCR engine split at a line-end hyphen."""
    prev = None
    while prev != text:
        prev = text
        text = HYPHEN_BREAK_RE.sub(r"\1\2", text)
    return text


def drop_page_furniture(text: str) -> str:
    """Drop lines that are only a page number, and short lines repeated often."""
    lines = text.splitlines()
    counts: dict[str, int] = {}
    stripped = [ln.strip() for ln in lines]
    for s in stripped:
        if 0 < len(s) <= 60:
            counts[s] = counts.get(s, 0) + 1
    # A header/footer repeated on many pages. Require several hits and a
    # short line so real repeated prose is not removed.
    repeated = {s for s, n in counts.items() if n >= 4 and len(s) <= 40}
    kept = []
    for raw, s in zip(lines, stripped):
        if not s:
            kept.append("")
            continue
        if PAGE_NUM_RE.match(s):
            continue
        if s in repeated:
            continue
        kept.append(raw)
    return "\n".join(kept)


def tidy_whitespace(text: str, keep_single_breaks: bool) -> str:
    text = text.replace("\r\n", "\n").replace("\r", "\n").replace("\f", "\n\n")
    # A single newline is just whitespace to the tokenizer, so fold wrapped
    # lines into the paragraph. A blank line is the paragraph pause.
    paragraphs = re.split(r"\n\s*\n", text)
    cleaned = []
    for para in paragraphs:
        para = para.strip()
        if not para:
            continue
        if keep_single_breaks:
            para = SPACE_RUN_RE.sub(" ", para)
        else:
            para = para.replace("\n", " ")
            para = SPACE_RUN_RE.sub(" ", para)
        para = SPACE_BEFORE_PUNCT_RE.sub(r"\1", para)
        para = INNER_APOS_RE.sub(r"\1\2\3", para)
        cleaned.append(para)
    return "\n\n".join(cleaned)


def enforce_token_limit(text: str, limit: int = 90) -> str:
    """Split any whitespace-delimited token longer than the firmware's 96-byte cap.

    The reader already splits at 96 bytes, but a pre-split on a hyphen or
    slash keeps the pieces readable instead of cutting mid-glyph.
    """
    out_paras = []
    for para in text.split("\n\n"):
        words = []
        for word in para.split(" "):
            if len(word.encode("utf-8")) <= limit:
                words.append(word)
                continue
            # Prefer a break at - or / so the reader does not cut a codepoint.
            chunk = word
            while len(chunk.encode("utf-8")) > limit:
                cut = limit
                raw = chunk.encode("utf-8")
                # Walk back to a UTF-8 boundary and, if possible, a hyphen.
                while cut > 1 and (raw[cut] & 0xC0) == 0x80:
                    cut -= 1
                hyphen = chunk.rfind("-", 0, cut)
                slash = chunk.rfind("/", 0, cut)
                brk = max(hyphen, slash)
                if brk > 8:
                    cut = brk + 1
                words.append(chunk[:cut])
                chunk = chunk[cut:]
            if chunk:
                words.append(chunk)
        out_paras.append(" ".join(words))
    return "\n\n".join(out_paras)


def format_text(
    text: str,
    *,
    fold_unknown: bool = True,
    dehyphenate_lines: bool = True,
    drop_furniture: bool = True,
    strip_markup: bool = True,
    keep_headings: bool = False,
) -> str:
    text = text.replace("\x00", "")
    if strip_markup:
        text = strip_rtf(text)
        text = strip_html(text)
        text = strip_markdown(text)
    if dehyphenate_lines:
        text = dehyphenate(text)
    if drop_furniture:
        text = drop_page_furniture(text)
    text = normalize_chars(text, fold_unknown=fold_unknown)
    if keep_headings:
        # Headings were already flattened by the markdown stripper; nothing
        # further to do. Flag kept so callers can opt into future handling.
        pass
    text = tidy_whitespace(text, keep_single_breaks=False)
    text = enforce_token_limit(text)
    text = MULTI_BLANK_RE.sub("\n\n", text).strip()
    return text + ("\n" if text else "")


def default_output(src: Path) -> Path:
    return src.with_name(src.stem + ".inkspeed.txt")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Format a text file for the inkSpeed RSVP reader."
    )
    ap.add_argument("input", type=Path, help="text-based file (txt, md, html, rtf, ocr dump)")
    ap.add_argument("-o", "--output", type=Path, help="output .txt (default: <name>.inkspeed.txt)")
    ap.add_argument("--keep-unknown", action="store_true",
                    help="drop unsupported characters instead of ASCII-folding them")
    ap.add_argument("--no-dehyphenate", action="store_true",
                    help="do not rejoin words split by a line-end hyphen")
    ap.add_argument("--keep-page-numbers", action="store_true",
                    help="keep lines that look like page numbers or repeated headers")
    ap.add_argument("--no-markup", action="store_true",
                    help="do not strip HTML, Markdown, or RTF")
    ap.add_argument("--keep-headings", action="store_true",
                    help="accepted for compatibility; headings are flattened either way")
    args = ap.parse_args(argv)

    if not args.input.is_file():
        print(f"not a file: {args.input}", file=sys.stderr)
        return 1

    raw = read_bytes(args.input)
    text = decode_bytes(raw)
    out_text = format_text(
        text,
        fold_unknown=not args.keep_unknown,
        dehyphenate_lines=not args.no_dehyphenate,
        drop_furniture=not args.keep_page_numbers,
        strip_markup=not args.no_markup,
        keep_headings=args.keep_headings,
    )

    dest = args.output or default_output(args.input)
    dest.write_bytes(out_text.encode("utf-8"))

    words = len(out_text.split())
    paras = len([p for p in out_text.split("\n\n") if p.strip()])
    print(f"wrote {dest}  ({words} words, {paras} paragraphs, {len(out_text)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
