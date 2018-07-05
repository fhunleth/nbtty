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
#include "ansi.h"

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <sys/select.h>

//#define REPORT_BYTES_DROPPED

/* The pty struct - The pty information is stored here. */
struct pty {
    /* File descriptor of the pty */
    int fd;
    /* Process id of the child. */
    pid_t pid;
    /* The current window size of the pty. */
    struct winsize ws;
};

/* The connected client. */
static int client_fd = -1;

/* The pseudo-terminal created for the child process. */
static struct pty the_pty;
static uint32_t next_poll_time = 0;

/* This gets set to true when it's time to poll the window size again */
static int poll_window_size = 0;

#ifdef REPORT_BYTES_DROPPED
int bytes_dropped = 0;
#endif

static uint32_t now()
{
    static uint32_t counter = 0;
    struct timespec tp;
    if (clock_gettime(CLOCK_MONOTONIC, &tp) < 0)
        return counter++;
    return (uint32_t) tp.tv_sec;
}

/* Signal */
static RETSIGTYPE die(int sig)
{
    /* Well, the child died. */
    if (sig == SIGCHLD)
        return;

    exit(EXIT_FAILURE);
}

/* Initialize the pty structure. */
static int init_pty(char **argv)
{
    memset(&the_pty.ws, 0, sizeof(struct winsize));

    /* Create the pty process */
    the_pty.pid = forkpty(&the_pty.fd, NULL, &orig_term, NULL);
    if (the_pty.pid < 0)
        return -1;
    else if (the_pty.pid == 0) {
        /* Child.. Execute the program. */
        execvp(*argv, argv);

        printf(EOS "Could not execute %s: %s\r\n",
               *argv, strerror(errno));
        fflush(stdout);
        _exit(127);
    }
    /* Parent.. Finish up and return */
    return 0;
}

/* Process activity on the pty - Input and terminal changes are sent out to
** the attached clients. If the pty goes away, we die. */
static void pty_activity()
{
    unsigned char buf[BUFSIZE];
    ssize_t len;

    size_t bytes_to_read = sizeof(buf);
    if (poll_window_size)
        bytes_to_read -= ANSI_MAX_RESPONSE_LEN;

    /* Read the pty activity */
    len = read(the_pty.fd, buf, bytes_to_read);

    /* Error -> die */
    if (len <= 0)
        exit(EXIT_FAILURE);

    /* If we need to poll the window size, tack the request onto the buffer */
    if (poll_window_size) {
        len += ansi_size_request(&buf[len]);
        poll_window_size = 0;
    }

    ssize_t written = 0;
    int retries = 0;
    for (;;) {
#ifdef REPORT_BYTES_DROPPED
        {
            static int last_bytes_dropped = 0;
            if (last_bytes_dropped != bytes_dropped)
            {
                char str[64];
                sprintf(str, "[%d dropped]", bytes_dropped);
                if (write(client_fd, str, strlen(str)) > 0)
                    last_bytes_dropped = bytes_dropped;
            }
        }
#endif
        ssize_t n = write(client_fd, buf + written, len - written);
        if (n > 0) {
            written += n;
            if (written == len)
                break;

            retries++;
            if (retries > 1)
                break;
        } else if (n < 0 && errno == EINTR)
            continue;
        else
            break;
    }

#ifdef REPORT_BYTES_DROPPED
    bytes_dropped += (len - written);
#endif
}

/* Process activity from a client. */
static void client_activity()
{
    unsigned char buf[BUFSIZE];

    /* Read the activity. */
    ssize_t len = read(client_fd, buf, sizeof(buf) - ANSI_MAX_RESPONSE_LEN);
    if (len < 0 && (errno == EAGAIN || errno == EINTR))
        return;

    /* Close the client on an error. */
    if (len <= 0) {
        close(client_fd);
        client_fd = -1;
        return;
    }

    /* Check if we should poll the window size */
    if (memchr(buf, '\r', len) != NULL) {
        uint32_t current_seconds = now();

        /* poll the window size at most every 5 seconds */
        if (next_poll_time - current_seconds > 5) {
            poll_window_size = 1;
            next_poll_time = current_seconds + 5;
        }
    }

    /* Push out data to the program. */
    unsigned char output[BUFSIZE];
    size_t output_size;
    if (ansi_process_input(buf, len, output, &output_size, &the_pty.ws))
        ioctl(the_pty.fd, TIOCSWINSZ, &the_pty.ws);
    if (output_size > 0)
        write(the_pty.fd, output, output_size);
}

/* The master process - It watches over the pty process and the attached */
/* clients. */
static void master_process(char **argv)
{
    /* Okay, disassociate ourselves from the original terminal, as we
    ** don't care what happens to it. */
    setsid();

    /* Create a pty in which the process is running. */
    signal(SIGCHLD, die);
    if (init_pty(argv) < 0) {
        if (errno == ENOENT)
            errx(EXIT_FAILURE, "Could not find a pty.");
        else
            err(EXIT_FAILURE, "init_pty");
    }

    /* Set up some signals. */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGXFSZ, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGINT, die);
    signal(SIGTERM, die);

    /* Make sure stdin/stdout/stderr point to /dev/null. We are now a
    ** daemon. */
    int nullfd = open("/dev/null", O_RDWR);
    dup2(nullfd, 0);
    dup2(nullfd, 1);
    dup2(nullfd, 2);
    if (nullfd > 2)
        close(nullfd);

    /* Loop forever. */
    while (1) {
        /* Re-initialize the file descriptor set for select. */
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(client_fd, &readfds);
        FD_SET(the_pty.fd, &readfds);
        int highest_fd = client_fd > the_pty.fd ? client_fd : the_pty.fd;

        /* Wait for something to happen. */
        if (select(highest_fd + 1, &readfds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            exit(1);
        }

        /* Activity on a client? */
        if (FD_ISSET(client_fd, &readfds))
            client_activity();
        /* pty activity? */
        if (FD_ISSET(the_pty.fd, &readfds))
            pty_activity();
    }
}

int master_main(char **argv, int s)
{
    ansi_reset_parser();

    if (fcntl(s, F_SETFD, FD_CLOEXEC) < 0)
        err(EXIT_FAILURE, "fcnt(F_SETFD, FD_CLOEXEC)");

    int flags = fcntl(s, F_GETFL);
    if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0)
        err(EXIT_FAILURE, "fcnt(F_SETFL, 0x%x | O_NONBLOCK)", flags);

    client_fd = s;

    /* Fork off so we can daemonize and such */
    pid_t pid = fork();
    if (pid < 0) {
        err(EXIT_FAILURE, "fork");
    } else if (pid == 0) {
        /* Child - this becomes the master */
        master_process(argv);
        return 0;
    }
    /* Parent - just return. */
    return 0;
}
