/*
 * smoke.c - A simple command-line note-taking application
 *
 * This program allows users to add, list, and edit notes.
 * Notes are stored in a local text file with timestamps.
 *
 * Platform-specific storage:
 * - Linux: ~/.local/share/notes.txt
 * - Windows: notes.txt in current directory
 *
 * Storage format (one record = one note):
 *
 *   [YYYY-MM-DD HH:MM:SS] first line of the note
 *    continuation line
 *    another continuation line
 *
 * The first line of a record always starts with a timestamp in brackets.
 * Every continuation line is stored with exactly one leading space, so a
 * note may contain paragraphs and still stay one single record.  The format
 * is backward compatible with the old one-line-per-note files: a record
 * written by an older version simply has no continuation lines.
 *
 * All content buffers grow dynamically: there is no fixed limit on the
 * length of a note, the number of notes, or the number of lines in the
 * notes file.
 */

/* Feature test macros must be set before the first #include.
 * mkstemp() and friends need POSIX 2008 when compiled with -std=c99. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
    #define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

/* Platform detection */
#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #include <io.h>
    #define PATH_SEPARATOR "\\"
    #define mkdir(path, mode) _mkdir(path)
#else
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <unistd.h>
    #include <errno.h>
    #include <termios.h>
    #include <fcntl.h>
    #define PATH_SEPARATOR "/"
#endif

/* Constants */
#define MAX_PATH 512              /* Maximum path length */
#define INITIAL_CAPACITY 64       /* Initial capacity of a dynamic buffer */

/* Global variables */
char command[256];                /* Store the command from user input */
char notes_file[MAX_PATH];        /* Full path to the notes file */

/* Dynamic string buffer */
typedef struct {
	char *data;               /* NUL terminated, may be NULL when empty */
	size_t len;
	size_t cap;
} Buffer;

/* List of note records */
typedef struct {
	Buffer *items;
	size_t count;
	size_t cap;
} RecordList;

/* Function declarations */
void init_notes_path(void);
void ensure_directory_exists(const char *path);
int handle_args(int argc, char * argv[]);
void print_usage(void);
void get_current_time(char *buffer, int size);
int add_note(int argc, char * argv[]);
int list_notes(void);
int edit_note(int argc, char * argv[]);
int write_note(const char *content);
int add_note_via_editor(void);
void trim_whitespace(char *s);
int read_line_interactive(Buffer *out, const char *prompt);

/*
 * buf_init / buf_free / buf_reset - Lifecycle helpers for a Buffer
 */
static void buf_init(Buffer *b) {
	b->data = NULL;
	b->len = 0;
	b->cap = 0;
}

static void buf_free(Buffer *b) {
	free(b->data);
	b->data = NULL;
	b->len = 0;
	b->cap = 0;
}

static void buf_reset(Buffer *b) {
	b->len = 0;
	if (b->data != NULL) b->data[0] = '\0';
}

/*
 * buf_reserve - Make room for 'extra' more bytes plus the NUL terminator
 *
 * Return: 1 on success, 0 on allocation failure
 */
static int buf_reserve(Buffer *b, size_t extra) {
	if (b->len + extra + 1 <= b->cap) return 1;

	size_t cap = (b->cap > 0) ? b->cap : INITIAL_CAPACITY;
	while (cap < b->len + extra + 1) {
		if (cap > (size_t)-1 / 2) return 0;
		cap *= 2;
	}

	char *grown = (char *)realloc(b->data, cap);
	if (grown == NULL) return 0;
	b->data = grown;
	b->cap = cap;
	return 1;
}

static int buf_append_n(Buffer *b, const char *s, size_t n) {
	if (n == 0) {
		if (!buf_reserve(b, 0)) return 0;
		b->data[b->len] = '\0';
		return 1;
	}
	if (!buf_reserve(b, n)) return 0;
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = '\0';
	return 1;
}

static int buf_append_str(Buffer *b, const char *s) {
	return buf_append_n(b, s, strlen(s));
}

static int buf_append_ch(Buffer *b, char c) {
	return buf_append_n(b, &c, 1);
}

/*
 * read_line_dyn - Read one line of arbitrary length from a stream
 *
 * The trailing "\n" and "\r" are removed.  The returned line is allocated
 * with malloc() and must be freed by the caller.
 *
 * Return: 1 = line read, 0 = end of file, -1 = allocation failure
 */
