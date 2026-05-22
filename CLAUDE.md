# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

radioclkd2 is a Linux/FreeBSD daemon that decodes time signals from radio clock receivers (DCF77, MSF, WWVB) and feeds them to ntpd via the SHM (shared memory) driver. It reads pulse data from serial port modem control lines or GPIO pins, decodes the time protocol, and writes timestamps to a shared memory segment that ntpd's SHM refclock driver consumes.

## Build Commands

```bash
./configure       # autoconf-based configuration
make              # builds radioclkd2 binary in project root
make install      # installs to /usr/local/sbin/radioclkd2
```

There is no test suite.

## Architecture

The signal processing pipeline flows: **serial I/O → pulse classification → protocol decode → SHM write**.

- **`main.c`** — CLI parsing, daemon setup, forks child processes for additional serial devices. Supports up to 16 clock units.
- **`serial.c`** — Reads pulse transitions from serial modem lines (DCD/CTS/DSR/RNG) or GPIO pins. Four timing modes with different accuracy: `POLL` (±2ms), `IWAIT` (Linux TIOCMIWAIT, ±1ms), `TIMEPPS` (FreeBSD, ±0.2ms), `GPIO` (Linux sysfs poll).
- **`clock.c`** — Core pulse processor. Accumulates 120 samples (2 minutes), classifies pulse lengths, maintains a 60-second rolling PPS offset average, and dispatches to the appropriate decoder.
- **`decode_dcf77.c`**, **`decode_msf.c`**, **`decode_wwvb.c`** — Protocol-specific BCD/binary decoders that validate parity/checksums and produce a `struct tm`.
- **`shm.c`** — Writes decoded timestamps to ntpd's SHM segment (key `0x4e545030 + unit`), using memory barriers for cross-process atomicity.
- **`logger.c`** — Logs to stderr and syslog at configurable verbosity (NOTE/INFO/DEBUG/TRACE).

## Key Design Details

- **Concurrency**: fork-per-device model. Parent handles the first serial port, children handle the rest. No threads.
- **Real-time**: Uses `SCHED_FIFO` and `mlockall()` when available; falls back to `nice(-20)`.
- **Time representation**: `time_f` is a `double` carrying seconds with microsecond precision (`timef.h`). Macros convert between `timeval`/`timespec`/`time_f`.
- **Clock types**: `dcf77` (default), `msf`, `wwvb`, `gps` (DCF77-encoded GPS data), `timecode` (hard-wired local time in DCF77 format).
- **Platform conditionals**: `autoconf.h` / `config.h` gate TIOCMIWAIT, timepps, scheduler, and memory-locking support.

## Running

```bash
radioclkd2 /dev/ttyS0                          # basic DCF77 on DCD line
radioclkd2 -s gpio -d /sys/class/gpio/gpio0/value:-DCD  # GPIO mode
radioclkd2 -s iwait -t msf -n 1 -v /dev/ttyS0  # MSF, SHM unit 1, verbose
```

Device format: `tty[:[-]line[:fudgeoffs]]` where line is `dcd|cts|dsr|rng`, `-` prefix inverts polarity, and fudgeoffs is a time correction in seconds.
