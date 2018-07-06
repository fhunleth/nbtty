# Changelog

## v0.4.0

* New features
  * Support `--tty` option to use a specified tty device. This will be retried
    if it doesn't exist at start. Needed for configfs configuration of the
    gadget USB interface.

* Bug fixes
  * Exit on SIGCHLD from the launched process. It appears to be unreliable to
    detect exit based on tty EOF.

## v0.3.2

* Fix missing include with Musl

## v0.3.1

* Reduce window size polling by triggering it on input from the user rather than
  output from Elixir. This reduces the bytes coming out quite a bit and the
  chance that one is dropped and the ANSI escape sequence gets corrupted.

## v0.3.0

* More code simplifications

## v0.2.0

* New features
  * Added ANSI window size polling

* Bug fixes
  * Simplified tty handling code - seems to work better

## v0.1.0

* Initial release
