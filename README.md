# Smoke - Command-line Note-taking Application

A simple and lightweight command-line note-taking application written in C. Smoke allows you to quickly add, list, and edit notes with automatic timestamps.

## Features

- **Add Notes** - Create new notes with automatic timestamps
- **List Notes** - View all saved notes with line numbers
- **Edit Notes** - Modify existing notes by line number
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
| `add [content]` | Add a new note with the specified content |
| `list` | Display all saved notes |
| `edit <line_number> [new_content]` | Edit an existing note by line number |

### Examples

**Add a new note:**
```bash
smoke add Buy groceries tomorrow
```

**List all notes:**
```bash
smoke list
```

Output:
```
=== Notes ===
1. [2026-03-10 14:30:00] Buy groceries tomorrow
```

**Edit a note:**
```bash
smoke edit 1 Buy groceries and cook dinner
```

Or edit interactively (without providing new content):
```bash
smoke edit 1
```

## Storage Location

Notes are stored in a plain text file with timestamps:

- **Linux**: `~/.local/share/notes.txt`
- **Windows**: `notes.txt` (current directory)

The application automatically creates the necessary directories on Linux if they don't exist.

## Note Format

Each note is stored in the following format:
```
[YYYY-MM-DD HH:MM:SS] Note content here
```

## Building Requirements

- C compiler (gcc, clang, etc.)
- Standard C library

## License

This project is open source and available for personal and educational use.

## Contributing

Feel free to submit issues and pull requests for bug fixes and improvements.
