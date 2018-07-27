# nbtty

`nbtty` runs a command with a pty that discards output from the command rather
than blocking.  This is useful for commands that log output and for whatever
reason output is redirected to a location that can't keep up.  Rather than
slowing or stopping the program, it can just keep going none the wiser.

This program also polls the terminal for its dimensions using ANSI escape
sequences. This is needed to pass on the size over a serial console since
SIGWINCH doesn't work.

The whole point of this program to make the Elixir IEx console in Nerves
projects work well. The blocking issue became especially problematic when the
IEx console was redirected over a USB gadget serial port.

This is a substantially trimmed down and modified fork of
[dtach](https://github.com/crigler/dtach). A huge thanks goes out to Ned T.
Crigler for this useful tool. Ned - if you're reading this and don't recognize
your code, sorry, I kind of butchered it.

## Usage

```sh
nbtty [--tty <tty path>] <command> [args...]
```

If you specify `--tty`, `nbtty` will use that tty instead of stdin/stdout. It
will additionally retry opening the tty if it doesn't exist on start. This is
useful for getting around the problem where Elixir code initializes a tty that
provides the main console.

## Building

You need some build tools for this project. In Ubuntu, you can install them
with the following:

```sh
sudo apt install build-essential automake autoconf
```

To build:

```sh
./autogen.sh
./configure
make
```

At the end of the commands, you will have `nbtty` in the project root directory.

## License

Since `nbtty` derives from `dtach`, much of it is Copyright © 2004-2016 Ned T.
Crigler.  Like `dtach`, `nbtty` is distributed under the General Public License.
See [COPYING](COPYING) for details.
