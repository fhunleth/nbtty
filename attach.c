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
static struct termios orig_term;

static int tty_in = STDIN_FILENO;
static int tty_out = STDOUT_FILENO;

static ssize_t write_string(int fd, const char *str)
{
    return write(fd, str, strlen(str));
}

/* Restores the original terminal settings. */
static void restore_term(void)
{
    tcsetattr(tty_in, TCSADRAIN, &orig_term);

    /* Make cursor visible. Assumes VT100. */
    write_string(tty_out, "\033[?25h");
}

/* Signal */
static RETSIGTYPE die(int sig)
{
    (void) sig;
    write_string(tty_out, EOS "\r\n[got signal - dying]\r\n");
    exit(EXIT_FAILURE);
}

static void open_tty(const char *ttypath)
{
    // If already open, the close the handle.
    if (tty_in != STDIN_FILENO)
        close(tty_in);

    if (ttypath == NULL || strcmp(ttypath, "-") == 0) {
        // Check if we're using stdin
        tty_in = STDIN_FILENO;
        tty_out = STDOUT_FILENO;
    } else {
        // Open the tty or retry until it works
        for (;;) {
            int fd = open(ttypath, O_RDWR | O_CLOEXEC);
            if (fd >= 0) {
                tty_in = fd;
                tty_out = fd;
                break;
            }

            // Try again in a second?
            sleep(1);
        }
    }

    /* Save the original terminal settings. */
    if (tcgetattr(tty_in, &orig_term) < 0)
        errx(EXIT_FAILURE, "Attaching to a session requires a terminal.");

    /* Set raw mode. */
    struct termios new_term = orig_term;
    new_term.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    new_term.c_iflag &= ~(IXON | IXOFF);
    new_term.c_oflag &= ~(OPOST);
    new_term.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    new_term.c_cflag &= ~(CSIZE | PARENB);
    new_term.c_cflag |= CS8;
    new_term.c_cc[VLNEXT] = VDISABLE;
    new_term.c_cc[VMIN] = 1;
    new_term.c_cc[VTIME] = 0;
    tcsetattr(tty_in, TCSADRAIN, &new_term);
}

int attach_main(int s, const char *ttypath)
{
    /* Set some signals. */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGXFSZ, SIG_IGN);
    signal(SIGHUP, die);
    signal(SIGTERM, die);
    signal(SIGINT, die);
    signal(SIGQUIT, die);

    open_tty(ttypath);

    /* Set a trap to restore the terminal when we die. */
    atexit(restore_term);

    /* Wait for things to happen */
    for (;;) {
        unsigned char buf[BUFSIZE];
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(tty_in, &readfds);
        FD_SET(s, &readfds);
        int highest_fd = tty_in > s ? tty_in : s;
        int rc = select(highest_fd + 1, &readfds, NULL, NULL, NULL);
        if (rc < 0) {
            if (errno != EINTR) {
                write_string(tty_out, EOS "\r\n[select failed]\r\n");
                exit(EXIT_FAILURE);
            }
            continue;
        }

        /* Pty activity */
        if (FD_ISSET(s, &readfds)) {
            ssize_t len = read(s, buf, sizeof(buf));

            if (len == 0) {
                write_string(tty_out, EOS "\r\n[EOF - nbtty terminating]\r\n");
                exit(EXIT_SUCCESS);
            } else if (len < 0) {
                write_string(tty_out, EOS "\r\n[read returned an error]\r\n");
                exit(EXIT_FAILURE);
            }
            /* Send the data to the terminal. */
            write(tty_out, buf, (size_t) len);
        }

        /* User activity */
        if (FD_ISSET(tty_in, &readfds)) {
            ssize_t len = read(tty_in, buf, sizeof(buf));
            if (len <= 0) {
                if (tty_in == STDIN_FILENO)
                    exit(EXIT_FAILURE);
                open_tty(ttypath);
                continue;
            }

            write(s, buf, (size_t) len);
        }
    }
}
