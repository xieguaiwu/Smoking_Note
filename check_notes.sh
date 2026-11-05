#!/bin/bash
# check_notes.sh - Diagnose a notes file for problems left by older versions
#
# Older versions of smoke had two defects that left traces in the notes file:
#   1. A note written in the editor was split into one note per line.
#   2. A note longer than 1023 bytes was split into several notes, and the
#      split could land in the middle of a UTF-8 character.
#
# This script only reads. It never modifies the notes file.
#
# Usage:
#   ./check_notes.sh [notes-file]
#
# Default notes file: $HOME/.local/share/notes.txt

set -u

NF=${1:-"$HOME/.local/share/notes.txt"}

if [ ! -f "$NF" ]; then
	echo "No notes file: $NF"
	exit 1
fi

echo "Notes file: $NF"
echo "Size: $(wc -c < "$NF") bytes, $(wc -l < "$NF") lines"
echo

echo "=== Records ==="
echo "Records (timestamp lines): $(grep -c '^\[[0-9][0-9][0-9][0-9]-' "$NF")"
echo

echo "=== UTF-8 validity ==="
if python3 -c "import sys; open(sys.argv[1], 'rb').read().decode('utf-8')" "$NF" 2>/dev/null; then
	echo "OK: the file is valid UTF-8."
else
	echo "PROBLEM: the file is not valid UTF-8."
	echo "         An older version cut a multi-byte character in half."
	echo "         Run with --show-bad to see the byte offsets."
fi
echo

echo "=== Possible editor splits (identical consecutive timestamps) ==="
awk '
/^\[[0-9][0-9][0-9][0-9]-/ {
	ts = substr($0, 1, 21)
	if (ts == prev) { count[ts]++ ; if (count[ts] == 1) group++ }
	prev = ts
}
END {
	if (group == 0) print "None found."
	else {
		printf "%d group(s) of notes share an identical timestamp.\n", group
		print "An older version wrote one note per editor line, so these"
		print "groups are usually ONE note that was split. Review them with:"
		print "  smoke list | less"
	}
}' "$NF"
echo

echo "=== Possible length splits (records at exactly the old 1023-byte limit) ==="
awk '
/^\[[0-9][0-9][0-9][0-9]-/ { if (length($0) >= 1000 && length($0) <= 1023) c++ }
END {
	if (c == 0) print "None found."
	else {
		printf "%d record(s) are 1000-1023 bytes long.\n", c
		print "An older version split notes at 1023 bytes, so a record in this"
		print "range may be the first fragment of a longer note."
	}
}' "$NF"
echo

echo "=== Suspicious lines (no timestamp, no continuation marker) ==="
orphans=$(grep -c '^[^[ ]' "$NF" 2>/dev/null || true)
if [ "${orphans:-0}" -eq 0 ]; then
	echo "None found."
else
	echo "PROBLEM: $orphans line(s) are neither a record nor a continuation."
	echo "         The reader keeps them as part of the previous record, so no"
	echo "         data is lost, but the file layout is not clean."
fi
echo

echo "=== Summary ==="
echo "This report is read-only. Back up the file before any manual edit:"
echo "  cp \"$NF\" \"$NF.bak\""
