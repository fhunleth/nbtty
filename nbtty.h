/*
    dtach - A simple program that emulates the detach feature of screen.
    Copyright (C) 2001, 2004-2016 Ned T. Crigler

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
#ifndef nbtty_h
#define nbtty_h

#include <config.h>

/*
** The master sends a simple stream of text to the attaching clients, without
** any protocol. This might change back to the packet based protocol in the
** future. In the meantime, however, we minimize the amount of data sent back
** and forth between the client and the master. BUFSIZE is the size of the
** buffer used for the text stream.
*/
#define BUFSIZE 4096

/* This hopefully moves to the bottom of the screen */
#define EOS "\033[999H"

int attach_main(int s, const char *ttypath, int wait_input);
int master_main(char **argv, int s);

#endif
