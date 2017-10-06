# nbtty

`nbtty` runs a command with a pty that discards output from the command rather
than blocking.  This is useful for commands that log output and for whatever
reason output is redirected to a location that can't keep up.  Rather than
slowing or stopping the program, it can just keep going none the wiser.

This is a substantially trimmed down fork of
[dtach](https://github.com/crigler/dtach). A huge thanks goes out to Ned T.
Crigler for this useful tool.

## Usage

```sh
nbtty <command> [args...]
```

## License

Since `nbtty` derives from `dtach`, much of it is Copyright © 2004-2016 Ned T.
Crigler. Like `dtach`, `nbtty` is distributed under the General Public License.
See [COPYING](COPYING) for details.

