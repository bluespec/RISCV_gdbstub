// Copyright (c) 2020-2023 Bluespec, Inc. All Rights Reserved
// Author: Rishiyur Nikhil
//
// ================================================================

#pragma once

// ****************************************************************
// ****************************************************************
// HIGH LEVEL API (called from the platform-specific top-level code)
// ****************************************************************
// ****************************************************************

extern
uint8_t gdbstub_be_xlen;

// ================================================================
// Spawn a new thread for main_gdbstub with a pipe set up for later
// stopping it.

extern
void gdbstub_start_fd (FILE *logfile, int gdb_fd);

// ================================================================
// Spawn a new thread listening for sockets which are then used to
// call main_gdbstub, with a pipe set up for later stopping it.
// Returns the port bound to (useful if the provided port is 0), or
// -1 on error

extern
int gdbstub_start_tcp (FILE *logfile, unsigned short port);

// ================================================================
// Stop the gdbstub thread.

extern
void gdbstub_stop (void);

// ================================================================
// Wait for the gdbstub thread to exit.

extern
void gdbstub_join (void);

// ================================================================