static int read_line_dyn(FILE *f, char **line) {
	Buffer b;
	int c;
	int got_any = 0;

	buf_init(&b);
	while ((c = fgetc(f)) != EOF) {
		got_any = 1;
		if (c == '\n') break;
		if (!buf_append_ch(&b, (char)c)) {
			buf_free(&b);
			return -1;
		}
	}

	if (!got_any) {
		buf_free(&b);
		return 0;
	}

	while (b.len > 0 && b.data[b.len - 1] == '\r') {
		b.data[--b.len] = '\0';
	}

	if (b.data == NULL) {
		if (!buf_reserve(&b, 0)) {
			buf_free(&b);
			return -1;
		}
		b.data[0] = '\0';
	}

	*line = b.data;
	return 1;
}

/*
 * records_init / records_free / records_push - Lifecycle for a RecordList
 */
static void records_init(RecordList *r) {
	r->items = NULL;
	r->count = 0;
	r->cap = 0;
}

static void records_free(RecordList *r) {
	if (r->items != NULL) {
		for (size_t i = 0; i < r->count; i++) buf_free(&r->items[i]);
		free(r->items);
	}
	records_init(r);
}

static int records_push(RecordList *r) {
	if (r->count == r->cap) {
		size_t cap = (r->cap > 0) ? r->cap * 2 : 32;
		if (cap > (size_t)-1 / sizeof(Buffer)) return 0;
		Buffer *grown = (Buffer *)realloc(r->items, cap * sizeof(Buffer));
		if (grown == NULL) return 0;
		r->items = grown;
		r->cap = cap;
	}
	buf_init(&r->items[r->count]);
	r->count++;
	return 1;
}

/*
 * is_record_header - Does this line start a new note record?
 *
 * A record header looks like "[YYYY-MM-DD HH:MM:SS]".
 */
static int is_record_header(const char *line) {
	if (line == NULL || line[0] != '[') return 0;
	for (int i = 1; i <= 19; i++) {
		char c = line[i];
		if (c == '\0') return 0;
		if (i == 5 || i == 8) {
			if (c != '-') return 0;
		} else if (i == 11) {
			if (c != ' ') return 0;
		} else if (i == 14 || i == 17) {
			if (c != ':') return 0;
		} else if (!isdigit((unsigned char)c)) {
			return 0;
		}
	}
	return line[20] == ']';
}

/*
 * load_records - Read the whole notes file into memory
 *
 * Each record is one note.  A line that does not start a record is a
 * continuation line of the previous record; one leading space is removed
 * from it (the storage marker).
 *
 * Return: 1 on success (an empty list is a success), 0 when the file cannot
 *         be opened or memory runs out
 */
static int load_records(RecordList *out) {
	FILE *file;
	char *line = NULL;
	int rc;

	records_init(out);

	file = fopen(notes_file, "r");
	if (file == NULL) return 0;

	while ((rc = read_line_dyn(file, &line)) == 1) {
		int ok = 1;

		if (is_record_header(line)) {
			ok = records_push(out) && buf_append_str(&out->items[out->count - 1], line);
		} else if (out->count == 0) {
			/* Orphan line: keep it so no data is ever lost. */
			ok = records_push(out) && buf_append_str(&out->items[out->count - 1], line);
		} else {
			const char *cont = (line[0] == ' ') ? line + 1 : line;
			Buffer *rec = &out->items[out->count - 1];
			ok = buf_append_ch(rec, '\n') && buf_append_str(rec, cont);
		}

		free(line);
		line = NULL;
		if (!ok) {
			fclose(file);
			records_free(out);
			return 0;
		}
	}

	if (rc < 0) {
		fclose(file);
		records_free(out);
		return 0;
	}

	fclose(file);
	return 1;
}

/*
 * save_records - Write all records back to the notes file atomically
 *
 * The records are written to a temporary file next to the notes file and
 * then renamed over it.  A crash, a full disk, or a write error therefore
 * never truncates or corrupts the existing notes: the old file survives
 * until the new one is complete.
 *
 * Return: 1 on success, 0 on failure (the original file is left untouched)
 */
static int save_records(const RecordList *recs) {
	char temp_path[MAX_PATH + 16];
	FILE *file;
	int ok = 1;

	if (snprintf(temp_path, sizeof(temp_path), "%s.tmp", notes_file) >= (int)sizeof(temp_path)) {
		return 0;
	}

	file = fopen(temp_path, "w");
	if (file == NULL) return 0;

	for (size_t i = 0; i < recs->count; i++) {
		const char *p = (recs->items[i].data != NULL) ? recs->items[i].data : "";
		int first_line = 1;

		for (;;) {
			const char *nl = strchr(p, '\n');
			size_t n = (nl != NULL) ? (size_t)(nl - p) : strlen(p);

			if (!first_line && n > 0 && fputc(' ', file) == EOF) { ok = 0; break; }
			if (n > 0 && fwrite(p, 1, n, file) != n) { ok = 0; break; }
			if (fputc('\n', file) == EOF) { ok = 0; break; }

			first_line = 0;
			if (nl == NULL) break;
			p = nl + 1;
		}
		if (!ok) break;
	}

	if (ok && fflush(file) != 0) ok = 0;
	if (fclose(file) != 0) ok = 0;

	if (!ok) {
		remove(temp_path);
		return 0;
	}

#ifdef _WIN32
	remove(notes_file);
#endif
	if (rename(temp_path, notes_file) != 0) {
		remove(temp_path);
		return 0;
	}
	return 1;
}

