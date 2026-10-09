#!/bin/bash
# Behavior tests for `smoke add` editor feature + regression
set -u
cd "$(dirname "$0")"
T=$(mktemp -d)
export HOME="$T"
unset VISUAL   # local shell exports VISUAL=nvim; tests must not inherit it
mkdir -p "$T/.local"
NF="$T/.local/share/notes.txt"
S=./smoke_test
pass=0; fail=0
ck() { if [ "$2" = "$3" ]; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL: $1: expected [$3] got [$2]"; fi; }

# 1. argv add regression
$S add hello world >/dev/null; ck "argv add" "$(grep -c 'hello world' $NF)" "1"
$S add "multi word phrase" >/dev/null; ck "argv quoted" "$(grep -c 'multi word phrase' $NF)" "1"

# 2. editor writes content
cat > "$T/ed1.sh" <<'EOF'
#!/bin/sh
echo editor note line > "$1"
EOF
chmod +x "$T/ed1.sh"
EDITOR="$T/ed1.sh" $S add >/dev/null
ck "editor content" "$(grep -c 'editor note line' $NF)" "1"

# 3. editor leaves hint only (blank) -> nothing saved
before=$(wc -l < "$NF")
EDITOR=true $S add > "$T/out3"
ck "blank msg" "$(cat "$T/out3")" "Note is empty. Nothing saved."
ck "blank nolines" "$(wc -l < "$NF")" "$before"

# 4. editor exits nonzero -> nothing saved
before=$(wc -l < "$NF")
EDITOR=false $S add > "$T/out4"
ck "nonzero msg" "$(cat "$T/out4")" "Error: The editor exited with an error. Nothing saved."
ck "nonzero nolines" "$(wc -l < "$NF")" "$before"

# 5. whitespace-only content -> nothing saved
cat > "$T/ed5.sh" <<'EOF'
#!/bin/sh
printf '   \n\t \n' > "$1"
EOF
chmod +x "$T/ed5.sh"
before=$(wc -l < "$NF")
EDITOR="$T/ed5.sh" $S add > "$T/out5"
ck "ws msg" "$(cat "$T/out5")" "Note is empty. Nothing saved."
ck "ws nolines" "$(wc -l < "$NF")" "$before"

# 6. multi-line -> multiple notes, hint ignored, trimmed
cat > "$T/ed6.sh" <<'EOF'
#!/bin/sh
printf '# hint line\nline A\n\n  line B  \n' > "$1"
EOF
chmod +x "$T/ed6.sh"
before=$(wc -l < "$NF")
EDITOR="$T/ed6.sh" $S add > "$T/out6"
ck "multiline msg" "$(cat "$T/out6")" "Notes added successfully: 2."
ck "multiline count" "$(wc -l < "$NF")" "$((before+2))"
ck "multiline A" "$(grep -c 'line A' "$NF")" "1"
ck "multiline B trimmed" "$(grep -c '] line B$' "$NF")" "1"
ck "hint ignored" "$(grep -c 'hint line' "$NF")" "0"

# 7. VISUAL precedence over EDITOR
cat > "$T/edv.sh" <<'EOF'
#!/bin/sh
echo visual wins > "$1"
EOF
cat > "$T/ede.sh" <<'EOF'
#!/bin/sh
echo editor loses > "$1"
EOF
chmod +x "$T/edv.sh" "$T/ede.sh"
VISUAL="$T/edv.sh" EDITOR="$T/ede.sh" $S add >/dev/null
ck "VISUAL wins" "$(grep -c 'visual wins' "$NF")" "1"

# 8. EDITOR fallback when VISUAL unset/empty
VISUAL= EDITOR="$T/ede.sh" $S add >/dev/null
ck "EDITOR used" "$(grep -c 'editor loses' "$NF")" "1"

# 9. list works
$S list | head -1 | grep -q '=== Notes ==='; ck "list" "$?" "0"

# 10. edit regression (content arg path)
$S edit 1 edited-content >/dev/null
ck "edit" "$(sed -n '1p' "$NF" | grep -c 'edited-content')" "1"

# 11. usage mentions editor
$S 2>&1 | grep -q 'opens editor'; ck "usage" "$?" "0"

# 12. temp files cleaned up
ck "tmp cleanup" "$(ls /tmp/smoke-note-* 2>/dev/null | wc -l)" "0"

# 13. blank argv content -> not recorded
before=$(wc -l < "$NF")
$S add "" > "$T/out13"
ck "empty argv msg" "$(cat "$T/out13")" "Note is empty. Nothing saved."
ck "empty argv nolines" "$(wc -l < "$NF")" "$before"

echo "PASS=$pass FAIL=$fail"
rm -rf "$T"
[ "$fail" -eq 0 ]
