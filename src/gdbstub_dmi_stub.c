// Copyright (c) 2016-2020 Bluespec, Inc. All Rights Reserved
// Author: Rishiyur Nikhil

// ================================================================
// Stub implementation of DMI read/write functions

// ================================================================
// C lib includes

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// ----------------
// Project includes

#include  "RVDM.h"

// ================================================================
// DMI interface (gdbstub invokes these functions)
// These should be filled in with the appropriate mechanisms that
// perform the actual DMI read/write on the RISC-V Debug module.

void dmi_write (FILE *logfile_fp, uint16_t addr, uint32_t data)
{
    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "dmi_write (addr %0x, data %0x)    NOT YET IMPLEMENTED\n",
		 addr, data);
    }
}

uint32_t  dmi_read  (FILE *logfile_fp, uint16_t addr)
{
    uint32_t data = 0;
    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "dmi_read  (addr %0x) => %0x    NOT YET IMPLEMENTED\n", addr, data);
    }
    return 0;
}
