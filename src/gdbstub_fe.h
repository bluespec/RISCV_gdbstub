// Copyright (c) 2016-2020 Bluespec, Inc. All Rights Reserved
// Author: Rishiyur Nikhil
//
// ================================================================

#pragma once

// ****************************************************************
// ****************************************************************
// FRONT END API (called from the platform-specific top-level code)
// ****************************************************************
// ****************************************************************

// ================================================================
// Parameters passed into main_gdbstub ()

typedef struct {
    // For debugging gdbstub and debugging interactions
    FILE *logfile;

    // File descriptor for read/write of RSP messages from/to GDB
    int   gdb_fd;

    // Optional file descriptor for stopping GDB server; stops when
    // a byte can be read. Use -1 to disable.
    int   stop_fd;

    // Whether to automatically close logfile and stop_fd. gdb_fd is
    // always closed.
    bool  autoclose_logfile_stop_fd;

} Gdbstub_FE_Params;

// ================================================================
// Main loop. This is just called once.
// The void *result and void *arg allow this to be passed into
// pthread_create() so it can be run as a separate thread.
// 'arg' should be a pointer to a Gdbstub_FE_Params struct.

// i.e., caller should have already established communication with GDB
// using params.gdb_fd, and should have already opened a logfile for
// writing using params.logfile (NULL => no logging).

extern
void *main_gdbstub (void *arg);
//    Gdbstub_FE_Params *params = (Gdbstub_FE_Params *) arg;

// ================================================================
