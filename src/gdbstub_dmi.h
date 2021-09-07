// Copyright (c) 2016-2020 Bluespec, Inc. All Rights Reserved
// Author: Rishiyur Nikhil

// ================================================================

#pragma once

// ================================================================
// DMI interface (gdbstub invokes these functions)
// These should be filled in with the appropriate mechanisms that
// perform the actual DMI read/write on the RISC-V Debug module.

extern void      dmi_write (uint16_t addr, uint32_t data);
extern uint32_t  dmi_read  (uint16_t addr);

// ================================================================
