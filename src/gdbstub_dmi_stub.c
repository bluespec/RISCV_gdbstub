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

void dmi_write (uint16_t addr, uint32_t data)
{
    fprintf (stderr, "RVDM.c: dmi_write(): Not yet implemented\n");
}

uint32_t  dmi_read  (uint16_t addr)
{
    fprintf (stderr, "RVDM.c: dmi_read(): Not yet implemented\n");
    return 0;
}
