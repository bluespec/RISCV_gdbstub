// ================================================================
// This is a very small "smoke test" that starts the gdb stub
// listening on a socket for GDB to attach, so so we can check the GDB
// connection.

// Once connected, there's not much more one can do, since we're not
// connected to a real process/CPU, and so cannot respond meaningfully
// to requests from GDB.

// ================================================================
// From C lib

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

// ----------------
// From project

#include "gdbstub.h"

// ================================================================

int main (int argc, char *argv [])
{
    FILE *logfile = fopen ("log_gdbstub.txt", "w");
    assert (logfile != NULL);
    fprintf (stdout, "%s: opened log_gdbstub.txt\n", __FUNCTION__);

    unsigned short port = 31000;
    int retval = gdbstub_start_tcp (logfile, port);
    if (retval < 0) {
	fprintf (stdout, "ERROR: %s:%s: gdbstub_start_tcp failed\n",
		 __FILE__, __FUNCTION__);
	return 1;
    }
    fprintf (stdout, "%s: gdbstub_start_tcp returned %0d\n", __FUNCTION__, retval);

    // Wait for thread to exit
    fprintf (stdout, "%s: waiting in gdbstub_join\n", __FUNCTION__);
    // gdbstub_stop ();
    gdbstub_join ();
}
