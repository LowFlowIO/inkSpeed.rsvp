#!/bin/sh
set -e
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
OUT="${1:-/tmp/inkspeed_host_tests}"
cc -std=c11 -Wall -Wextra -Werror -O1 -g \
    -D_FILE_OFFSET_BITS=64 \
    -DSSD1681_FRAME_BYTES=5000 \
    -I"$ROOT/src" \
    -o "$OUT" \
    "$ROOT/tools/host_tests/test_core.c" \
    "$ROOT/tools/host_tests/test_sched.c" \
    "$ROOT/tools/host_tests/test_session.c" \
    "$ROOT/src/utf8.c" \
    "$ROOT/src/font.c" \
    "$ROOT/src/font_data.c" \
    "$ROOT/src/draw.c" \
    "$ROOT/src/sleep_logo.c" \
    "$ROOT/src/drive_counters.c" \
    "$ROOT/src/rsvp_tokenizer.c" \
    "$ROOT/src/rsvp_timing.c" \
    "$ROOT/src/rsvp_fit.c" \
    "$ROOT/src/rsvp_bookmark.c" \
    "$ROOT/src/rsvp_settings.c" \
    "$ROOT/src/rsvp_sched.c" \
    "$ROOT/src/rsvp_session.c" \
    "$ROOT/src/upload_name.c"
PREVIEW="$ROOT/tools/font_preview"
mkdir -p "$PREVIEW"
INKSPEED_DEMO_TXT="$ROOT/tools/host_tests/demo_rsvp_guide.txt" \
INKSPEED_PREVIEW_DIR="$PREVIEW" "$OUT"
if command -v python3 >/dev/null 2>&1; then
    python3 "$ROOT/tools/pbm_to_png.py" "$PREVIEW" || true
fi
