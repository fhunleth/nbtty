# Changelog

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
