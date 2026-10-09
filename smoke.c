/*
 * smoke.c - A simple command-line note-taking application
 * 
 * This program allows users to add, list, and edit notes
 * Notes are stored in a local text file with timestamps
 * 
 * Platform-specific storage:
 * - Linux: ~/.local/share/notes.txt
 * - Windows: notes.txt in current directory
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

/* Platform detection */
#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #include <conio.h>
    #define PATH_SEPARATOR "\\"
    #define mkdir(path, mode) _mkdir(path)
#else
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <unistd.h>
    #include <errno.h>
    #include <termios.h>
    #include <fcntl.h>
    #define PATH_SEPARATOR "/"
#endif

/* Constants */
#define MAX_LINE 1024             /* Maximum length of a single line */
#define MAX_PATH 512              /* Maximum path length */

/* Global variables */
char command[256];                /* Store the command from user input */
char notes_file[MAX_PATH];        /* Full path to the notes file */

/* Function declarations */
void init_notes_path(void);
void ensure_directory_exists(const char *path);
void handle_args(int argc, char * argv[]);
void print_usage();
void get_current_time(char *buffer, int size);
void add_note(int argc, char * argv[]);
void list_notes();
void edit_note(int argc, char * argv[]);
int write_note(const char *content);
int add_note_via_editor(void);
void trim_whitespace(char *s);
#ifndef _WIN32
int read_line_interactive(char *buffer, int size, const char *prompt);
#endif

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
	if (home == NULL) {
		/* Fallback to current directory if HOME not set */
		strncpy(notes_file, "notes.txt", MAX_PATH - 1);
		notes_file[MAX_PATH - 1] = '\0';
		return;
	}
	
	/* Build path: ~/.local/share */
	char dir_path[MAX_PATH];
	snprintf(dir_path, MAX_PATH, "%s/.local/share", home);
	
	/* Ensure directory exists */
	ensure_directory_exists(dir_path);
	
	/* Build full file path */
	snprintf(notes_file, MAX_PATH, "%s/.local/share/notes.txt", home);
#endif
}

/*
 * ensure_directory_exists - Create directory if it doesn't exist
 * 
 * @path: Directory path to create
 */
void ensure_directory_exists(const char *path) {
	struct stat st = {0};
	
	if (stat(path, &st) == -1) {
		/* Directory doesn't exist, create it with permissions 0755 */
		if (mkdir(path, 0755) != 0 && errno != EEXIST) {
			/* Failed to create directory, will fall back to current dir */
			fprintf(stderr, "Warning: Could not create directory %s\n", path);
		}
	}
}

/*
 * main - Entry point of the program
 * 
 * @argc: Number of command-line arguments
 * @argv: Array of command-line argument strings
 * 
 * Return: 0 on success
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
	handle_args(argc, argv);
	return 0;
}

/*
 * print_usage - Display usage instructions
 */
void print_usage() {
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
	strftime(buffer, size, "%Y-%m-%d %H:%M:%S", localTime);
}

/*
 * handle_args - Route the command to the appropriate handler function
 * 
 * @argc: Number of command-line arguments
 * @argv: Array of command-line argument strings
 */
void handle_args(int argc, char * argv[]) {
	if (strcmp(command, "add") == 0) {
		add_note(argc, argv);
	} else if (strcmp(command, "list") == 0) {
		list_notes();
	} else if (strcmp(command, "edit") == 0) {
		edit_note(argc, argv);
	} else {
		print_usage();
	}
}

/*
 * write_note - Append one note line with a timestamp
 *
 * @content: Note text without timestamp
 *
 * Return: 1 on success, 0 on failure
 */
int write_note(const char *content) {
	FILE *file = fopen(notes_file, "a"); //it is so different to deal with external files in C
	if (!file) {
		printf("Error: Cannot open notes file: %s\n", notes_file);
		return 0;
	}
	
	char timeStr[100];
	get_current_time(timeStr, sizeof(timeStr));
	
	fprintf(file, "[%s] %s\n", timeStr, content);
	fclose(file);
	return 1;
}

/*
 * trim_whitespace - Remove leading and trailing spaces, tabs, and newlines
 *
 * @s: String to trim in place
 */
void trim_whitespace(char *s) {
	int i = 0;
	while (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') i++;
	if (i > 0) memmove(s, s + i, strlen(s + i) + 1);
	
	size_t len = strlen(s);
	while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
	                  s[len - 1] == '\r' || s[len - 1] == '\n')) {
		s[--len] = '\0';
	}
}