/*
 * normalize_newlines - Turn CRLF and lone CR into LF, in place
 */
static void normalize_newlines(char *s) {
	char *w;

	if (s == NULL) return;
	w = s;
	for (char *r = s; *r != '\0'; r++) {
		if (*r == '\r') {
			if (r[1] == '\n') continue;
			*w++ = '\n';
		} else {
			*w++ = *r;
		}
	}
	*w = '\0';
}

/*
 * strip_timestamp - Remove a leading "[YYYY-MM-DD HH:MM:SS] " prefix
 *
 * @s: Text to strip in place; unchanged when no timestamp is present
 */
static void strip_timestamp(char *s) {
	size_t len;

	if (s == NULL || !is_record_header(s)) return;
	len = strlen(s);
	if (len > 22 && s[20] == ']' && s[21] == ' ') {
		memmove(s, s + 22, len - 22 + 1);
	} else if (len == 21) {
		s[0] = '\0';
	}
}

/*
 * mkdir_p - Create a directory and all missing parents
 *
 * Return: 1 on success (or the directory already exists), 0 on failure
 */
static int mkdir_p(const char *path) {
	char tmp[MAX_PATH];
	size_t len;

	if (path == NULL || path[0] == '\0') return 0;
	if (snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp)) return 0;

	len = strlen(tmp);
	while (len > 1 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\')) {
		tmp[--len] = '\0';
	}

	for (char *p = tmp + 1; *p != '\0'; p++) {
		if (*p != '/' && *p != '\\') continue;
		*p = '\0';
		if (mkdir(tmp, 0755) != 0) {
#ifndef _WIN32
			/* A failed mkdir is only fatal when the path is not a directory. */
			struct stat st;
			if (errno != EEXIST && (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode))) {
				*p = '/';
				return 0;
			}
#else
			return 0;
#endif
		}
		*p = '/';
	}

	if (mkdir(tmp, 0755) != 0) {
#ifndef _WIN32
		if (errno != EEXIST) return 0;
#else
		return 0;
#endif
	}
	return 1;
}

/*
 * init_notes_path - Initialize the notes file path based on platform
 *
 * Linux: ~/.local/share/notes.txt
 * Windows: notes.txt in current directory
 */
void init_notes_path(void) {
#ifdef _WIN32
	/* Windows: use current directory */
	strncpy(notes_file, "notes.txt", MAX_PATH - 1);
	notes_file[MAX_PATH - 1] = '\0';
#else
	/* Linux: use ~/.local/share/notes.txt */
	const char *home = getenv("HOME");

	if (home == NULL || home[0] == '\0') {
		/* Fallback to current directory if HOME not set */
		strncpy(notes_file, "notes.txt", MAX_PATH - 1);
		notes_file[MAX_PATH - 1] = '\0';
		return;
	}

	/* Build path: ~/.local/share and make sure it exists */
	char dir_path[MAX_PATH];
	if (snprintf(dir_path, MAX_PATH, "%s/.local/share", home) < MAX_PATH) {
		ensure_directory_exists(dir_path);
	}

	if (snprintf(notes_file, MAX_PATH, "%s/.local/share/notes.txt", home) >= MAX_PATH) {
		strncpy(notes_file, "notes.txt", MAX_PATH - 1);
		notes_file[MAX_PATH - 1] = '\0';
	}
#endif
}

/*
 * ensure_directory_exists - Create a directory (and parents) if needed
 *
 * @path: Directory path to create
 */
void ensure_directory_exists(const char *path) {
	if (!mkdir_p(path)) {
		fprintf(stderr, "Warning: Could not create directory %s\n", path);
	}
}

/*
 * main - Entry point of the program
 *
 * @argc: Number of command-line arguments
 * @argv: Array of command-line argument strings
 *
 * Return: 0 on success, 1 on error
 */
int main(int argc, char * argv[]) {
	/* Initialize platform-specific notes file path */
	init_notes_path();

	/* Check if a command is provided */
	if (argc < 2) {
		print_usage();
		return 0;
	}

	/* Copy the first argument as the command */
	strncpy(command, argv[1], sizeof(command) - 1);
	command[sizeof(command) - 1] = '\0';

	/* Process the command */
	return handle_args(argc, argv);
}

/*
 * print_usage - Display usage instructions
 */
