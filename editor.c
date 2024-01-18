/** Includes **/

#include <ctype.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/** Defines **/

#define VERSION "0.0.1"
#define INITIAL_BUFFER_SIZE 1 << 12
#define CTRL_KEY(k) ((k) & 0x1f)

/** Typedefs **/

typedef struct {
    char *string;
    uint64_t len;
} string_t;

typedef struct {
    char *buffer;
    uint64_t len;
    uint64_t size;
} append_buffer;

struct state {
    uint64_t x, y;
    uint8_t rows, cols;
    append_buffer a_buf;
    struct termios orig_termios;
};

struct state state;

typedef enum {
    CONTINUE,
    RELOAD,
    EXIT,
} after_keypress;

/** Terminal **/

void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &state.orig_termios);
}

void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &state.orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = state.orig_termios;
    // Flags for enabling raw mode
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    // Function read returns as soon as there is any input to be read
    raw.c_cc[VMIN] = 0;
    // Function read times out after 1 second of no input (returns 0)
    raw.c_cc[VTIME] = 1;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

bool get_window_size(uint8_t *rows, uint8_t *cols) {

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        return false;
    }

    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return true;
}

/** Append Buffer **/

void free_append_buffer(append_buffer *a_buf) { free(a_buf->buffer); }

append_buffer *enlarge(append_buffer *a_buf) {
    char *new = calloc(sizeof(char), a_buf->size * 2);
    memcpy(new, a_buf->buffer, a_buf->len);
    free_append_buffer(a_buf);
    a_buf->buffer = new;
    return a_buf;
}

append_buffer *concat(append_buffer *a_buf, char *string, uint16_t len) {
    if (a_buf->len + len >= a_buf->size) {
        a_buf = enlarge(a_buf);
    }
    memcpy(a_buf->buffer + a_buf->len, string, len);
    a_buf->len += len;
    return a_buf;
}

append_buffer *flush(append_buffer *a_buf) {
    if (a_buf->len == 0) {
        return a_buf;
    }
    write(STDOUT_FILENO, "\x1b[?25l", 6); // Hide the cursor
    write(STDOUT_FILENO, a_buf->buffer, a_buf->len);
    write(STDOUT_FILENO, "\x1b[?25h", 6); // Show the cursor
    a_buf->len = 0;
    return a_buf;
}

/** Editor **/

bool init(void) {
    state.x = 0;
    state.y = 0;
    state.a_buf.buffer = calloc(sizeof(char), INITIAL_BUFFER_SIZE);
    state.a_buf.size = INITIAL_BUFFER_SIZE;
    state.a_buf.len = 0;
    return get_window_size(&state.rows, &state.cols);
}

void move_cursor(char key) {
    switch (key) {
    case 'h':
        state.x--;
        return;
    case 'j':
        state.y++;
        return;
    case 'k':
        state.y--;
        return;
    case 'l':
        state.x++;
        return;
    }
}

char read_key(void) {
    char c;
    while (read(STDIN_FILENO, &c, 1) != 1) {
    }
    if (c != '\x1b') {
        return c;
    }
    char seq[2];
}

after_keypress process_keypress(void) {
    char c = read_key();

    switch (c) {
    case 'h':
    case 'j':
    case 'k':
    case 'l':
        move_cursor(c);
        return CONTINUE;

    case CTRL_KEY('q'):
        return EXIT;
    case CTRL_KEY('r'):
        return RELOAD;
    }

    return CONTINUE;
}

void clear_screen(append_buffer *a_buf) { flush(concat(a_buf, "\x1b[2J", 4)); }

string_t padding_string(uint8_t len) {
    string_t new;
    char spaces[256];
    memset(spaces, 32, len);
    spaces[len] = '\0';
    new.string = spaces;
    new.len = len;
    return new;
}

string_t welcome_message() {
    string_t new;
    char welcome[256];
    int len = snprintf(welcome, sizeof(welcome),
                       "Meta-quine editor -- version %s", VERSION);
    new.string = welcome;
    new.len = len;

    uint8_t padding = (state.cols - len) / 2;
    string_t result = padding_string(padding);
    memcpy(result.string + result.len, new.string, new.len);
    result.len += new.len;

    return result;
}

void draw_rows(append_buffer *a_buf) {
    concat(&state.a_buf, "\x1b[H", 3);
    for (int i = 0; i < state.rows; i++) {
        concat(&state.a_buf, "\x1b[K", 3); // Clear the line before drawing

        concat(&state.a_buf, "+", 1);
        if (i == state.rows / 3) {
            string_t welcome = welcome_message();
            concat(a_buf, welcome.string, welcome.len);
        }
        if (i < state.rows - 1) {
            concat(&state.a_buf, "\r\n", 2);
        }
    }
    flush(a_buf);
}

string_t create_cursor_command(uint8_t x, uint8_t y) {
    string_t command;
    char buf[32];
    command.len = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (int)state.y + 1,
                           (int)state.x + 1);
    command.string = buf;
    return command;
}

void refresh_screen(append_buffer *a_buf) {
    draw_rows(a_buf);
    string_t cursor = create_cursor_command(state.x, state.y);
    concat(a_buf, cursor.string, cursor.len);
    flush(a_buf);
}

void cleanup(void) {
    refresh_screen(&state.a_buf);
    clear_screen(&state.a_buf);
    // Have to do this just for now
    state.x = 0;
    state.y = 0;
    string_t reset_cursor = create_cursor_command(0, 0);
    flush(concat(&state.a_buf, reset_cursor.string, reset_cursor.len));
    disable_raw_mode();
}

int main(void) {
    if (!init()) {
        return 0;
    };
    enable_raw_mode();

    char c;
    while (true) {
        refresh_screen(&state.a_buf);
        switch (process_keypress()) {
        case CONTINUE:
            break;
        case RELOAD:
            cleanup();
            return 1;
        case EXIT:
            cleanup();
            return 0;
        }
    }

    cleanup();
    return 0;
}