/*
 * add_note_via_editor - Open the default editor to compose a new note
 *
 * Uses $VISUAL, then $EDITOR, then vi (notepad on Windows).
 * The editor opens a temp file that starts with a hint line.
 * Lines that start with '#' are hints and are ignored.
 * Each other non-empty line becomes one note.
 * Blank content records nothing.
 *
 * Return: 1 when at least one note was saved, 0 otherwise
 */
int add_note_via_editor(void) {
	const char *editor;
	char tempPath[MAX_PATH];
	
#ifdef _WIN32
	char tempDir[MAX_PATH];
	if (GetTempPathA(MAX_PATH, tempDir) == 0 ||
	    GetTempFileNameA(tempDir, "smk", 0, tempPath) == 0) {
		printf("Error: Cannot create a temporary file.\n");
		return 0;
	}
	editor = getenv("EDITOR");
	if (editor == NULL || editor[0] == '\0') editor = "notepad";
#else
	strcpy(tempPath, "/tmp/smoke-note-XXXXXX");
	int fd = mkstemp(tempPath);
	if (fd < 0) {
		printf("Error: Cannot create a temporary file.\n");
		return 0;
	}
	close(fd);
	
	editor = getenv("VISUAL");
	if (editor == NULL || editor[0] == '\0') editor = getenv("EDITOR");
	if (editor == NULL || editor[0] == '\0') editor = "vi";
#endif
	
	/* Seed the temp file with a hint line */
	FILE *seed = fopen(tempPath, "w");
	if (seed == NULL) {
		printf("Error: Cannot write to the temporary file.\n");
		remove(tempPath);
		return 0;
	}
	fprintf(seed, "# Write your note below. One line = one note.\n");
	fprintf(seed, "# Lines that start with '#' are ignored. Empty notes are not saved.\n");
	fclose(seed);
	
	/* Run the editor and wait for it to exit */
	char editorCmd[MAX_PATH + 256];
	snprintf(editorCmd, sizeof(editorCmd), "%s \"%s\"", editor, tempPath);
	int status = system(editorCmd);
	if (status != 0) {
		printf("Error: The editor exited with an error. Nothing saved.\n");
		remove(tempPath);
		return 0;
	}
	
	/* Read the edited file back */
	FILE *rf = fopen(tempPath, "r");
	if (rf == NULL) {
		printf("Error: Cannot read the temporary file.\n");
		remove(tempPath);
		return 0;
	}
	
	char line[MAX_LINE];
	int saved = 0;
	while (fgets(line, sizeof(line), rf)) {
		line[strcspn(line, "\r\n")] = '\0';
		trim_whitespace(line);
		if (line[0] == '\0' || line[0] == '#') continue;
		if (write_note(line)) saved++;
	}
	fclose(rf);
	remove(tempPath);
	
	if (saved == 0) {
		printf("Note is empty. Nothing saved.\n");
		return 0;
	}
	if (saved == 1) {
		printf("Note added successfully.\n");
	} else {
		printf("Notes added successfully: %d.\n", saved);
	}
	return 1;
}

/*
 * add_note - Add a new note to the notes file
 * 
 * @argc: Number of command-line arguments
 * @argv: Array of command-line argument strings
 * 
 * With content: the note text comes from argv[2] onwards.
 * Without content: the default editor opens to compose the note.
 */
void add_note(int argc, char * argv[]) {
	/* No content on the command line: open the editor */
	if (argc < 3) {
		add_note_via_editor();
		return;
	}
	
	/* Join all arguments into one string */
	char content[MAX_LINE];
	size_t len = 0;
	content[0] = '\0';
	for (int i = 2; i < argc; i++) {
		int written = snprintf(content + len, MAX_LINE - len, "%s%s",
		                       (len > 0) ? " " : "", argv[i]);
		if (written < 0 || (size_t)written >= MAX_LINE - len) {
			printf("Error: Note is too long. Max %d characters.\n", MAX_LINE - 1);
			return;
		}
		len += (size_t)written;
	}
	
	trim_whitespace(content);
	if (content[0] == '\0') {
		printf("Note is empty. Nothing saved.\n");
		return;
	}
	
	if (write_note(content)) {
		printf("Note added successfully.\n");
	}
}

