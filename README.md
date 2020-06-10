# A GDBSTUB for RISC-V

Background
==========

The term "gdbstub" refers generically to an artifact that mediates
between GDB and a process or hardware CPU.

Typically GDB and gdbstub are in separate processes (possibly even on
separate computers) connected via TCP, PTY, RS-232, or other
communication medium.  They interact using GDB's _Remote Serial
Protocol_ (RSP), which is a certain coding of ASCII text.  RSP
documentation can be found
[here](https://sourceware.org/gdb/onlinedocs/gdb/Remote-Protocol.html)

gdbstub, in turn, controls the DUT (process or hardware CPU).

A typical command flow is:

- GDB gets a command (user input at GDB prompt, or from a GDB script)
- GDB sends an RSP message (ASCII text) to gdbstub
- gdbstub 'performs' the action on the DUT
- gdbstub sends back an RSP response message (ASCII text) to GDB
- GDB consumes the response (e.g., displays something at its console)

For RISC-V, gdbstub controls the DUT via a RISC-V _Debug Module_.
Their interface is "DMI" (Debug Module Interface), a simple
memory-like read/write interface. The spec for DMI and the Debug
Module can be found
[here](https://riscv.org/specifications/debug-specification/)

This code
=========

This code implements the major components of gdbstub for RISC-V.  It
is structured as a "front end" (`gdbstub_fe`) and a "back end"
(`gdbstub_be`).  The front end handles RSP communication with GDB,
parsing ASCII commands from GDB and formatting ASCII responses for
GDB.  When `gdbstub_fe` has parsed a command from GDB (such as, "read
GPR 13"), it invokes a corresponding function in the `gdbstub_be`,
which performs the corresponding DMI interactions to execute that
command; `gdbstub_fe` then responds to GDB.

To use this gdbstub in your environment, you need to:

- Provide a `main` (see example main.c here) that sets up the
    communication mechanism to talk to GDB (e.g., TCP socket, PTY,
    RS-232, ...), and passes it in to `gdbstub_fe` as a standard file
    descriptor (`gdbstub_fe` uses standard `read` and `write` and
    `poll` calls on this).

- Provide an implementation of `dmi_read` and `dmi_write` (in RVDM.c)
    that talks to the Debug Module in the DUT.

----------------------------------------------------------------
History
=======

This code was written by Rishiyur Nikhil based on an original version
of gdbstub at Bluespec, 'gdbstub.cc' written by Todd Snyder and/or
Darius Rad circa 2014.

Compared to that version, this version:

- Completely separates GDB-side communication from HW-side communication,
    into a front-end and back-end.

- Abstracts out the choice of communication transport for talking with GDB.
    Here, it is passed in as an argument, a file descriptor, 'gdb_fd'
    which could be a PTY, TCP socket or something else.

- Adds a lot of defensive error-checking on the communications
    with GDB, in case of flaky transports or bugs.

- Suports both RV64 and RV32 (original only supported RV64)
    (this manifests itself in the ASCII formatting of
     GDB RSP messages for register reads and writes)

- Fixes a number of bugs in the original.

- Adds some capabilities such as direct ELF-file load
