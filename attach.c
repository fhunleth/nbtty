/*
    dtach - A simple program that emulates the detach feature of screen.
    Copyright (C) 2004-2016 Ned T. Crigler

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include "nbtty.h"

#include <err.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#ifndef VDISABLE
#ifdef _POSIX_VDISABLE
#define VDISABLE _POSIX_VDISABLE
#else
#define VDISABLE 0377
#endif
#endif

/*
** The original terminal settings. On exit, we restore this.
*/
static struct termios orig_term[MAX_TTYS];

static int tty_in[MAX_TTYS];
static int tty_out[MAX_TTYS];
static int num_ttys = 0;
static const char *tty_paths[MAX_TTYS];

static int terminal_active = 1;

/* Ignore the return code of write. This works around a compiler warning */
static ssize_t write_buffer(int fd, const unsigned char *buffer, size_t len)
{
    if (terminal_active)
        return write(fd, buffer, len);
    else
        return (ssize_t) len;
}

/* Write to all output TTYs */
static void write_to_all_ttys(const unsigned char *buffer, size_t len)
{
    for (int i = 0; i < num_ttys; i++) {
        write_buffer(tty_out[i], buffer, len);
    }
}

static ssize_t write_string(int fd, const char *str)
{
    return write_buffer(fd, (const unsigned char *) str, strlen(str));
}

/* Write string to all output TTYs */
static void write_string_to_all(const char *str)
{
    for (int i = 0; i < num_ttys; i++) {
        write_string(tty_out[i], str);
    }
}


/* Restores the original terminal settings. */
static void restore_term(void)
{
    for (int i = 0; i < num_ttys; i++) {
        tcsetattr(tty_in[i], TCSADRAIN, &orig_term[i]);
        /* Make cursor visible. Assumes VT100. */
        write_string(tty_out[i], "\033[?25h");
    }
}

/* Signal */
static void die(int sig)
{
    (void) sig;
    write_string_to_all(EOS "\r\n[nbtty: terminating via signal]\r\n");
    exit(EXIT_FAILURE);
}

static void open_tty(int tty_index, const char *ttypath)
{
    // If already open, then close the handle.
    if (tty_in[tty_index] != -1 && tty_in[tty_index] != STDIN_FILENO)
        close(tty_in[tty_index]);

    if (ttypath == NULL || strcmp(ttypath, "-") == 0) {
        // Use stdin/stdout
        tty_in[tty_index] = STDIN_FILENO;
        tty_out[tty_index] = STDOUT_FILENO;
    } else {
        // Open the tty or retry until it works
        for (;;) {
            int fd = open(ttypath, O_RDWR | O_CLOEXEC);
            if (fd >= 0) {
                tty_in[tty_index] = fd;
                tty_out[tty_index] = fd;
                break;
            }

            // Try again in a second?
            sleep(1);
        }
    }

    /* Save the original terminal settings. */
    if (tcgetattr(tty_in[tty_index], &orig_term[tty_index]) < 0)
        errx(EXIT_FAILURE, "Attaching to a session requires a terminal.");

    /* Set raw mode. */
    struct termios new_term = orig_term[tty_index];
    new_term.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    new_term.c_iflag &= ~(IXON | IXOFF);
    new_term.c_oflag &= ~(OPOST);
    new_term.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    new_term.c_cflag &= ~(CSIZE | PARENB);
    new_term.c_cflag |= CS8;
    new_term.c_cc[VLNEXT] = VDISABLE;
    new_term.c_cc[VMIN] = 1;
    new_term.c_cc[VTIME] = 0;
    tcsetattr(tty_in[tty_index], TCSADRAIN, &new_term);
}

int attach_main(int s, const char **ttypaths, int n_ttys, int wait_input)
{
    terminal_active = !wait_input;
    
    /* Initialize tty arrays */
    for (int i = 0; i < MAX_TTYS; i++) {
        tty_in[i] = -1;
        tty_out[i] = -1;
        tty_paths[i] = NULL;
    }
    
    /* If no ttys specified, use stdin/stdout */
    if (n_ttys == 0) {
        num_ttys = 1;
        tty_paths[0] = NULL;
        open_tty(0, NULL);
    } else {
        /* Validate that stdin/stdout is not used when multiple TTYs are specified */
        if (n_ttys > 1) {
            for (int i = 0; i < n_ttys; i++) {
                if (ttypaths[i] == NULL || strcmp(ttypaths[i], "-") == 0)
                    errx(EXIT_FAILURE, "Cannot use stdin/stdout when multiple TTYs are specified. Use explicit tty paths.");
            }
        }
        
        num_ttys = n_ttys;
        for (int i = 0; i < num_ttys; i++) {
            tty_paths[i] = ttypaths[i];
            open_tty(i, ttypaths[i]);
        }
    }

    /* Set some signals. */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGXFSZ, SIG_IGN);
    signal(SIGHUP, die);
    signal(SIGTERM, die);
    signal(SIGINT, die);
    signal(SIGQUIT, die);

    /* Set a trap to restore the terminal when we die. */
    atexit(restore_term);

    /* Wait for things to happen */
    for (;;) {
        unsigned char buf[BUFSIZE];
        fd_set readfds;
        FD_ZERO(&readfds);
        
        /* Add all TTY input fds to the read set */
        int highest_fd = s;
        for (int i = 0; i < num_ttys; i++) {
            FD_SET(tty_in[i], &readfds);
            if (tty_in[i] > highest_fd)
                highest_fd = tty_in[i];
        }
        FD_SET(s, &readfds);
        
        int rc = select(highest_fd + 1, &readfds, NULL, NULL, NULL);
        if (rc < 0) {
            if (errno != EINTR) {
                write_string_to_all(EOS "\r\n[nbtty: select failed]\r\n");
                exit(EXIT_FAILURE);
            }
            continue;
        }

        /* Pty activity */
        if (FD_ISSET(s, &readfds)) {
            ssize_t len = read(s, buf, sizeof(buf));

            if (len == 0) {
                write_string_to_all(EOS "\r\n[nbtty: terminating]\r\n");
                exit(EXIT_SUCCESS);
            } else if (len < 0) {
                write_string_to_all(EOS "\r\n[nbtty: read returned an error]\r\n");
                exit(EXIT_FAILURE);
            }
            /* Send the data to all terminals. */
            write_to_all_ttys(buf, (size_t) len);
        }

        /* User activity on any TTY */
        for (int i = 0; i < num_ttys; i++) {
            if (FD_ISSET(tty_in[i], &readfds)) {
                ssize_t len = read(tty_in[i], buf, sizeof(buf));
                if (len <= 0) {
                    if (tty_in[i] == STDIN_FILENO)
                        exit(EXIT_FAILURE);
                    open_tty(i, tty_paths[i]);
                    continue;
                }

                if (terminal_active) {
                    write_buffer(s, buf, (size_t) len);
                } else if (memchr(buf, '\r', (size_t) len)) {
                    /* Activate the terminal on carriage return and forward the input.
                    ** This ensures user input (including the carriage return) is sent
                    ** to the child process when the terminal becomes active. */
                    terminal_active = 1;
                    write_string_to_all(EOS "\r\n");
                    write_buffer(s, buf, (size_t) len);
                }
            }
        }
    }
}