/*
 * list_notes - Display all notes from the notes file
 * 
 * Each note is displayed with a line number for easy reference
 */
void list_notes() {
	/* Open file in read mode */
	FILE *file = fopen(notes_file, "r");
	if (!file) {
		printf("No notes found.\n");
		printf("Notes file: %s\n", notes_file);
		return;
	}
	
	/* Read and print each line with line number */
	char line[MAX_LINE];
	int lineNum = 1;
	printf("=== Notes ===\n");
	while (fgets(line, sizeof(line), file)) {
		printf("%d. %s", lineNum, line);
		lineNum++;
	}
	fclose(file);
}

#ifndef _WIN32
/*
 * read_line_interactive - Read a line with interactive editing support
 * 
 * @buffer: Buffer to store the input line
 * @size: Size of the buffer
 * @prompt: Prompt string to display (can be NULL)
 * 
 * Supports: Left/Right arrows, Home, End, Backspace, Delete
 * 
 * Return: Length of the input string
 */
int read_line_interactive(char *buffer, int size, const char *prompt) {
	struct termios orig_termios, new_termios;
	int len = 0;
	int pos = 0;
	int ch;
	
	/* Save original terminal settings */
	if (tcgetattr(STDIN_FILENO, &orig_termios) < 0) {
		/* Fallback to fgets if terminal control fails */
		if (prompt) printf("%s", prompt);
		if (fgets(buffer, size, stdin)) {
			buffer[strcspn(buffer, "\n")] = '\0';
			return strlen(buffer);
		}
		return 0;
	}
	
	/* Set terminal to raw mode */
	new_termios = orig_termios;
	new_termios.c_lflag &= ~(ICANON | ECHO);
	new_termios.c_cc[VMIN] = 1;
	new_termios.c_cc[VTIME] = 0;
	
	if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) < 0) {
		tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
		if (prompt) printf("%s", prompt);
		if (fgets(buffer, size, stdin)) {
			buffer[strcspn(buffer, "\n")] = '\0';
			return strlen(buffer);
		}
		return 0;
	}
	
	/* Display prompt if provided */
	if (prompt) {
		printf("%s", prompt);
		fflush(stdout);
	}
	
	while (1) {
		ch = getchar();
		
		if (ch == '\n' || ch == '\r') {
			/* Enter key - finish input */
			buffer[len] = '\0';
			printf("\n");
			break;
		} else if (ch == 127 || ch == 8) {
			/* Backspace - delete character before cursor */
			if (pos > 0) {
				/* Shift characters left */
				for (int i = pos - 1; i < len; i++) {
					buffer[i] = buffer[i + 1];
				}
				len--;
				pos--;
				/* Redraw line */
				printf("\b");
				for (int i = pos; i < len; i++) {
					printf("%c", buffer[i]);
				}
				printf(" \b");
				/* Move cursor back to position */
				for (int i = len; i > pos; i--) {
					printf("\b");
				}
				fflush(stdout);
			}
		} else if (ch == 27) {
			/* Escape sequence - handle arrow keys, Home, End */
			if (getchar() == '[') {
				int key = getchar();
				switch (key) {
					case 'D': /* Left arrow */
						if (pos > 0) {
							pos--;
							printf("\b");
							fflush(stdout);
						}
						break;
					case 'C': /* Right arrow */
						if (pos < len) {
							printf("%c", buffer[pos]);
							pos++;
							fflush(stdout);
						}
						break;
					case 'H': /* Home (some terminals) */
					case '1': /* Home (VT100) */
						if (key == '1') getchar(); /* consume ~ */
						while (pos > 0) {
							printf("\b");
							pos--;
						}
						fflush(stdout);
						break;
					case 'F': /* End (some terminals) */
					case '4': /* End (VT100) */
						if (key == '4') getchar(); /* consume ~ */
						while (pos < len) {
							printf("%c", buffer[pos]);
							pos++;
						}
						fflush(stdout);
						break;
					case '3': /* Delete key */
						getchar(); /* consume ~ */
						if (pos < len) {
							/* Shift characters left */
							for (int i = pos; i < len; i++) {
								buffer[i] = buffer[i + 1];
							}
							len--;
							/* Redraw line */
							for (int i = pos; i < len; i++) {
								printf("%c", buffer[i]);
							}
							printf(" \b");
							/* Move cursor back to position */
							for (int i = len; i > pos; i--) {
								printf("\b");
							}
							fflush(stdout);
						}
						break;
					case 'A': /* Up arrow - ignore */
					case 'B': /* Down arrow - ignore */
						break;
				}
			}
		} else if (ch == 1) {
			/* Ctrl+A - Go to beginning */
			while (pos > 0) {
				printf("\b");
				pos--;
			}
			fflush(stdout);
		} else if (ch == 5) {
			/* Ctrl+E - Go to end */
			while (pos < len) {
				printf("%c", buffer[pos]);
				pos++;
			}
			fflush(stdout);
		} else if (ch == 4) {
			/* Ctrl+D - Delete character at cursor (like Delete) */
			if (pos < len) {
				for (int i = pos; i < len; i++) {
					buffer[i] = buffer[i + 1];
				}
				len--;
				for (int i = pos; i < len; i++) {
					printf("%c", buffer[i]);
				}
				printf(" \b");
				for (int i = len; i > pos; i--) {
					printf("\b");
				}
				fflush(stdout);
			}
		} else if (ch >= 32 && ch < 127 && len < size - 1) {
			/* Printable character - insert at cursor position */
			/* Shift characters right to make room */
			for (int i = len; i > pos; i--) {
				buffer[i] = buffer[i - 1];
			}
			buffer[pos] = ch;
			len++;
			/* Print character and rest of line */
			for (int i = pos; i < len; i++) {
				printf("%c", buffer[i]);
			}
			pos++;
			/* Move cursor back to correct position */
			for (int i = len; i > pos; i--) {
				printf("\b");
			}
			fflush(stdout);
		}
	}
	
	/* Restore original terminal settings */
	tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
	
	return len;
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
 * If no new content is provided, user will be prompted to enter it
 */
