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
#include "ansi.h"

//#define REPORT_BYTES_DROPPED

/* The pty struct - The pty information is stored here. */
struct pty
{
	/* File descriptor of the pty */
	int fd;
	/* Process id of the child. */
	pid_t pid;
	/* The terminal parameters of the pty. Old and new for comparision
	** purposes. */
	struct termios term;
	/* The current window size of the pty. */
	struct winsize ws;
};

/* The connected client. */
static int client_fd = -1;
/* The pseudo-terminal created for the child process. */
static struct pty the_pty;

#ifdef REPORT_BYTES_DROPPED
int bytes_dropped = 0;
#endif

#ifndef HAVE_FORKPTY
pid_t forkpty(int *amaster, char *name, struct termios *termp,
	struct winsize *winp);
#endif


/* Signal */
static RETSIGTYPE 
die(int sig)
{
	/* Well, the child died. */
	if (sig == SIGCHLD)
	{
		return;
	}
	exit(1);
}

/* Sets a file descriptor to non-blocking mode. */
static int
setnonblocking(int fd)
{
	int flags;

#if defined(O_NONBLOCK)
	flags = fcntl(fd, F_GETFL);
	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
		return -1;
	return 0;
#elif defined(FIONBIO)
	flags = 1;
	if (ioctl(fd, FIONBIO, &flags) < 0)
		return -1;
	return 0;
#else
#warning Do not know how to set non-blocking mode.
	return 0;
#endif
}

/* Initialize the pty structure. */
static int
init_pty(char **argv, int statusfd)
{
	/* Use the original terminal's settings. We don't have to set the
	** window size here, because the attacher will send it in a packet. */
	the_pty.term = orig_term;
	memset(&the_pty.ws, 0, sizeof(struct winsize));

	/* Create the pty process */
    the_pty.pid = forkpty(&the_pty.fd, NULL, &the_pty.term, NULL);
	if (the_pty.pid < 0)
		return -1;
	else if (the_pty.pid == 0)
	{
		/* Child.. Execute the program. */
		execvp(*argv, argv);

		/* Report the error to statusfd if we can, or stdout if we
		** can't. */
		if (statusfd != -1)
			dup2(statusfd, 1);
		else
			printf(EOS "\r\n");

		printf("%s: could not execute %s: %s\r\n", progname,
		       *argv, strerror(errno));
		fflush(stdout);
		_exit(127);
	}
	/* Parent.. Finish up and return */
	return 0;
}

/* Process activity on the pty - Input and terminal changes are sent out to
** the attached clients. If the pty goes away, we die. */
static void
pty_activity()
{
	unsigned char buf[BUFSIZE];
	ssize_t len;

	/* Read the pty activity */
	len = read(the_pty.fd, buf, sizeof(buf));

	/* Error -> die */
	if (len <= 0)
		exit(1);

	/* Get the current terminal settings. */
	if (tcgetattr(the_pty.fd, &the_pty.term) < 0)
		exit(1);

    ssize_t written = 0;
    int retries = 0;
    for (;;)
    {
#ifdef REPORT_BYTES_DROPPED
        {
            static int last_bytes_dropped = 0;
            if (last_bytes_dropped != bytes_dropped) {
                char str[64];
                sprintf(str, "[%d dropped]", bytes_dropped);
                if (write(client_fd, str, strlen(str)) > 0)
                    last_bytes_dropped = bytes_dropped;
            }
        }
#endif
        ssize_t n = write(client_fd, buf + written, len - written);
        if (n > 0) {
            if (n == len && len < 80 && buf[len - 1] == '\n') {
                // Everything wrote in one go and there was a
                // new line, so check window size.
                // TODO: Figure out a better heuristic. Perhaps check for the width no more than x times per minute, etc.
                ansi_size_request(client_fd);
            }

            written += n;
            if (written == len)
                break;

            retries++;
            if (retries > 1)
                break;
        }
        else if (n < 0 && errno == EINTR)
            continue;
        else
            break;
    }

#ifdef REPORT_BYTES_DROPPED
    bytes_dropped += (len - written);
#endif
}