void print_usage(void) {
	printf("smoke <command> [options]\n");
	printf("commands:\n");
	printf("  add new note: add [content] (opens editor when content is missing)\n");
	printf("  find existing notes: list\n");
	printf("  edit existing notes: edit <line_number> [new_content]\n");
	printf("\nNotes file: %s\n", notes_file);
}

/*
 * get_current_time - Get current local time as a formatted string
 *
 * @buffer: Buffer to store the formatted time string
 * @size: Size of the buffer
 */
void get_current_time(char *buffer, int size) {
	time_t currentTime;
	struct tm *localTime;

	time(&currentTime);
	localTime = localtime(&currentTime);
	if (localTime == NULL) {
		snprintf(buffer, (size_t)size, "0000-00-00 00:00:00");
		return;
	}
	strftime(buffer, (size_t)size, "%Y-%m-%d %H:%M:%S", localTime);
}

/*
 * handle_args - Route the command to the appropriate handler function
 *
 * @argc: Number of command-line arguments
 * @argv: Array of command-line argument strings
 */
int handle_args(int argc, char * argv[]) {
	if (strcmp(command, "add") == 0) {
		return add_note(argc, argv);
	}
	if (strcmp(command, "list") == 0) {
		return list_notes();
	}
	if (strcmp(command, "edit") == 0) {
		return edit_note(argc, argv);
	}
	print_usage();
	return 2;
}

/*
 * ends_with_newline - Report whether the file ends with a newline
 *
 * Appending to a file whose last byte is not '\n' would glue the new record
 * onto the previous one, so the append path checks this first.
 *
 * Return: 1 when the file is empty, missing, or ends with '\n'
 */
static int ends_with_newline(void) {
	FILE *file = fopen(notes_file, "rb");
	long size;
	int last = '\n';

	if (file == NULL) return 1;
	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return 1;
	}
	size = ftell(file);
	if (size > 0 && fseek(file, -1, SEEK_END) == 0) {
		last = fgetc(file);
	}
	fclose(file);
	return (size <= 0) || (last == '\n');
}

/*
 * write_note - Append one note record with a timestamp
 *
 * A note may contain newlines: the first line goes after the timestamp and
 * every following non-empty line is stored with one leading space, so the
 * record stays one logical note in the file.  Blank lines are stored blank.
 *
 * @content: Note text without timestamp
 *
 * Return: 1 on success, 0 on failure
 */
int write_note(const char *content) {
	FILE *file;
	char timeStr[64];
	const char *p;
	int first_line = 1;
	int ok = 1;

	if (content == NULL) content = "";

	file = fopen(notes_file, "a");
	if (file == NULL) {
		printf("Error: Cannot open notes file: %s\n", notes_file);
		return 0;
	}

	/* Never glue a new record onto a file that lacks a final newline. */
	if (!ends_with_newline() && fputc('\n', file) == EOF) ok = 0;

	get_current_time(timeStr, sizeof(timeStr));
	if (ok && fprintf(file, "[%s] ", timeStr) < 0) ok = 0;

	p = content;
	while (ok) {
		const char *nl = strchr(p, '\n');
		size_t n = (nl != NULL) ? (size_t)(nl - p) : strlen(p);

		if (!first_line && n > 0 && fputc(' ', file) == EOF) ok = 0;
		if (ok && n > 0 && fwrite(p, 1, n, file) != n) ok = 0;
		if (ok && fputc('\n', file) == EOF) ok = 0;

		first_line = 0;
		if (nl == NULL) break;
		p = nl + 1;
	}

	if (fclose(file) != 0) ok = 0;
	if (!ok) printf("Error: Cannot write to notes file: %s\n", notes_file);
	return ok;
}

/*
 * trim_whitespace - Remove leading and trailing spaces, tabs, and newlines
 *
 * @s: String to trim in place
 */
void trim_whitespace(char *s) {
	size_t i = 0;
	size_t len;

	if (s == NULL) return;

	while (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') i++;
	if (i > 0) memmove(s, s + i, strlen(s + i) + 1);

	len = strlen(s);
	while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
	                  s[len - 1] == '\r' || s[len - 1] == '\n')) {
		s[--len] = '\0';
	}
}

/*
 * read_editor_content - Read the edited file and build one note text
 *
 * Leading and trailing blank lines are dropped, inner blank lines are kept
 * (one blank line per blank line, so paragraphs survive).  Trailing spaces
 * are trimmed from each line.
 *
 * @path: File written by the editor
 * @strip_hash_lines: 1 to ignore lines that start with '#'
 * @out: Receives the note text (may stay empty)
 *
 * Return: 1 on success, 0 on failure
 */
