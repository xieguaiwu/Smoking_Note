#!/bin/bash
# Behavior, regression, and stress tests for smoke
#
# Covers:
#   - add via argv and via editor
#   - one note = one record (multi-line notes are NOT split into several notes)
#   - arbitrarily long notes and arbitrarily many notes (no fixed limits)
#   - list / edit round-trip integrity
#   - backward compatibility with the old one-line-per-note file format
#   - no stack overflow, no buffer overflow, no data loss
#
# Usage: ./test_add_editor.sh            (uses ./smoke_test)
#        SMOKE_BIN=/tmp/smoke_asan ./test_add_editor.sh   (sanitizer build)
set -u
cd "$(dirname "$0")"

BIN=${SMOKE_BIN:-./smoke_test}
if [ ! -x "$BIN" ]; then
	echo "FAIL: binary not found: $BIN (build it first)" >&2
	exit 1
fi
BIN=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")

T=$(mktemp -d)
export HOME="$T"
unset VISUAL   # local shell exports VISUAL=nvim; tests must not inherit it
unset EDITOR
mkdir -p "$T/.local/share"
NF="$T/.local/share/notes.txt"
pass=0; fail=0

ck() { if [ "$2" = "$3" ]; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL: $1: expected [$3] got [$2]"; fi; }
ckne() { if [ "$2" != "$3" ]; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL: $1: value must not be [$3]"; fi; }
# Number of note records in the notes file (a record starts with a timestamp).
records() { grep -c '^\[[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9] ' "$NF" 2>/dev/null || echo 0; }
# Raw line count of the notes file.
rawlines() { if [ -f "$NF" ]; then wc -l < "$NF" | tr -d ' '; else echo 0; fi; }
run() { "$BIN" "$@" </dev/null; }

# ---------------------------------------------------------------- 1. argv add
run add hello world >/dev/null
ck "argv add" "$(grep -c 'hello world' "$NF")" "1"
run add "multi word phrase" >/dev/null
ck "argv quoted" "$(grep -c 'multi word phrase' "$NF")" "1"
ck "argv add records" "$(records)" "2"

# ------------------------------------------- 2. editor content -> ONE record
cat > "$T/ed_multi.sh" <<'EOF'
#!/bin/sh
printf '# hint\nline one\nline two\nline three\n' > "$1"
EOF
chmod +x "$T/ed_multi.sh"
before=$(records)
EDITOR="$T/ed_multi.sh" run add > "$T/out_multi"
ck "editor multiline msg" "$(cat "$T/out_multi")" "Note added successfully."
ck "editor multiline = 1 record" "$(records)" "$((before+1))"
ck "editor multiline raw lines" "$(rawlines)" "$((before+3))"
ck "editor line1" "$(grep -c '\] line one$' "$NF")" "1"
ck "editor cont line2" "$(grep -c '^ line two$' "$NF")" "1"
ck "editor cont line3" "$(grep -c '^ line three$' "$NF")" "1"
ck "editor hint ignored" "$(grep -c 'hint' "$NF")" "0"

# ------------------------------------------------- 3. blank / error / empty
before=$(records)
EDITOR=true run add > "$T/out_blank"
ck "blank msg" "$(cat "$T/out_blank")" "Note is empty. Nothing saved."
ck "blank records" "$(records)" "$before"

EDITOR=false run add > "$T/out_nonzero"
ck "nonzero msg" "$(cat "$T/out_nonzero")" "Error: The editor exited with an error. Nothing saved."
ck "nonzero records" "$(records)" "$before"

cat > "$T/ed_ws.sh" <<'EOF'
#!/bin/sh
printf '   \n\t \n\n' > "$1"
EOF
chmod +x "$T/ed_ws.sh"
EDITOR="$T/ed_ws.sh" run add > "$T/out_ws"
ck "whitespace msg" "$(cat "$T/out_ws")" "Note is empty. Nothing saved."
ck "whitespace records" "$(records)" "$before"

run add "" > "$T/out_emptyargv"
ck "empty argv msg" "$(cat "$T/out_emptyargv")" "Note is empty. Nothing saved."
ck "empty argv records" "$(records)" "$before"

# ------------------------------------------------------ 4. VISUAL / EDITOR
cat > "$T/ed_visual.sh" <<'EOF'
#!/bin/sh
echo visual wins > "$1"
EOF
cat > "$T/ed_editor.sh" <<'EOF'
#!/bin/sh
echo editor loses > "$1"
EOF
chmod +x "$T/ed_visual.sh" "$T/ed_editor.sh"
VISUAL="$T/ed_visual.sh" EDITOR="$T/ed_editor.sh" run add >/dev/null
ck "VISUAL wins" "$(grep -c 'visual wins' "$NF")" "1"
VISUAL= EDITOR="$T/ed_editor.sh" run add >/dev/null
ck "EDITOR fallback" "$(grep -c 'editor loses' "$NF")" "1"

# ------------------------------------- 5. paragraphs survive as one record
cat > "$T/ed_para.sh" <<'EOF'
#!/bin/sh
printf '\n\npara one line a\npara one line b\n\n\npara two after two blanks\n\n\n' > "$1"
EOF
chmod +x "$T/ed_para.sh"
before=$(records)
EDITOR="$T/ed_para.sh" run add >/dev/null
ck "paragraph = 1 record" "$(records)" "$((before+1))"
ck "paragraph leading blanks trimmed" "$(grep -c '^\[.*\] para one line a$' "$NF")" "1"
ck "paragraph blank kept" "$(grep -c '^$' "$NF")" "2"
ck "paragraph no space-only lines" "$(grep -c '^ $' "$NF")" "0"

# ------------------------------------------------------- 6. long notes (bug 2)
for n in 1024 1025 4096 65536; do
	before=$(records)
	beforeA=$(tr -cd 'A' < "$NF" 2>/dev/null | wc -c | tr -d ' ')
	long=$(python3 -c "print('A'*$n)")
	run add "$long" > "$T/out_long$n"
	ck "long $n msg" "$(cat "$T/out_long$n")" "Note added successfully."
	ck "long $n records" "$(records)" "$((before+1))"
	ck "long $n bytes stored" "$(tr -cd 'A' < "$NF" | wc -c | tr -d ' ')" "$((beforeA+n))"
	ck "long $n single line" "$(awk -v n=$n '{t=$0; if (gsub(/A/,"",t)==n) c++} END{print c+0}' "$NF")" "1"
done

# long note via editor (single line of 200000 bytes)
python3 - "$T/ed_huge.sh" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'w') as f:
    f.write('#!/bin/sh\n')
    f.write('python3 -c "import sys;open(sys.argv[1],\'w\').write(\'Z\'*200000)" "$1"\n')
PY
chmod +x "$T/ed_huge.sh"
before=$(records)
EDITOR="$T/ed_huge.sh" run add > "$T/out_huge"
ck "huge editor msg" "$(cat "$T/out_huge")" "Note added successfully."
ck "huge editor records" "$(records)" "$((before+1))"
ck "huge editor bytes" "$(tr -cd 'Z' < "$NF" | wc -c | tr -d ' ')" "200000"

# ------------------------------------- 7. UTF-8 / CJK content stays intact
run add "中文笔记：测试多字节字符不会被截断" >/dev/null
ck "utf8 stored" "$(grep -c '中文笔记：测试多字节字符不会被截断' "$NF")" "1"
python3 - "$T/ed_cjk.sh" <<'PY'
import sys
with open(sys.argv[1], 'w') as f:
    f.write('#!/bin/sh\n')
    f.write('printf "%s\\n" "第一段中文" "第二段中文继续" > "$1"\n')
PY
chmod +x "$T/ed_cjk.sh"
before=$(records)
EDITOR="$T/ed_cjk.sh" run add >/dev/null
ck "utf8 multiline = 1 record" "$(records)" "$((before+1))"
ck "utf8 cont line" "$(grep -c '^ 第二段中文继续$' "$NF")" "1"

# -------------------------- 8. list shows every record, no truncation
run add "short" >/dev/null
long40=$(python3 -c "print('Q'*4000)")
run add "$long40" >/dev/null
listing=$("$BIN" list </dev/null)
ck "list header" "$(printf '%s\n' "$listing" | head -1)" "=== Notes ==="
ck "list 4000 chars present" "$(printf '%s\n' "$listing" | grep -c "Q\{4000\}")" "1"
ck "list entry count" "$(printf '%s\n' "$listing" | grep -c '^[0-9]\+\. ')" "$(records)"

# ------------------------------- 9. edit: many records, no stack overflow
python3 - "$NF" <<'PY'
import sys
with open(sys.argv[1], 'w') as f:
    for i in range(1, 5001):
        f.write('[2026-01-01 00:00:00] note %d\n' % i)
PY
ck "5000 records loaded" "$(records)" "5000"
timeout 30 "$BIN" edit 5000 EDITED_LAST </dev/null > "$T/out_edit5000"
ck "edit 5000 msg" "$(cat "$T/out_edit5000")" "Note updated successfully."
ck "edit 5000 records kept" "$(records)" "5000"
ck "edit 5000 hit" "$(grep -c 'EDITED_LAST' "$NF")" "1"
ck "edit 5000 first intact" "$(grep -c '\] note 1$' "$NF")" "1"
ck "edit 5000 middle intact" "$(grep -c '\] note 4999$' "$NF")" "1"

# edit out of range must not write anything
cp "$NF" "$T/snapshot"
timeout 10 "$BIN" edit 5001 nope </dev/null > "$T/out_range"
ck "edit out of range msg" "$(cat "$T/out_range")" "Error: Line 5001 does not exist (total 5000)"
ck "edit out of range file unchanged" "$(cmp -s "$NF" "$T/snapshot" && echo same)" "same"

# invalid line numbers
for bad in 0 -1 abc 1x "" 99999999999999999999; do
	timeout 10 "$BIN" edit "$bad" x </dev/null > "$T/out_bad"
	ckne "edit invalid [$bad] rejected" "$(head -1 "$T/out_bad")" "Note updated successfully."
	ck "edit invalid [$bad] file unchanged" "$(cmp -s "$NF" "$T/snapshot" && echo same)" "same"
done

# -------------------------- 10. edit round-trip keeps multi-line records
rm -f "$NF"
EDITOR="$T/ed_multi.sh" run add >/dev/null          # record 1: 3 lines
run add "second" >/dev/null                          # record 2
EDITOR="$T/ed_multi.sh" run add >/dev/null           # record 3: 3 lines
ck "roundtrip records" "$(records)" "3"
timeout 10 "$BIN" edit 2 CHANGED </dev/null >/dev/null
ck "roundtrip after edit records" "$(records)" "3"
ck "roundtrip rec1 line1" "$(grep -c '\] line one$' "$NF")" "2"
ck "roundtrip rec1 cont" "$(grep -c '^ line two$' "$NF")" "2"
ck "roundtrip rec2 changed" "$(grep -c '\] CHANGED$' "$NF")" "1"
ck "roundtrip list count" "$("$BIN" list </dev/null | grep -c '^[0-9]\+\. ')" "3"

# ---------------- 10b. multi-line edit via editor keeps one timestamp
rm -f "$NF"
EDITOR="$T/ed_multi.sh" run add >/dev/null
ck "ml-edit pre records" "$(records)" "1"
cat > "$T/ed_noop.sh" <<'EOF'
#!/bin/sh
: > /dev/null
EOF
chmod +x "$T/ed_noop.sh"
EDITOR="$T/ed_noop.sh" timeout 10 "$BIN" edit 1 </dev/null > "$T/out_mlcat"
ck "ml-edit msg" "$(tail -1 "$T/out_mlcat")" "Note updated successfully."
ck "ml-edit records" "$(records)" "1"
ck "ml-edit single timestamp" "$(grep -c '^\[[^]]*\] \[[^]]*\]' "$NF")" "0"
ck "ml-edit content kept" "$(grep -c '\] line one$' "$NF")" "1"

cat > "$T/ed_rewrite.sh" <<'EOF'
#!/bin/sh
printf 'NEW first line\nNEW second line\n' > "$1"
EOF
chmod +x "$T/ed_rewrite.sh"
EDITOR="$T/ed_rewrite.sh" timeout 10 "$BIN" edit 1 </dev/null >/dev/null
ck "ml-rewrite records" "$(records)" "1"
ck "ml-rewrite first" "$(grep -c '\] NEW first line$' "$NF")" "1"
ck "ml-rewrite cont" "$(grep -c '^ NEW second line$' "$NF")" "1"
ck "ml-rewrite no stale" "$(grep -c 'line three' "$NF")" "0"

# ---------------- 10c. interactive prompt path for a single-line note
rm -f "$NF"
printf '[2026-03-10 18:03:59] single line note\n' > "$NF"
printf 'typed via stdin\n' | timeout 10 "$BIN" edit 1 > "$T/out_prompt"
ck "prompt msg" "$(tail -1 "$T/out_prompt")" "Note updated successfully."
ck "prompt applied" "$(grep -c '\] typed via stdin$' "$NF")" "1"
ck "prompt records" "$(records)" "1"
# empty input keeps the note unchanged
printf '\n' | timeout 10 "$BIN" edit 1 > "$T/out_prompt_empty"
ck "prompt empty msg" "$(tail -1 "$T/out_prompt_empty")" "Note is empty. Nothing saved."
ck "prompt empty kept" "$(grep -c 'typed via stdin' "$NF")" "1"
# EOF keeps the note unchanged
timeout 10 "$BIN" edit 1 < /dev/null > "$T/out_prompt_eof"
ck "prompt eof kept" "$(grep -c 'typed via stdin' "$NF")" "1"

# ------------------------------- 11. old format compatibility (no markers)
printf '[2026-03-10 18:03:59] old one\n[2026-03-11 00:39:35] old two\n' > "$NF"
ck "legacy records" "$(records)" "2"
ck "legacy list count" "$("$BIN" list </dev/null | grep -c '^[0-9]\+\. ')" "2"
timeout 10 "$BIN" edit 2 new-two </dev/null >/dev/null
ck "legacy after edit records" "$(records)" "2"
ck "legacy edit applied" "$(grep -c '\] new-two$' "$NF")" "1"
ck "legacy first intact" "$(grep -c '\] old one$' "$NF")" "1"

# ------------------- 12. file without trailing newline is not corrupted
printf '[2026-03-10 18:03:59] no newline at eof' > "$NF"
ck "no-eol records" "$(records)" "1"
run add "appended" >/dev/null
ck "no-eol append records" "$(records)" "2"
ck "no-eol both present" "$(grep -c 'no newline at eof' "$NF")" "1"

# ------------------------------- 13. CRLF files (written on Windows) parse
printf '[2026-03-10 18:03:59] crlf one\r\n[2026-03-11 00:39:35] crlf two\r\n' > "$NF"
ck "crlf records" "$(records)" "2"
ck "crlf list count" "$("$BIN" list </dev/null | grep -c '^[0-9]\+\. ')" "2"
ck "crlf no stray CR" "$(grep -c $'\r' "$NF" | tr -d ' ')" "2"
timeout 10 "$BIN" edit 1 edited-crlf </dev/null >/dev/null
ck "crlf edit rewrites clean" "$(grep -c $'\r' "$NF" | tr -d ' ')" "0"

# ------------------------- 14. empty file / missing file behaviour is sane
: > "$NF"
ck "empty file list" "$("$BIN" list </dev/null | head -1)" "No notes found."
timeout 10 "$BIN" edit 1 x </dev/null > "$T/out_emptiedit"
ck "empty file edit msg" "$(head -1 "$T/out_emptiedit")" "No notes found."
rm -f "$NF"
ck "missing file list" "$("$BIN" list </dev/null | head -1)" "No notes found."
run add "recreated" >/dev/null
ck "missing file recreated" "$(records)" "1"

# ------------------------------------- 15. usage text and temp file hygiene
"$BIN" 2>&1 | grep -q 'opens editor'; ck "usage mentions editor" "$?" "0"
"$BIN" 2>&1 | grep -q 'add new note'; ck "usage mentions add" "$?" "0"
tmp_before=$(ls /tmp/smoke-note-* 2>/dev/null | wc -l | tr -d ' ')
EDITOR=true run add >/dev/null
EDITOR=false run add >/dev/null
VISUAL= EDITOR=true run add >/dev/null
ck "temp files cleaned" "$(ls /tmp/smoke-note-* 2>/dev/null | wc -l | tr -d ' ')" "$tmp_before"

# ------------------------------------- 16. stress: 20000 records round-trip
python3 - "$NF" <<'PY'
import sys
with open(sys.argv[1], 'w') as f:
    for i in range(1, 20001):
        f.write('[2026-01-01 00:00:00] bulk %d\n' % i)
        if i % 1000 == 0:
            f.write(' continuation for %d\n' % i)
PY
ck "stress records" "$(records)" "20000"
timeout 60 "$BIN" list </dev/null > "$T/out_listbig"
ck "stress list count" "$(grep -c '^[0-9]\+\. ' "$T/out_listbig")" "20000"
ck "stress list continuation shown" "$(grep -c '^continuation for 1000$' "$T/out_listbig")" "1"
timeout 60 "$BIN" edit 1000 stress-edited </dev/null >/dev/null
ck "stress edit records" "$(records)" "20000"
ck "stress edit applied" "$(grep -c '\] stress-edited$' "$NF")" "1"
ck "stress neighbor 999" "$(grep -c '\] bulk 999$' "$NF")" "1"
ck "stress neighbor 1001" "$(grep -c '\] bulk 1001$' "$NF")" "1"
ck "stress continuation dropped with old record" "$(grep -c '^ continuation for 1000$' "$NF")" "0"

# ------------------------------------- 17. fuzz: random content never breaks
fuzz_out=$(python3 - "$T" "$NF" "$BIN" <<'PY'
import os, random, subprocess, sys
tmp, nf, binary = sys.argv[1], sys.argv[2], sys.argv[3]
env = dict(os.environ)
alphabet = 'abcXYZ 0123\t\n中文éüß\\"\'$`*?#[](){}<>|;&~!@%^&-_=+'
random.seed(20261105)
ok = True
for trial in range(120):
    n = random.choice([0, 1, 5, 50, 500, 5000])
    text = ''.join(random.choice(alphabet) for _ in range(n))
    p = subprocess.run([binary, 'add', text], stdin=subprocess.DEVNULL,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    if p.returncode != 0:
        print('FUZZ_FAIL rc=%d trial=%d text=%r' % (p.returncode, trial, text[:80]))
        ok = False
        break
p = subprocess.run([binary, 'list'], stdin=subprocess.DEVNULL,
                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
if p.returncode != 0:
    print('FUZZ_FAIL list rc=%d' % p.returncode)
    ok = False
print('FUZZ_OK' if ok else 'FUZZ_BAD')
PY
)
ck "fuzz 120 random notes no crash" "$(printf '%s' "$fuzz_out" | tail -1)" "FUZZ_OK"

# ---------------- 18. exit codes are meaningful for scripts
rm -f "$NF"
run add "code test" >/dev/null;              ck "exit add ok" "$?" "0"
"$BIN" list </dev/null >/dev/null;           ck "exit list ok" "$?" "0"
"$BIN" edit 1 "code2" </dev/null >/dev/null; ck "exit edit ok" "$?" "0"
"$BIN" </dev/null >/dev/null;                ck "exit usage" "$?" "0"
"$BIN" bogus </dev/null >/dev/null;          ck "exit unknown cmd" "$?" "2"
run add "" >/dev/null;                       ck "exit empty add" "$?" "0"
"$BIN" edit 0 x </dev/null >/dev/null;       ck "exit bad line" "$?" "1"
"$BIN" edit 99999 x </dev/null >/dev/null;   ck "exit out of range" "$?" "1"
EDITOR=false "$BIN" add </dev/null >/dev/null; ck "exit editor fail" "$?" "1"

# ---------------- 19. a failing edit must never damage the notes file
python3 - "$NF" <<'PYEOF'
import sys
with open(sys.argv[1], 'w') as f:
    for i in range(1, 51):
        f.write('[2026-01-01 00:00:00] n%d\n' % i)
PYEOF
cp "$NF" "$T/snapshot_fail"
chmod 444 "$NF"
chmod 555 "$(dirname "$NF")"
"$BIN" edit 5 CHANGED </dev/null > "$T/out_ro" 2>&1
ck "ro edit exit" "$?" "1"
ck "ro edit file intact" "$(cmp -s "$NF" "$T/snapshot_fail" && echo same)" "same"
ck "ro edit no tmp left" "$(ls "$(dirname "$NF")"/*.tmp 2>/dev/null | wc -l | tr -d ' ')" "0"
chmod 755 "$(dirname "$NF")"
chmod 644 "$NF"

# ---------------- 20. successful edit leaves no temporary file behind
"$BIN" edit 5 OK </dev/null >/dev/null
ck "edit no tmp left" "$(ls "$(dirname "$NF")"/*.tmp 2>/dev/null | wc -l | tr -d ' ')" "0"
ck "edit applied after ro test" "$(grep -c '\] OK$' "$NF")" "1"
ck "edit record count stable" "$(records)" "50"

# ---------------- 21. concurrent writers never corrupt the file
rm -f "$NF"
for i in $(seq 1 40); do run add "concurrent-$i" >/dev/null & done
wait
ck "concurrent records" "$(records)" "40"
ck "concurrent unique" "$(grep -o 'concurrent-[0-9]*' "$NF" | sort -u | wc -l | tr -d ' ')" "40"
ck "concurrent no malformed" "$(grep -vc '^\[[0-9-]* [0-9:]*\] concurrent-[0-9]*$' "$NF")" "0"

echo "PASS=$pass FAIL=$fail"
rm -rf "$T"
[ "$fail" -eq 0 ]