/* Process activity from a client. */
static void
client_activity()
{
	ssize_t len;
	struct packet pkt;

	/* Read the activity. */
    len = read(client_fd, &pkt, sizeof(struct packet));
	if (len < 0 && (errno == EAGAIN || errno == EINTR))
		return;

	/* Close the client on an error. */
	if (len <= 0)
	{
        close(client_fd);
        client_fd = -1;
		return;
	} 

	/* Push out data to the program. */
	if (pkt.type == MSG_PUSH)
	{
        if (pkt.len <= sizeof(pkt.u.buf)) {
            unsigned char output[sizeof(pkt.u.buf) + ANSI_MAX_RESPONSE_LEN];
            size_t output_size;
            if (ansi_process_input(pkt.u.buf, pkt.len, output, &output_size, &the_pty.ws))
                ioctl(the_pty.fd, TIOCSWINSZ, &the_pty.ws);
            if (output_size > 0)
                write(the_pty.fd, output, output_size);
        }
	}
}

/* The master process - It watches over the pty process and the attached */
/* clients. */
static void
master_process(char **argv, int statusfd)
{

	/* Okay, disassociate ourselves from the original terminal, as we
	** don't care what happens to it. */
	setsid();

	/* Create a pty in which the process is running. */
	signal(SIGCHLD, die);
	if (init_pty(argv, statusfd) < 0)
	{
		if (statusfd != -1)
			dup2(statusfd, 1);
		if (errno == ENOENT)
			printf("%s: Could not find a pty.\n", progname);
		else
			printf("%s: init_pty: %s\n", progname, strerror(errno));
		exit(1);
	}

	/* Set up some signals. */
	signal(SIGPIPE, SIG_IGN);
	signal(SIGXFSZ, SIG_IGN);
	signal(SIGHUP, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGINT, die);
	signal(SIGTERM, die);

	/* Close statusfd, since we don't need it anymore. */
	if (statusfd != -1)
		close(statusfd);

	/* Make sure stdin/stdout/stderr point to /dev/null. We are now a
	** daemon. */
    int nullfd = open("/dev/null", O_RDWR);
	dup2(nullfd, 0);
	dup2(nullfd, 1);
	dup2(nullfd, 2);
	if (nullfd > 2)
		close(nullfd);

	/* Loop forever. */
	while (1)
	{
		/* Re-initialize the file descriptor set for select. */
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(client_fd, &readfds);
        FD_SET(the_pty.fd, &readfds);
        int highest_fd = client_fd > the_pty.fd ? client_fd : the_pty.fd;

		/* Wait for something to happen. */
		if (select(highest_fd + 1, &readfds, NULL, NULL, NULL) < 0)
		{
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

int
master_main(char **argv, int s)
{
	int fd[2] = {-1, -1};
	pid_t pid;

    ansi_reset_parser();

#if defined(F_SETFD) && defined(FD_CLOEXEC)
	fcntl(s, F_SETFD, FD_CLOEXEC);

    if (pipe(fd) >= 0)
	{
		if (fcntl(fd[0], F_SETFD, FD_CLOEXEC) < 0 ||
		    fcntl(fd[1], F_SETFD, FD_CLOEXEC) < 0)
		{
			close(fd[0]);
			close(fd[1]);
			fd[0] = fd[1] = -1;
		}
	}
#endif
    setnonblocking(s);
    client_fd = s;

	/* Fork off so we can daemonize and such */
	pid = fork();
	if (pid < 0)
	{
		printf("%s: fork: %s\n", progname, strerror(errno));
		return 1;
	}
	else if (pid == 0)
	{
		/* Child - this becomes the master */
		if (fd[0] != -1)
			close(fd[0]);
        master_process(argv, fd[1]);
		return 0;
	}
	/* Parent - just return. */

#if defined(F_SETFD) && defined(FD_CLOEXEC)
	/* Check if an error occurred while trying to execute the program. */
	if (fd[0] != -1)
	{
		char buf[1024];
		ssize_t len;

		close(fd[1]);
		len = read(fd[0], buf, sizeof(buf));
		if (len > 0)
		{
			write(2, buf, len);
			kill(pid, SIGTERM);
			return 1;
		}
		close(fd[0]);
	}
#endif
	close(s);
	return 0;
}