static int read_editor_content(const char *path, int strip_hash_lines, Buffer *out) {
	FILE *file = fopen(path, "r");
	char *line = NULL;
	int rc;
	int have_content = 0;
	size_t pending_blanks = 0;

	if (file == NULL) return 0;

	buf_reset(out);

	while ((rc = read_line_dyn(file, &line)) == 1) {
		size_t len = strlen(line);
		int ok = 1;

		while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t')) {
			line[--len] = '\0';
		}

		if (strip_hash_lines && line[0] == '#') {
			free(line);
			line = NULL;
			continue;
		}

		if (line[0] == '\0') {
			if (have_content) pending_blanks++;
			free(line);
			line = NULL;
			continue;
		}

		if (have_content) {
			for (size_t i = 0; i < pending_blanks; i++) ok = ok && buf_append_ch(out, '\n');
			ok = ok && buf_append_ch(out, '\n');
		}
		pending_blanks = 0;
		ok = ok && buf_append_str(out, line);
		have_content = 1;

		free(line);
		line = NULL;
		if (!ok) {
			fclose(file);
			return 0;
		}
	}

	if (rc < 0) {
		fclose(file);
		return 0;
	}

	fclose(file);
	return 1;
}

/*
 * run_editor - Open the user's editor on a temporary file
 *
 * Uses $VISUAL, then $EDITOR, then vi (notepad on Windows).
 *
 * @initial: Text to seed the file with, or NULL to seed hint lines
 * @strip_hash_lines: 1 to ignore lines that start with '#' after editing
 * @out: Receives the edited text (empty when nothing was written)
 *
 * Return: 1 on success, 0 on failure
 */
static int run_editor(const char *initial, int strip_hash_lines, Buffer *out) {
	char tempPath[MAX_PATH];
	const char *editor;
	FILE *seed;
	char *editorCmd;
	size_t need;
	int status;
	int ok;

	buf_reset(out);

#ifdef _WIN32
	{
		char tempDir[MAX_PATH];
		if (GetTempPathA(MAX_PATH, tempDir) == 0 ||
		    GetTempFileNameA(tempDir, "smk", 0, tempPath) == 0) {
			printf("Error: Cannot create a temporary file.\n");
			return 0;
		}
	}
#else
	strcpy(tempPath, "/tmp/smoke-note-XXXXXX");
	{
		int fd = mkstemp(tempPath);
		if (fd < 0) {
			printf("Error: Cannot create a temporary file.\n");
			return 0;
		}
		close(fd);
	}
#endif

	/* Seed the temp file */
	seed = fopen(tempPath, "w");
	if (seed == NULL) {
		printf("Error: Cannot write to the temporary file.\n");
		remove(tempPath);
		return 0;
	}
	if (initial != NULL) {
		fputs(initial, seed);
	} else {
		fprintf(seed, "# Write your note below. The whole text becomes one note.\n");
		fprintf(seed, "# Lines that start with '#' are ignored. Empty notes are not saved.\n");
	}
	if (fclose(seed) != 0) {
		printf("Error: Cannot write to the temporary file.\n");
		remove(tempPath);
		return 0;
	}

	/* Resolve the editor */
#ifdef _WIN32
	editor = getenv("EDITOR");
	if (editor == NULL || editor[0] == '\0') editor = "notepad";
#else
	editor = getenv("VISUAL");
	if (editor == NULL || editor[0] == '\0') editor = getenv("EDITOR");
	if (editor == NULL || editor[0] == '\0') editor = "vi";
#endif

	/* Run the editor and wait for it to exit */
	need = strlen(editor) + strlen(tempPath) + 8;
	editorCmd = (char *)malloc(need);
	if (editorCmd == NULL) {
		printf("Error: Out of memory.\n");
		remove(tempPath);
		return 0;
	}
	snprintf(editorCmd, need, "%s \"%s\"", editor, tempPath);
	status = system(editorCmd);
	free(editorCmd);

#ifdef _WIN32
	ok = (status == 0);
#else
	ok = (status != -1) && WIFEXITED(status) && (WEXITSTATUS(status) == 0);
#endif

	if (!ok) {
		printf("Error: The editor exited with an error. Nothing saved.\n");
		remove(tempPath);
		return 0;
	}

	ok = read_editor_content(tempPath, strip_hash_lines, out);
	remove(tempPath);

	if (!ok) {
		printf("Error: Cannot read the temporary file.\n");
		return 0;
	}
	return 1;
}

/*
 * add_note_via_editor - Open the default editor to compose a new note
 *
 * The whole editor content becomes ONE note, even when it spans several
 * lines or paragraphs.  Blank content records nothing.
 *
 * Return: 1 when the note was saved, 0 otherwise
 */