void edit_note(int argc, char * argv[]) {
	/* Validate arguments */
	if (argc < 3) {
		printf("Usage: smoke edit <line_number> [new_content]\n");
		return;
	}
	
	/* Parse line number */
	int targetLine = atoi(argv[2]);
	if (targetLine <= 0) {
		printf("Error: Invalid line number\n");
		return;
	}
	
	/* Read all existing notes into memory */
	FILE *file = fopen(notes_file, "r");
	if (!file) {
		printf("No notes found.\n");
		printf("Notes file: %s\n", notes_file);
		return;
	}
	
	char lines[1000][MAX_LINE];
	int totalLines = 0;
	
	while (fgets(lines[totalLines], MAX_LINE, file) && totalLines < 1000) {
		totalLines++;
	}
	fclose(file);
	
	/* Validate line number exists */
	if (targetLine > totalLines) {
		printf("Error: Line %d does not exist\n", targetLine);
		return;
	}
	
	/* Update the target line with new content */
	if (argc >= 4) {
		/* New content provided via command line */
		char timeStr[100];
		get_current_time(timeStr, sizeof(timeStr));
		
		snprintf(lines[targetLine - 1], MAX_LINE, "[%s] ", timeStr);
		for (int i = 3; i < argc; i++) {
			strncat(lines[targetLine - 1], argv[i], MAX_LINE - strlen(lines[targetLine - 1]) - 1);
			if (i < argc - 1) strncat(lines[targetLine - 1], " ", MAX_LINE - strlen(lines[targetLine - 1]) - 1);
		}
		strncat(lines[targetLine - 1], "\n", MAX_LINE - strlen(lines[targetLine - 1]) - 1);
	} else {
		/* Prompt user for new content interactively */
		printf("Current: %s", lines[targetLine - 1]);
		char newContent[MAX_LINE];
#ifdef _WIN32
		printf("Enter new content: ");
		if (fgets(newContent, sizeof(newContent), stdin)) {
			newContent[strcspn(newContent, "\n")] = '\0';
#else
		if (read_line_interactive(newContent, sizeof(newContent), "Enter new content: ") > 0) {
#endif
			char timeStr[100];
			get_current_time(timeStr, sizeof(timeStr));
			snprintf(lines[targetLine - 1], MAX_LINE, "[%s] %s\n", timeStr, newContent);
		}
	}
	
	/* Write all notes back to file */
	file = fopen(notes_file, "w");
	if (!file) {
		printf("Error: Cannot write to notes file: %s\n", notes_file);
		return;
	}
	
	for (int i = 0; i < totalLines; i++) {
		fputs(lines[i], file);
	}
	fclose(file);
	printf("Note updated successfully.\n");
}
