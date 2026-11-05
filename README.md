# Smoke - Command-line Note-taking Application

[**中文版**](README_zh.md) | [**English**](#)

A simple and lightweight command-line note-taking application written in C. Smoke lets you add, list, and edit notes with automatic timestamps. There is no limit on note length, note count, or the number of lines in a note.

## Features

- **Add Notes** - Create a new note with an automatic timestamp
- **Editor Mode** - Run `smoke add` with no content to open your default editor. The whole text becomes one note, paragraphs included
- **List Notes** - View all saved notes with line numbers. Long notes and multi-line notes are printed in full
- **Edit Notes** - Modify a note by line number. Multi-line notes open in the editor
- **No Length Limits** - Notes, notes files, and lines grow as needed
- **Safe Writes** - An edit is written to a temporary file and then renamed, so a crash or a full disk never damages your notes
- **UTF-8 Safe** - Multi-byte characters are never cut in half
- **Cross-platform** - Works on both Linux and Windows
- **Persistent Storage** - Notes are saved to a local text file

## Installation

### Fedora / RHEL / CentOS (COPR)

```bash
sudo dnf copr enable xieguaiwu/smoke
sudo dnf install smoke
```

### Compile from Source

```bash
gcc smoke.c -o smoke
```

### Install to System (Optional)

```bash
sudo cp smoke /usr/local/bin/
```

## Usage

```
smoke <command> [options]
```

### Commands

| Command | Description |
|---------|-------------|
| `add [content]` | Add a new note. With no content, open the default editor |
| `list` | Display all saved notes |
| `edit <line_number> [new_content]` | Edit a note by line number |

### Examples

**Add a new note:**
```bash
smoke add Buy groceries tomorrow
```

**Add a note with the editor (no content argument):**
```bash
smoke add
```
This opens `$VISUAL`, then `$EDITOR`, then `vi` (`notepad` on Windows).

- The temp file starts with `#` hint lines. Lines that start with `#` are ignored.
- **The whole text becomes one note**, however many lines and paragraphs you write.
- Leading and trailing blank lines are dropped. Inner blank lines are kept.
- Blank content or a non-zero editor exit saves nothing.

**List all notes:**
```bash
smoke list
```

Output:
```
=== Notes ===
1. [2026-03-10 14:30:00] Buy groceries tomorrow
2. [2026-03-11 09:12:44] Long note, first line
second line
third line
```

**Edit a note:**
```bash
smoke edit 1 Buy groceries and cook dinner
```

Or edit interactively (without providing new content):
```bash
smoke edit 1
```
A single-line note is edited on the prompt. A multi-line note opens in the editor, seeded with its current text.

## Storage Location

Notes are stored in a plain text file with timestamps:

- **Linux**: `~/.local/share/notes.txt`
- **Windows**: `notes.txt` (current directory)

The application creates the directory, including missing parents, when it does not exist.

## Note Format

One note is one record. A record starts with a timestamp in brackets. Every further line of the same note is stored with one leading space:

```
[2026-03-10 14:30:00] Buy groceries tomorrow
[2026-03-11 09:12:44] Long note, first line
 second line
 third line
```

This format is backward compatible: a file written by an older version, with one line per note, is read correctly.

## Checking an Existing Notes File

Older versions had two defects: a note written in the editor was split into one note per line, and a note longer than 1023 bytes was split into several notes. `check_notes.sh` reports traces of both. It only reads.

```bash
./check_notes.sh ~/.local/share/notes.txt
```

## Exit Codes

| Code | Meaning |
|:---:|:---|
| `0` | Success, or nothing to save (empty note) |
| `1` | Write failure, editor failure, invalid line number, line out of range |
| `2` | Usage error (missing argument, unknown command) |

## Building Requirements

- C compiler (gcc, clang, etc.)
- Standard C library

## Tests

```bash
gcc -O2 -Wall -Wextra -std=c99 smoke.c -o smoke_test
./test_add_editor.sh
```

To run the same suite under the address and undefined-behaviour sanitizers:

```bash
clang -g -O1 -fsanitize=address,undefined -std=c99 smoke.c -o /tmp/smoke_asan
SMOKE_BIN=/tmp/smoke_asan ./test_add_editor.sh
```

## License

This project is open source and available for personal and educational use.

## Contributing

Feel free to submit issues and pull requests for bug fixes and improvements.