int add_note_via_editor(void) {
	Buffer content;
	int ok;

	buf_init(&content);
	if (!run_editor(NULL, 1, &content)) {
		buf_free(&content);
		return 1;
	}

	if (content.len == 0) {
		printf("Note is empty. Nothing saved.\n");
		buf_free(&content);
		return 0;
	}

	normalize_newlines(content.data);
	ok = write_note(content.data);
	buf_free(&content);

	if (!ok) return 1;
	printf("Note added successfully.\n");
	return 0;
}

/*
 * add_note - Add a new note to the notes file
 *
 * @argc: Number of command-line arguments
 * @argv: Array of command-line argument strings
 *
 * With content: the note text comes from argv[2] onwards (no length limit).
 * Without content: the default editor opens to compose the note.
 */
int add_note(int argc, char * argv[]) {
	Buffer content;
	int ok = 1;

	/* No content on the command line: open the editor */
	if (argc < 3) {
		return add_note_via_editor();
	}

	buf_init(&content);
	for (int i = 2; i < argc && ok; i++) {
		if (i > 2) ok = buf_append_ch(&content, ' ');
		if (ok) ok = buf_append_str(&content, argv[i]);
	}

	if (!ok) {
		printf("Error: Out of memory.\n");
		buf_free(&content);
		return 1;
	}

	normalize_newlines(content.data);
	trim_whitespace(content.data);

	if (content.len == 0) {
		printf("Note is empty. Nothing saved.\n");
		buf_free(&content);
		return 0;
	}

	ok = write_note(content.data);
	buf_free(&content);
	if (!ok) return 1;
	printf("Note added successfully.\n");
	return 0;
}

/*
 * list_notes - Display all notes from the notes file
 *
 * Each note is displayed with a line number for easy reference.  Multi-line
 * notes are printed in full, never truncated.
 */
int list_notes(void) {
	RecordList records;

	if (!load_records(&records) || records.count == 0) {
		records_free(&records);
		printf("No notes found.\n");
		printf("Notes file: %s\n", notes_file);
		return 0;
	}

	printf("=== Notes ===\n");
	for (size_t i = 0; i < records.count; i++) {
		printf("%lu. %s\n", (unsigned long)(i + 1),
		       (records.items[i].data != NULL) ? records.items[i].data : "");
	}
	records_free(&records);
	return 0;
}

/*
 * utf8_prev_len - Byte length of the character that ends just before 'pos'
 */
static size_t utf8_prev_len(const char *s, size_t pos) {
	size_t start;

	if (pos == 0) return 0;
	start = pos - 1;
	while (start > 0 && ((unsigned char)s[start] & 0xC0) == 0x80 && (pos - start) < 4) {
		start--;
	}
	return pos - start;
}

/*
 * utf8_next_len - Byte length of the character that starts at 'pos'
 */
static size_t utf8_next_len(const char *s, size_t pos, size_t len) {
	unsigned char c;
	size_t n = 1;

	if (pos >= len) return 0;
	c = (unsigned char)s[pos];
	if (c >= 0xF0) n = 4;
	else if (c >= 0xE0) n = 3;
	else if (c >= 0xC0) n = 2;
	if (pos + n > len) n = 1;
	return n;
}

#ifndef _WIN32
/*
 * irl_redraw - Redraw the interactive input line
 */
static void irl_redraw(const char *prompt, const char *buf, size_t len, size_t pos) {
	printf("\r\033[K");
	if (prompt != NULL) printf("%s", prompt);
	if (len > 0) fwrite(buf, 1, len, stdout);
	for (size_t i = pos; i < len; i++) putchar('\b');
	fflush(stdout);
}

/*
 * read_line_interactive - Read a line with interactive editing support
 *
 * @out: Buffer that receives the input (grows without limit)
 * @prompt: Prompt string to display (can be NULL)
 *
 * Supports: Left/Right arrows, Home, End, Backspace, Delete, Ctrl+A, Ctrl+E,
 * Ctrl+D, Ctrl+U, and multi-byte UTF-8 input.
 *
 * Return: Length of the input string, or -1 on EOF/allocation failure
 */
