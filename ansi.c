#include "dtach.h"
#include "ansi.h"

/*
 * This escape sequence does the following:
 * 1. Saves the cursor position
 * 2. Put the cursor at 999,999
 * 3. Get where the cursor ended up
 * 4. Put the cursor back
 *
 * The remote terminal should send back:
 * ESC"[%u;%uR" with the number of rows and columns.
 */
#define ESC "\033"
static const char window_size_sequence[] = ESC"7" ESC"[r" ESC"[999;999H" ESC"[6n" ESC"8";

struct ansi_parser
{
    unsigned char buffer[64];
    int index;
    int state;

    int row;
    int col;

    struct winsize ws;
};
static struct ansi_parser ansi_parser;

struct ansi_parse_transition
{
    const char *chars;
    int next_state;
    void (*handler)(unsigned char in, unsigned char **out);
};

struct ansi_parse_state
{
    struct ansi_parse_transition transitions[3];
};

static void ansi_handle_start(unsigned char in, unsigned char **out)
{
    (void) out;
    ansi_parser.row = 0;
    ansi_parser.col = 0;
    ansi_parser.buffer[0] = in;
    ansi_parser.index = 1;
}

static void ansi_handle_capture(unsigned char in, unsigned char **out)
{
    (void) out;
    ansi_parser.buffer[ansi_parser.index++] = in;
}

static void ansi_handle_row(unsigned char in, unsigned char **out)
{
    ansi_parser.row = ansi_parser.row * 10 + ansi_parser.buffer[ansi_parser.index] - '0';
    ansi_handle_capture(in, out);
}

static void ansi_handle_col(unsigned char in, unsigned char **out)
{
    ansi_parser.col = ansi_parser.col * 10 + ansi_parser.buffer[ansi_parser.index] - '0';
    ansi_handle_capture(in, out);
}

static void ansi_handle_rc(unsigned char in, unsigned char **out)
{
    (void) in;
    (void) out;

    // set to row, col
    ansi_parser.ws.ws_row = ansi_parser.row;
    ansi_parser.ws.ws_col = ansi_parser.col;
    ansi_parser.ws.ws_xpixel = 0;
    ansi_parser.ws.ws_ypixel = 0;

    // Discard ANSI code
    ansi_parser.index = 0;
}

static void ansi_handle_mismatch(unsigned char in, unsigned char **out)
{
    if (ansi_parser.index > 0) {
        memcpy(*out, ansi_parser.buffer, ansi_parser.index);
        *out += ansi_parser.index;
        ansi_parser.index = 0;
    }
    **out = in;
    *out += 1;
}

/*
 * This is a little DFA for parsing the one ANSI response that
 * we're interested in. It is also discarded so that it doesn't
 * mess up Erlang's ANSI parser which doesn't seem to handle it.
 *
 * Note that this DFA buffers characters due to the requirement
 * to toss the ANSI response of interest. Since buffering input
 * could really confuse a user, it passes it on as soon as there
 * is no chance of a match.
 */
#define CATCH_ALL {NULL, 0, ansi_handle_mismatch}
static struct ansi_parse_state ansi_states[] = {
 /* 0 - Start */ {{{ESC, 1, ansi_handle_start}, CATCH_ALL}},
 /* 1 - Esc   */ {{{"[", 2, ansi_handle_capture}, CATCH_ALL}},
 /* 2 - [     */ {{{"0123456789", 3, ansi_handle_row}, CATCH_ALL}},
 /* 3 - n1    */ {{{"0123456789", 4, ansi_handle_row}, {";", 6, ansi_handle_capture}, CATCH_ALL}},
 /* 4 - n2    */ {{{"0123456789", 5, ansi_handle_row}, {";", 6, ansi_handle_capture}, CATCH_ALL}},
 /* 5 - n3    */ {{{";", 6, ansi_handle_capture}, CATCH_ALL}},
 /* 6 - ;     */ {{{"0123456789", 7, ansi_handle_col}, CATCH_ALL}},
 /* 7 - m1    */ {{{"0123456789", 8, ansi_handle_col}, {"R", 0, ansi_handle_rc}, CATCH_ALL}},
 /* 8 - m2    */ {{{"0123456789", 9, ansi_handle_col}, {"R", 0, ansi_handle_rc}, CATCH_ALL}},
 /* 9 - m3    */ {{{"R", 0, ansi_handle_rc}, CATCH_ALL}},
};

/**
 * Scan the input looking for window size updates. This function
 * caches input temporarily if it looks like a relevent escape
 * sequence, so the amount that should be output won't always be
 * the same as what comes in.
 *
 * If the window size changes, this will return something non-zero.
 */
int ansi_process_input(const unsigned char *input, size_t input_size,
                       unsigned char *output, size_t *output_size,
                       struct winsize *ws)
{
    unsigned char *out = output;
    for (size_t i = 0; i < input_size; i++) {
        struct ansi_parse_transition *transition = ansi_states[ansi_parser.state].transitions;
        unsigned char in = input[i];
        ansi_parser.buffer[ansi_parser.index] = input[i];
        for (;;) {
            if (transition->chars == NULL || strchr(transition->chars, input[i])) {
                // match
                transition->handler(in, &out);
                ansi_parser.state = transition->next_state;
                break;
            } else {
                // no match -> try the next one.
                transition++;
            }
        }
    }
    *output_size = out - output;

    if (ansi_parser.ws.ws_col != 0 &&
        ansi_parser.ws.ws_col != ws->ws_col &&
        ansi_parser.ws.ws_row != ws->ws_row) {
        *ws = ansi_parser.ws;
        return 1;
    } else {
        return 0;
    }
}

void ansi_reset_parser()
{
    memset(&ansi_parser, 0, sizeof(ansi_parser));
}

/**
 * Send the ANSI sequence to get the window size.
 *
 * @param fd where to send it
 * @return -1 and errno if it failed
 */
int ansi_size_request(int fd)
{
    return write(fd, window_size_sequence, sizeof(window_size_sequence) - 1);
}
