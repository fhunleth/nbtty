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
#include "dtach.h"

#ifndef VDISABLE
#ifdef _POSIX_VDISABLE
#define VDISABLE _POSIX_VDISABLE
#else
#define VDISABLE 0377
#endif
#endif

/*
** The current terminal settings. After coming back from a suspend, we
** restore this.
*/
static struct termios cur_term;

/* Restores the original terminal settings. */
static void restore_term(void)
{
    tcsetattr(0, TCSADRAIN, &orig_term);

    /* Make cursor visible. Assumes VT100. */
    printf("\033[?25h");
    fflush(stdout);
}

/* Signal */
static RETSIGTYPE die(int sig)
{
    /* Print a nice pretty message for some things. */
    if (sig == SIGHUP || sig == SIGINT)
        printf(EOS "\r\n[detached]\r\n");
    else
        printf(EOS "\r\n[got signal %d - dying]\r\n", sig);
    exit(EXIT_FAILURE);
}

int attach_main(int s)
{
    unsigned char buf[BUFSIZE];

    /* The current terminal settings are equal to the original terminal
    ** settings at this point. */
    cur_term = orig_term;

    /* Set a trap to restore the terminal when we die. */
    atexit(restore_term);

    /* Set some signals. */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGXFSZ, SIG_IGN);
    signal(SIGHUP, die);
    signal(SIGTERM, die);
    signal(SIGINT, die);
    signal(SIGQUIT, die);

    /* Set raw mode. */
    cur_term.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    cur_term.c_iflag &= ~(IXON | IXOFF);
    cur_term.c_oflag &= ~(OPOST);
    cur_term.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    cur_term.c_cflag &= ~(CSIZE | PARENB);
    cur_term.c_cflag |= CS8;
    cur_term.c_cc[VLNEXT] = VDISABLE;
    cur_term.c_cc[VMIN] = 1;
    cur_term.c_cc[VTIME] = 0;
    tcsetattr(0, TCSADRAIN, &cur_term);

    /* Clear the screen. This assumes VT100. */
    write(STDIN_FILENO, "\33[H\33[J", 6);

    /* Wait for things to happen */
    for (;;) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(s, &readfds);
        int n = select(s + 1, &readfds, NULL, NULL, NULL);
        if (n < 0 && errno != EINTR && errno != EAGAIN) {
            printf(EOS "\r\n[select failed]\r\n");
            exit(EXIT_FAILURE);
        }

        /* Pty activity */
        if (n > 0 && FD_ISSET(s, &readfds)) {
            ssize_t len = read(s, buf, sizeof(buf));

            if (len == 0) {
                printf(EOS "\r\n[EOF - nbtty terminating]"
                       "\r\n");
                exit(EXIT_SUCCESS);
            } else if (len < 0) {
                printf(EOS "\r\n[read returned an error]\r\n");
                exit(EXIT_FAILURE);
            }
            /* Send the data to the terminal. */
            write(STDOUT_FILENO, buf, len);
            n--;
        }
        /* stdin activity */
        if (n > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            ssize_t len = read(STDIN_FILENO, buf, sizeof(buf));
            if (len <= 0)
                exit(EXIT_FAILURE);

            write(s, buf, len);
            n--;
        }
    }
    return 0;
}