int read_line_interactive(Buffer *out, const char *prompt) {
	struct termios orig_termios, new_termios;
	size_t pos = 0;
	int ch;

	buf_reset(out);

	/* Save original terminal settings */
	if (tcgetattr(STDIN_FILENO, &orig_termios) < 0) {
		/* Fallback to plain line reading when stdin is not a terminal */
		char *line = NULL;
		int rc = read_line_dyn(stdin, &line);
		int ok;

		if (rc != 1) {
			free(line);
			return -1;
		}
		ok = buf_append_str(out, line);
		free(line);
		return ok ? (int)out->len : -1;
	}

	/* Set terminal to raw mode */
	new_termios = orig_termios;
	new_termios.c_lflag &= (tcflag_t)~(ICANON | ECHO);
	new_termios.c_cc[VMIN] = 1;
	new_termios.c_cc[VTIME] = 0;

	if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) < 0) {
		char *line = NULL;
		int rc;
		int ok;

		tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
		if (prompt != NULL) printf("%s", prompt);
		rc = read_line_dyn(stdin, &line);
		if (rc != 1) {
			free(line);
			return -1;
		}
		ok = buf_append_str(out, line);
		free(line);
		return ok ? (int)out->len : -1;
	}

	if (prompt != NULL) {
		printf("%s", prompt);
		fflush(stdout);
	}

	while (1) {
		ch = getchar();

		if (ch == EOF) {
			if (out->len == 0) {
				tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
				return -1;
			}
			break;
		}

		if (ch == '\n' || ch == '\r') {
			break;
		} else if (ch == 127 || ch == 8) {
			/* Backspace - delete the character before the cursor */
			if (pos > 0) {
				size_t n = utf8_prev_len(out->data, pos);
				memmove(out->data + pos - n, out->data + pos, out->len - pos + 1);
				pos -= n;
				out->len -= n;
				irl_redraw(prompt, out->data, out->len, pos);
			}
		} else if (ch == 27) {
			/* Escape sequence - arrows, Home, End, Delete */
			int c2 = getchar();
			if (c2 == '[' || c2 == 'O') {
				int num = -1;
				int final = getchar();

				while (final != EOF && !(final >= 0x40 && final <= 0x7E)) {
					if (final >= '0' && final <= '9') {
						num = (num < 0 ? 0 : num) * 10 + (final - '0');
					}
					final = getchar();
				}

				if (final == 'D') {
					if (pos > 0) {
						pos -= utf8_prev_len(out->data, pos);
						irl_redraw(prompt, out->data, out->len, pos);
					}
				} else if (final == 'C') {
					if (pos < out->len) {
						pos += utf8_next_len(out->data, pos, out->len);
						irl_redraw(prompt, out->data, out->len, pos);
					}
				} else if (final == 'H') {
					pos = 0;
					irl_redraw(prompt, out->data, out->len, pos);
				} else if (final == 'F') {
					pos = out->len;
					irl_redraw(prompt, out->data, out->len, pos);
				} else if (final == '~') {
					if (num == 1 || num == 7) {
						pos = 0;
						irl_redraw(prompt, out->data, out->len, pos);
					} else if (num == 4 || num == 8) {
						pos = out->len;
						irl_redraw(prompt, out->data, out->len, pos);
					} else if (num == 3 && pos < out->len) {
						size_t n = utf8_next_len(out->data, pos, out->len);
						memmove(out->data + pos, out->data + pos + n, out->len - pos - n + 1);
						out->len -= n;
						irl_redraw(prompt, out->data, out->len, pos);
					}
				}
			}
		} else if (ch == 1) {
			/* Ctrl+A - go to the beginning */
			pos = 0;
			irl_redraw(prompt, out->data, out->len, pos);
		} else if (ch == 5) {
			/* Ctrl+E - go to the end */
			pos = out->len;
			irl_redraw(prompt, out->data, out->len, pos);
		} else if (ch == 4) {
			/* Ctrl+D - delete the character at the cursor */
			if (pos < out->len) {
				size_t n = utf8_next_len(out->data, pos, out->len);
				memmove(out->data + pos, out->data + pos + n, out->len - pos - n + 1);
				out->len -= n;
				irl_redraw(prompt, out->data, out->len, pos);
			}
		} else if (ch == 21) {
			/* Ctrl+U - clear the whole line */
			buf_reset(out);
			pos = 0;
			irl_redraw(prompt, out->data, out->len, pos);
		} else if (ch == 9) {
			/* Tab - insert a space instead of a control character */
			if (!buf_reserve(out, 1)) break;
			memmove(out->data + pos + 1, out->data + pos, out->len - pos + 1);
			out->data[pos] = ' ';
			out->len++;
			out->data[out->len] = '\0';
			pos++;
			irl_redraw(prompt, out->data, out->len, pos);
		} else if (ch >= 32) {
			/* Printable character (also accepts UTF-8 continuation bytes) */
			if (!buf_reserve(out, 1)) break;
			memmove(out->data + pos + 1, out->data + pos, out->len - pos + 1);
			out->data[pos] = (char)ch;
			out->len++;
			out->data[out->len] = '\0';
			pos++;
			irl_redraw(prompt, out->data, out->len, pos);
		}
	}

	tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
	printf("\n");
	fflush(stdout);
	return (int)out->len;
}
#else
/*
 * read_line_interactive - Read one line from standard input (Windows)
 *
 * Return: Length of the input string, or -1 on EOF/allocation failure
 */
int read_line_interactive(Buffer *out, const char *prompt) {
	char *line = NULL;
	int rc;
	int ok;

	buf_reset(out);
	if (prompt != NULL) {
		printf("%s", prompt);
		fflush(stdout);
	}

	rc = read_line_dyn(stdin, &line);
	if (rc != 1) {
		free(line);
		return -1;
	}
	ok = buf_append_str(out, line);
	free(line);
	return ok ? (int)out->len : -1;
}
#endif

/*
 * edit_note - Edit an existing note by line number
 *
 * @argc: Number of command-line arguments
 * @argv: Array of command-line argument strings
 *
 * argv[2] should contain the line number to edit
 * argv[3] onwards contains the new content (optional)
 * Without new content, a single-line note is edited on the prompt and a
 * multi-line note is opened in the editor, seeded with its current text.
 */
int edit_note(int argc, char * argv[]) {
	RecordList records;
	Buffer newContent;
	Buffer record;
	long targetLine;
	char *endptr = NULL;
	char timeStr[64];

	/* Validate arguments */
	if (argc < 3) {
		printf("Usage: smoke edit <line_number> [new_content]\n");
		return 2;
	}

	/* Parse the line number strictly */
	errno = 0;
	targetLine = strtol(argv[2], &endptr, 10);
	if (errno == ERANGE || endptr == argv[2] || *endptr != '\0' || targetLine <= 0) {
		printf("Error: Invalid line number\n");
		return 1;
	}

	/* Read all existing notes into memory */
	if (!load_records(&records) || records.count == 0) {
		records_free(&records);
		printf("No notes found.\n");
		printf("Notes file: %s\n", notes_file);
		return 0;
	}

	/* Validate line number exists */
	if ((size_t)targetLine > records.count) {
		printf("Error: Line %ld does not exist (total %lu)\n",
		       targetLine, (unsigned long)records.count);
		records_free(&records);
		return 1;
	}

	buf_init(&newContent);

	if (argc >= 4) {
		/* New content provided via command line */
		int ok = 1;
		for (int i = 3; i < argc && ok; i++) {
			if (i > 3) ok = buf_append_ch(&newContent, ' ');
			if (ok) ok = buf_append_str(&newContent, argv[i]);
		}
		if (!ok) {
			printf("Error: Out of memory.\n");
			buf_free(&newContent);
			records_free(&records);
			return 1;
		}
	} else if (strchr(records.items[targetLine - 1].data, '\n') != NULL) {
		/* Multi-line note: edit it in the editor, seeded with its text.
		 * The timestamp is stripped first; it is re-added after editing. */
		Buffer edited;
		Buffer seed;
		int ok;

		buf_init(&seed);
		buf_init(&edited);
		if (!buf_append_str(&seed, records.items[targetLine - 1].data)) {
			printf("Error: Out of memory.\n");
			buf_free(&seed);
			buf_free(&edited);
			buf_free(&newContent);
			records_free(&records);
			return 1;
		}
		strip_timestamp(seed.data);

		ok = run_editor(seed.data, 0, &edited);
		buf_free(&seed);
		if (!ok) {
			buf_free(&edited);
			buf_free(&newContent);
			records_free(&records);
			return 1;
		}
		if (edited.len > 0) buf_append_str(&newContent, edited.data);
		buf_free(&edited);
	} else {
		/* Single-line note: edit it on the prompt */
		printf("Current: %s\n", records.items[targetLine - 1].data);
		if (read_line_interactive(&newContent, "Enter new content: ") <= 0) {
			buf_reset(&newContent);
		}
	}

	normalize_newlines(newContent.data);
	trim_whitespace(newContent.data);

	if (newContent.len == 0) {
		printf("Note is empty. Nothing saved.\n");
		buf_free(&newContent);
		records_free(&records);
		return 0;
	}

	/* Build the new record: timestamp + content */
	get_current_time(timeStr, sizeof(timeStr));
	buf_init(&record);
	if (!buf_append_ch(&record, '[') ||
	    !buf_append_str(&record, timeStr) ||
	    !buf_append_str(&record, "] ") ||
	    !buf_append_str(&record, newContent.data)) {
		printf("Error: Out of memory.\n");
		buf_free(&record);
		buf_free(&newContent);
		records_free(&records);
		return 1;
	}
	buf_free(&newContent);

	buf_free(&records.items[targetLine - 1]);
	records.items[targetLine - 1] = record;

	/* Write all notes back to file */
	if (!save_records(&records)) {
		printf("Error: Cannot write to notes file: %s\n", notes_file);
		records_free(&records);
		return 1;
	}
	records_free(&records);
	printf("Note updated successfully.\n");
	return 0;
}
