// Copyright (c) 2019-2020 Bluespec, Inc. All Rights Reserved
// Author: Rishiyur Nikhil

// ================================================================
// This is a gdbstub 'back-end', to be paired with a gdbstub 'front-end'

// The 'front-end' interacts with a remote GDB process using the ASCII
// RSP (Remote Serial Protocol), parsing commands from GDB.
// It calls functions in this 'back-end' to perform actions.

// This 'back-end' interacts with a hardware Debug Module in a RISC-V
// implementation, using the DMI read/write interface.

// ================================================================
// C lib includes

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>

// ----------------
// Local includes

#include "RVDM.h"
#include "gdbstub_be.h"
#include "gdbstub_dmi.h"
#include "Elf_read.h"

// ****************************************************************
// ****************************************************************
// ****************************************************************
// Private definitions (until next ***** section)

// ================================================================
// Private globals

#define min(x,y)  (((x)<(y)) ? (x) : (y))

// In mem_read/mem_write: logging of data bytes transferred
//     verbosity =  0: no logging of data
//     verbosity =  1: log up to first 64 bytes
//     verbosity >= 2: log all bytes

static int verbosity = 1;

static bool initialized = false;

static FILE *logfile_fp = NULL;

// ================================================================
// Run-mode

// Continue/pause control
typedef enum { PAUSED, PAUSE_REQUESTED, STEP, CONTINUE } Run_Mode;

static Run_Mode run_mode = PAUSED;

// Timeout related variable. The -1 for CPU_TIMEOUT implies, the
// timeout check is disabled
static uint32_t numHaltChecks = 0;
static uint32_t CPU_TIMEOUT = (~ ((uint32_t) 0));

// ================================================================
// Poll dmstatus until ((dmstatus & mask) == value)
// Return status, and dmstatus value.

static
uint32_t poll_dmstatus (char      *dbg_string,
			uint32_t   mask,
			uint32_t   value,
			uint32_t  *p_dmstatus)
{
    uint32_t usecs = 0;

    while (true) {
	// Timeout
	if (usecs >= 1000000) {
	    if (logfile_fp != NULL) {
	      fprintf (logfile_fp,
		       "    %s: polled dmstatus %0d usecs; mask 0x%0x, value 0x%0x; timeout\n",
		       dbg_string, usecs, mask, value);
	    }
	    return status_err;
	}
	*p_dmstatus = dmi_read (dm_addr_dmstatus);

	if ((*p_dmstatus & mask) == value) {
	    return status_ok;
	}

	if (verbosity == 2)
	    if (logfile_fp != NULL) {
		fprintf (logfile_fp,
			 "    %s: polling dmstatus: busy (%d usecs)\n",
			 dbg_string, usecs);
	    }
	usleep (1);
	usecs += 1;
    }
}

// ================================================================
// Poll abstractcs until not-busy or error.
// Return status, and abstractcs value.

static
uint32_t poll_abstractcs_until_notbusy (char      *dbg_string,
					uint32_t  *p_abstractcs)
{
    uint32_t usecs = 0;

    // Assuming abstractcs.cmderr == 0 in the HW
    while (true) {
	// Timeout condition
	if (usecs > 1000000) {
	    if (logfile_fp != NULL) {
		fprintf (logfile_fp,
			 "    %s: polling abstractcs: busy for > 1 sec\n",
			 dbg_string);
		fprintf (logfile_fp,
			 "    timeout\n");
	    }
	    return status_err;
	}

	*p_abstractcs = dmi_read (dm_addr_abstractcs);

	if (! fn_abstractcs_busy (*p_abstractcs)) {
	    return status_ok;
	}

	if (verbosity == 2)
	    if (logfile_fp != NULL) {
		fprintf (logfile_fp,
			 "    %s: polling abstractcs: busy (%d usecs)\n",
			 dbg_string, usecs);
	    }
	usleep (1);
	usecs += 1;
    }
}

// ================================================================
// Check if abstractcs.cmderr is non-zero, and reset it to zero if necessary.

static
uint8_t check_abstractcs_error (char* dbg_string, uint32_t abstractcs)
{
    // Check abstractcs if any error
    uint32_t abstractcs_no_err = abstractcs;
    uint8_t cmderr = fn_abstractcs_cmderr (abstractcs);
    if (cmderr != 0) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp, "    %s", dbg_string);
	    fprint_abstractcs_cmderr (logfile_fp, ": abstractcs.cmderr: ", cmderr, "\n");
	}

	fprint_abstractcs_cmderr (stdout, "ERROR: abstractcs.cmderr: ", cmderr, "\n");

	// Clear cmderr, for future accesses
	// DM_ABSTRACTCS_CMDERR_OTHER = 3'b111 is used to clear the field (Write-1-clear)
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "    %s : clear abstractcs cmderr\n", dbg_string);
	}
	abstractcs_no_err = fn_mk_abstractcs (DM_ABSTRACTCS_CMDERR_OTHER);
	dmi_write (dm_addr_abstractcs, abstractcs_no_err);
    }
    return cmderr;
}

// ================================================================
// For System Bus access commands, wait until non-busy

#define SB_TIMEOUT_USECS  1000000

static
uint32_t  gdbstub_be_wait_for_sb_nonbusy (uint32_t  *p_sbcs)
{
    uint32_t sbcs;
    bool     sbbusy;
    uint32_t usecs = 0;
    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "gdbstub_be_wait_for_sb_nonbusy\n");
    }
    while (true) {
	sbcs    = dmi_read (dm_addr_sbcs);
	sbbusy  = fn_sbcs_sbbusy (sbcs);
	if (! sbbusy) break;

	if (usecs > SB_TIMEOUT_USECS) {
	    if (logfile_fp != NULL) {
		fprintf (logfile_fp,
			 "gdbstub_be_wait_for_sb_nonbusy: TIMEOUT (> %0d usecs)\n", usecs);
	    }
	    return status_err;
	}
	usleep (1);
	usecs += 1;
    }
    if (usecs > 100)
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "INFO: gdbstub_be_wait_for_sb_nonbusy: %0d polls (extend usleep time?)\n",
		     usecs);
	}

    if (p_sbcs != NULL) *p_sbcs = sbcs;
    return status_ok;
}

// ================================================================
// gdbstub_be_reg_read is shared by the functions for reading GPR/CSR/FPR
// dm_regnum for CSR x is:    x
//           for GPR x is:    x + 0x1000
//           for FPR x is:    x + 0x1020

static
uint32_t gdbstub_be_reg_read (const uint8_t xlen, uint16_t dm_regnum, uint64_t *p_regval, uint8_t *p_cmderr)
{
    // Assuming abstractcs.cmderr == 0 in the HW
    uint32_t abstractcs;
    uint64_t data0 = 0;
    uint64_t data1 = 0;

    // Send command to do a register read
    if (verbosity == 2)
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "    gdbstub_be_reg_read (0x%0x): read command\n",
		     dm_regnum);
	    fflush (logfile_fp);
	}

    uint32_t command = fn_mk_command_access_reg (((xlen == 32)
						  ? DM_COMMAND_ACCESS_REG_SIZE_LOWER32
						  : DM_COMMAND_ACCESS_REG_SIZE_LOWER64),    // aarsize
						 false,    // aarpostincrement
						 false,    // postexec
						 true,     // transfer
						 false,    // write
						 dm_regnum);
    dmi_write (dm_addr_command, command);

    // Poll abstractcs until not busy
    poll_abstractcs_until_notbusy ("gdbstub_be_reg_read", & abstractcs);

    *p_cmderr = check_abstractcs_error ("gdbstub_be_reg_read", abstractcs);

    if (*p_cmderr == 0) {
	// Read data0 register
	data0 = dmi_read (dm_addr_data0);
	if (xlen == 64) {
	    // Read upper 32 bits from data1
	    data1 = dmi_read (dm_addr_data1);
	    data1 = data1 << 32;
	}
	*p_regval = data1 | data0;
	if (verbosity == 2)
	    if (logfile_fp != NULL) {
		fprintf (logfile_fp,
			 "    gdbstub_be_reg_read (0x%0x) => 0x%0" PRIx64 "\n",
			 dm_regnum, *p_regval);
		fflush (logfile_fp);
	    }
	return  status_ok;
    }
    else {
	return  status_err;
    }
}

// ================================================================
// gdbstub_be_reg_write is shared by the functions for writing GPR/CSR/FPR
// dm_regnum for CSR x is:    x
//           for GPR x is:    x + 0x1000
//           for FPR x is:    x + 0x1020

static
uint32_t  gdbstub_be_reg_write (const uint8_t xlen, uint16_t dm_regnum, uint64_t regval, uint8_t *p_cmderr)
{
    if (verbosity == 2)
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "    gdbstub_be_reg_write (0x%0x, 0x%0" PRIx64 ")\n",
		     dm_regnum, regval);
	    fflush (logfile_fp);
	}

    // Assuming abstractcs.cmderr == 0
    uint32_t abstractcs;

    // Write regval to dm_data0 register
    dmi_write (dm_addr_data0, (uint32_t) regval);

    if (xlen == 64) {
	// Write upper bits of regval to dm_data1 register
	dmi_write (dm_addr_data1, (uint32_t) (regval >> 32));
    }

    // Send command to do a register write
    uint32_t command = fn_mk_command_access_reg (((xlen == 32)
						  ? DM_COMMAND_ACCESS_REG_SIZE_LOWER32
						  : DM_COMMAND_ACCESS_REG_SIZE_LOWER64),    // aarsize
						 false,    // postincrement
						 false,    // postexec
						 true,     // transfer
						 true,     // write
						 dm_regnum);
    dmi_write (dm_addr_command, command);

    // Poll abstractcs until not busy
    poll_abstractcs_until_notbusy ("gdbstub_be_reg_write", & abstractcs);

    *p_cmderr = check_abstractcs_error ("gdbstub_be_reg_write", abstractcs);

    if (*p_cmderr == 0) {
	return status_ok;
    }
    else {
	return status_ok;
    }
}

// ================================================================
// Local function to read one 32b word from RISC-V memory
// Used in gdbstub_be_mem_write for read-modify-writes at unaligned edges of addr range.

static
uint32_t  gdbstub_be_mem32_read (const char *context,
				 const uint8_t xlen, const uint64_t addr, uint32_t *p_data)
{
    uint32_t addr0  = (uint32_t) addr;
    uint32_t addr1  = (uint32_t) (addr >> 32);
    uint32_t status = 0;

    // Assert that the address is aligned
    if ((addr & 0x3) != 0) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "ERROR: %s.gdbstub_be_mem32_read (addr 0x%0" PRIx64 ") is not 4-byte aligned\n",
		     context, addr);
	}
	exit (1);
    }

    // Write 'read' command to sbcs (we don't care about autoincrement and readondata)
    // We switch on readonaddr so that when we write the addr, next, it starts the read
    status = gdbstub_be_wait_for_sb_nonbusy (NULL);
    if (status == status_err) return status;
    uint32_t sbcs = fn_mk_sbcs (true,                      // sbbusyerr (W1C)
				true,                      // sbreadonaddr
				DM_SBACCESS_32_BIT,        // sbaccess (size)
				true,                      // sbautoincrement
				true,                      // sbreadondata
				DM_SBERROR_UNDEF7_W1C);    // Clear sberror
    if (logfile_fp != NULL) {
	fprint_sbcs (logfile_fp, "    Write ", sbcs, "\n");
    }
    dmi_write (dm_addr_sbcs, sbcs);

    // Write the address to sbaddress1/0
    if (xlen == 64) {
	// Write upper 64b of address to sbaddress1
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp, "    Write to sbaddress1: 0x%08x\n", addr1);
	}
	dmi_write (dm_addr_sbaddress1, addr1);
    }
    // Write lower 64b of the address to sbaddress0 (which will start a bus read)
    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "    Write to sbaddress0: 0x%08x\n", addr0);
    }
    dmi_write (dm_addr_sbaddress0, addr0);

    // Read sbdata0
    status = gdbstub_be_wait_for_sb_nonbusy (NULL);
    if (status == status_err) return status;
    uint32_t data = dmi_read (dm_addr_sbdata0);

    /* debug */
    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "%s.gdbstub_be_mem32_read  (addr 0x%0" PRIx64 ") => 0x%0" PRIx32 "\n",
		 context, addr, data);
    }
    
    *p_data = data;
    return status_ok;
}

// ================================================================
// Local function to write one 32b word to RISC-V system memory
// Used in gdbstub_be_mem_write for read-modify-writes at unaligned edges of addr range.

static
uint32_t  gdbstub_be_mem32_write (const char *context,
				  const uint8_t xlen, const uint64_t addr, const uint32_t data)
{
    uint32_t addr0  = (uint32_t) addr;
    uint32_t addr1  = (uint32_t) (addr >> 32);
    uint32_t status = 0;

    // Assert that the address is aligned
    if ((addr & 0x3) != 0) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "ERROR: %s.gdbstub_be_mem32_write (addr 0x%0" PRIx64 ", data 0x%0x) is not 4-byte aligned\n",
		     context, addr, data);
	}
	exit (1);
    }

    // Write 'read' command to sbcs (we don't care about autoincrement and readondata)
    status = gdbstub_be_wait_for_sb_nonbusy (NULL);
    if (status == status_err) return status;
    uint32_t sbcs = fn_mk_sbcs (true,                      // sbbusyerr (W1C)
				false,                     // sbreadonaddr
				DM_SBACCESS_32_BIT,        // sbaccess (size)
				false,                     // sbautoincrement
				false,                     // sbreadondata
				DM_SBERROR_UNDEF7_W1C);    // Clear sberror
    dmi_write (dm_addr_sbcs, sbcs);

    // Write the address to sbaddress1/0
    status = gdbstub_be_wait_for_sb_nonbusy (NULL);
    if (status == status_err) return status;
    if (xlen == 64) {
	// Write upper 64b of address to sbaddress1
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp, "    Write to sbaddress1: 0x%08x\n", addr1);
	}
	dmi_write (dm_addr_sbaddress1, addr1);
    }
    // Write lower 64b of the address to sbaddress0 (which will start a bus read)
    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "    Write to sbaddress0: 0x%08x\n", addr0);
    }
    dmi_write (dm_addr_sbaddress0, addr0);

    // Write data to sbdata0 (which writes through to mem)
    status = gdbstub_be_wait_for_sb_nonbusy (NULL);
    if (status == status_err) return status;
    dmi_write (dm_addr_sbdata0, data);

    /* debug */
    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "%s.gdbstub_be_mem32_write (addr 0x%0" PRIx64 ") <= 0x%0" PRIx32 "\n",
		 context, addr, data);
    }
    
    return status_ok;
}

// ================================================================
// Log memory data to logfile.
// Amount of data written depends on verbosity.

static
void fprint_mem_data (FILE *fp, const int verbosity, const char *data, const size_t len)
{
    fprintf (fp, "    Data (hex):\n");
    if (verbosity == 0) {
	fprintf (fp, "    (verbosity 0: not logging data)\n");
	return;
    }

    size_t jmax = ((verbosity == 1) ? min (64, len) : len);
    for (size_t j = 0; j < jmax; j++) {
	if ((j & 0xF) == 0) fprintf (fp, "   ");
	if ((j & 0x3) == 0) fprintf (fp, " ");

	fprintf (fp, " 0x%02x", data [j]);

	if (((j & 0xF) == 0xF) || (j == (jmax - 1)))
	    fprintf (fp, "\n");
    }
    if ((verbosity == 1) && (jmax < len))
	fprintf (fp, "    (verbosity 1: logging only first 64 bytes)\n");
    fflush (fp);
}

// ****************************************************************
// ****************************************************************
// ****************************************************************
// Public definitions

// ================================================================
// Word bitwidth (32 for RV32, 64 for RV64)
// (Important: affects format of data strings in RSP communication with GDB)
// This defaults to 64 (for RV64), but can be set to 32.
// If gdbstub_be_elf_load() is invoked, it'll be picked up from the ELF file.

uint8_t gdbstub_be_xlen = 64;

// ================================================================
// Help
// Return a help string for GDB to print out,
// listing which 'monitor' commands are available

const char *gdbstub_be_help (void)
{
    const char *help_msg =
	"monitor help                       Print this help message\n"
	"monitor verbosity n                Set verbosity of HW simulation to n\n"
	"monitor xlen n                     Set XLEN to n (32 or 64 only)\n"
	"monitor reset_dm                   Perform Debug Module DM_RESET\n"
	"monitor reset_ndm                  Perform Debug Module NDM_RESET\n"
	"monitor reset_hart                 Perform Debug Module HART_RESET\n"
	"elf_load filename                  Load ELF file into RISC-V memory\n"
	;

    fprintf (logfile_fp, "gdbstub_be_help ()\n");

    return help_msg;
}

// ================================================================
// Initialize gdbstub_be

uint32_t  gdbstub_be_init (FILE *logfile)
{
    // Fill in whatever is needed to initialize

    logfile_fp  = logfile;
    initialized = true;

    return status_ok;
}

// ================================================================
// Final actions for gdbstub_be

uint32_t  gdbstub_be_final (const uint8_t xlen)
{
    // Fill in whatever is needed as final actions

    if (logfile_fp != NULL)
	fclose (logfile_fp);

    return status_ok;
}

// ================================================================
// Reset the Debug Module

uint32_t  gdbstub_be_dm_reset (const uint8_t xlen)
{
    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "gdbstub_be_dm_reset\n");
	fflush (logfile_fp);
    }

    // Reset the debug module (dm) itself
    uint32_t dmcontrol = fn_mk_dmcontrol (false,          // haltreq
					  false,          // resumereq
					  false,          // hartreset
					  false,          // ackhavereset
					  false,          // hasel
					  0,              // hartsello
					  0,              // hartselhi
					  false,          // setresethaltreq
					  false,          // clrresethaltreq
					  false,          // ndmreset
					  false);         // dmactive_N
    if (logfile_fp != NULL) {
	fprint_dmcontrol (logfile_fp,
			  "gdbstub_be_dm_reset: write ", dmcontrol, "\n");
	fflush (logfile_fp);
    }
    dmi_write (dm_addr_dmcontrol, dmcontrol);

    // Poll abstractcs until not busy, check for errors
    uint32_t abstractcs;
    poll_abstractcs_until_notbusy ("gdbstub_be_dm_reset", & abstractcs);
    check_abstractcs_error ("gdbstub_be_dm_reset", abstractcs);

    // Readback dmstatus
    uint32_t dmstatus = dmi_read (dm_addr_dmstatus);
    if (logfile_fp != NULL) {
	fprint_dmstatus (logfile_fp, "  dmstatus = {", dmstatus, "}\n");
	fflush (logfile_fp);
    }

    // Report Debug Module version
    uint8_t  version  = (dmstatus & 0xF);
    if (version == 0) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "    gdbstub_be_startup: no debug module present\n");
	    fflush (logfile_fp);
	}
	return  status_err;
    }
    else if (version == 1) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "    gdbstub_be_startup: debug module version is 0.11; not supported\n");
	    fflush (logfile_fp);
	}
	return  status_err;
    }
    else if (version == 2) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "    gdbstub_be_startup: debug module version is 0.13\n");
	    fflush (logfile_fp);
	}
    }
    else {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "    gdbstub_be_startup: unknown debug module version: %0d\n",
		     version);
	    fflush (logfile_fp);
	}
	return  status_err;
    }

    return status_ok;
}

// ================================================================
// Reset the NDM (non-debug module, i.e., everything but the debug module)

uint32_t  gdbstub_be_ndm_reset (const uint8_t xlen, bool haltreq)
{
    if (! initialized) return status_ok;

    uint32_t dmcontrol;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "gdbstub_be_ndm_reset (haltreq = %0d): pulse dmcontrol.ndmreset\n",
		 haltreq);
	fflush (logfile_fp);
    }

    // Assert dmcontrol.ndmreset
    dmcontrol = fn_mk_dmcontrol (haltreq,
				 false,          // resumereq
				 false,          // hartreset
				 false,          // ackhavereset
				 false,          // hasel
				 0,              // hartsello
				 0,              // hartselhi
				 false,          // setresethaltreq
				 false,          // clrresethaltreq
				 true,           // ndmreset
				 true);          // dmactive_N
    if (logfile_fp != NULL) {
	fprint_dmcontrol (logfile_fp,
			  "gdbstub_be_ndm_reset: write ", dmcontrol, "\n");
	fflush (logfile_fp);
    }
    dmi_write (dm_addr_dmcontrol, dmcontrol);

    // Deassert dmcontrol.ndmreset
    dmcontrol = fn_mk_dmcontrol (haltreq,
				 false,          // resumereq
				 false,          // hartreset
				 false,          // ackhavereset
				 false,          // hasel
				 0,              // hartsello
				 0,              // hartselhi
				 false,          // setresethaltreq
				 false,          // clrresethaltreq
				 false,          // ndmreset
				 true);          // dmactive_N
    if (logfile_fp != NULL) {
	fprint_dmcontrol (logfile_fp,
			  "gdbstub_be_ndm_reset: write ", dmcontrol, "\n");
	fflush (logfile_fp);
    }
    dmi_write (dm_addr_dmcontrol, dmcontrol);

    // Poll dmstatus until '(! anyunavail)'
    uint32_t dmstatus;
    poll_dmstatus ("gdbstub_be_ndm_reset", DMSTATUS_ANYUNAVAIL, 0, & dmstatus);

    if (logfile_fp != NULL) {
	fprintf (logfile_fp,"    gdbstub_be_ndm_reset: dmstatus = 0x%0x\n", dmstatus);
	fflush (logfile_fp);
    }

    return status_ok;
}

// ================================================================
// Reset the HART 

uint32_t  gdbstub_be_hart_reset (const uint8_t xlen, bool haltreq)
{
    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "gdbstub_be_hart_reset (haltreq = %0d)\n", haltreq);
	fflush (logfile_fp);
    }

    // Assuming abstractcs.cmderr == 0 in the HW

    // Reset the HART
    uint32_t dmcontrol = fn_mk_dmcontrol (haltreq,
					  false,    // resumereq
					  true,     // hartreset
					  false,    // ackhavereset
					  false,    // hasel
					  0,        // hartsello
					  0,        // hartselhi
					  false,    // setresethaltreq
					  false,    // clrresethaltreq
					  false,    // ndmreset
					  true);    // dmactive_N
    if (logfile_fp != NULL) {
	fprint_dmcontrol (logfile_fp,
			  "gdbstub_be_hart_reset: write ", dmcontrol, "\n");
	fflush (logfile_fp);
    }
    dmi_write (dm_addr_dmcontrol, dmcontrol);

    // Poll dmstatus until '(! anyhavereset)'
    uint32_t dmstatus;
    poll_dmstatus ("gdbstub_be_hart_reset", DMSTATUS_ANYHAVERESET, 0, & dmstatus);

    return status_ok;
}

// ================================================================
// Set verbosity to n in RISC-V system

uint32_t gdbstub_be_verbosity (uint32_t n)
{
    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "gdbstub_be_verbosity (%0d)\n", n);
	fflush (logfile_fp);
    }

    dmi_write (dm_addr_verbosity, n);
    return  status_ok;

    /* TODO: transition to debug_module setup
    Control_Rsp control_rsp;
    // Set verbosity of mem_controller to 2
    control_req_rsp (control_req_op_set_verbosity,
		     n,
		     0,
		     & control_rsp);
    if (control_rsp.status == status_err)
       return -1;
    else {
       return 0;
    }
    */
}

// ================================================================
// Load ELF file into RISC-V memory

// Normally GDB opens an ELF file, and sends memory-write commands to
// gdbstub to write it to DUT memory.

// This is an alternative mechanism, where GDB passes the ELF filename
// to gdbstub, which opens the ELF file and writes it into DUT memory.
// The mem-write command below could be done using DMA, possibly
// providing faster ELF-loading.

// Note: the ELF file specifies XLEN; we record it here in gdbstub_be_xlen

#ifdef GDBSTUB_NO_ELF_LOAD
uint32_t gdbstub_be_elf_load (const char *elf_filename)
{
    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "gdbstub_be_elf_load compiled out; returning error\n");
    }

    return status_err;
}
#else
uint32_t gdbstub_be_elf_load (const char *elf_filename)
{
    struct timespec timespec1, timespec2;

    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "gdbstub_be_elf_load\n");
    }

    Elf_Features  features;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "    Reading ELF file\n");
	fflush (logfile_fp);
    }
    int ret = elf_readfile (logfile_fp, elf_filename, & features);
    if (ret == 0) return status_err;

    gdbstub_be_xlen = features.bitwidth;
    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "    xlen %0d\n", features.bitwidth);
	fflush (logfile_fp);
    }
    fprintf (stdout,     "    xlen %0d\n", features.bitwidth);

    uint64_t  n_bytes = features.max_addr - features.min_addr + 1;
    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "    Writing 0x%0" PRIx64 " (%0" PRId64 ") bytes of ELF data to memory\n",
		 n_bytes, n_bytes);
	fflush (logfile_fp);
    }
    fprintf (stdout,
	     "    Writing 0x%0" PRIx64 " (%0" PRId64 ") bytes of ELF data to memory\n",
	     n_bytes, n_bytes);

    // Write ELF file contents to memory
    // Note: this could be done using DMA
    clock_gettime (CLOCK_REALTIME, & timespec1);
    uint32_t status = gdbstub_be_mem_write (gdbstub_be_xlen,
					    features.min_addr,
					    & (features.mem_buf [features.min_addr]),
					    n_bytes);
    clock_gettime (CLOCK_REALTIME, & timespec2);
    uint64_t time1 = ((uint64_t) timespec1.tv_sec) * 1000000000 + ((uint64_t) timespec1.tv_nsec);
    uint64_t time2 = ((uint64_t) timespec2.tv_sec) * 1000000000 + ((uint64_t) timespec2.tv_nsec);
    uint64_t time_delta = time2 - time1;
    uint64_t B_per_sec = (n_bytes * 1000000000) / time_delta;

    fprintf (stdout, "ELF-load statistics\n");
    fprintf (stdout, "Size :        %0ld bytes\n",     n_bytes);
    fprintf (stdout, "Elapsed time: %0ld nsec\n",      time_delta);
    fprintf (stdout, "Speed:        %0ld bytes/sec\n", B_per_sec);

    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "    ELF file loaded\n");
	fflush (logfile_fp);
    }
    fprintf (stdout,     "    ELF file loaded\n");
    return status;
}
#endif

// ================================================================
// Continue the HW execution at given PC

uint32_t gdbstub_be_continue (const uint8_t xlen)
{
    if (! initialized) return status_ok;

    // Read 'dcsr' register
    uint64_t dcsr64;
    uint8_t  cmderr;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "gdbstub_be_continue: read dcsr ...\n");
	fflush (logfile_fp);
    }
    uint32_t status = gdbstub_be_reg_read (xlen, csr_addr_dcsr, & dcsr64, & cmderr);
    if (status == status_err) return status_err;

    uint32_t dcsr = (uint32_t) dcsr64;
    if (logfile_fp != NULL) {
	fprint_dcsr (logfile_fp,
		     "gdbstub_be_continue: read dcsr => ", dcsr, "\n");
	fflush (logfile_fp);
    }

    // If dcsr.step bit is set, clear it
    if (fn_dcsr_step (dcsr)) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp, "gdbstub_be_continue: clear single-step bit in dcsr\n");
	    fflush (logfile_fp);
	}
	dcsr = fn_mk_dcsr (fn_dcsr_xdebugver (dcsr),
			   fn_dcsr_ebreakm (dcsr),
			   fn_dcsr_ebreaks (dcsr),
			   fn_dcsr_ebreaku (dcsr),
			   fn_dcsr_stepie (dcsr),
			   fn_dcsr_stopcount (dcsr),
			   fn_dcsr_stoptime (dcsr),
			   fn_dcsr_cause (dcsr),
			   fn_dcsr_mprven (dcsr),
			   fn_dcsr_nmip (dcsr),
			   false,                       // step
			   fn_dcsr_prv (dcsr));

	// Write back 'dcsr' register
	if (logfile_fp != NULL) {
	    fprint_dcsr (logfile_fp,
			 "gdbstub_be_continue: write reg ", dcsr, "\n");
	    fflush (logfile_fp);
	}
	status = gdbstub_be_reg_write (xlen, csr_addr_dcsr, dcsr, & cmderr);
	if (status == status_err) return status_err;
    }

    // Write 'resumereq' to dmcontrol
    uint32_t dmcontrol;
    dmcontrol = fn_mk_dmcontrol (false,    // haltreq
				 true,     // resumereq
				 false,    // hartreset
				 false,    // ackhavereset
				 false,    // hasel
				 0,        // hartsello
				 0,        // hartselhi
				 false,    // setresethaltreq
				 false,    // clrresethaltreq
				 false,    // ndmreset
				 true);    // dmactive_N
    if (logfile_fp != NULL) {
	fprint_dmcontrol (logfile_fp,
			  "gdbstub_be_continue: write ", dmcontrol, "\n");
	fflush (logfile_fp);
    }

    dmi_write (dm_addr_dmcontrol, dmcontrol);

    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "gdbstub_be_continue () => ok\n");
	fflush (logfile_fp);
    }
    numHaltChecks = 0;

    run_mode = CONTINUE;
    return  status_ok;
}

// ================================================================
// Step the HW execution at given PC
// Return 0 on success
//        n (> 0) on error, where n is an error code.

uint32_t  gdbstub_be_step (const uint8_t xlen)
{
    if (! initialized) return status_ok;

    // Read 'dcsr' register
    uint64_t dcsr64;
    uint8_t  cmderr;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "gdbstub_be_step: read dcsr ...\n");
	fflush (logfile_fp);
    }
    uint32_t status = gdbstub_be_reg_read (xlen, csr_addr_dcsr, & dcsr64, & cmderr);
    if (status == status_err) return status_err;

    uint32_t dcsr = (uint32_t) dcsr64;
    if (logfile_fp != NULL) {
	fprint_dcsr (logfile_fp,
		     "gdbstub_be_step: read dcsr => ", dcsr, "\n");
	fflush (logfile_fp);
    }

    // If dcsr.step bit is clear, set it
    if (! fn_dcsr_step (dcsr)) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "gdbstub_be_step: set single-step bit in dcsr\n");
	    fflush (logfile_fp);
	}
	dcsr = fn_mk_dcsr (fn_dcsr_xdebugver (dcsr),
			   fn_dcsr_ebreakm (dcsr),
			   fn_dcsr_ebreaks (dcsr),
			   fn_dcsr_ebreaku (dcsr),
			   fn_dcsr_stepie (dcsr),
			   fn_dcsr_stopcount (dcsr),
			   fn_dcsr_stoptime (dcsr),
			   fn_dcsr_cause (dcsr),
			   fn_dcsr_mprven (dcsr),
			   fn_dcsr_nmip (dcsr),
			   true,                       // step
			   fn_dcsr_prv (dcsr));

	// Write back 'dcsr' register
	if (logfile_fp != NULL) {
	    fprint_dcsr (logfile_fp,
			 "gdbstub_be_step: write reg ", dcsr, "\n");
	    fflush (logfile_fp);
	}
	status = gdbstub_be_reg_write (xlen, csr_addr_dcsr, dcsr, & cmderr);
	if (status == status_err) return status_err;
    }

    // Write 'resumereq' to dmcontrol
    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "gdbstub_be_step: set resumereq bit in dmcontrol\n");
	fflush (logfile_fp);
    }
    uint32_t dmcontrol = fn_mk_dmcontrol (false,    // haltreq
					  true,     // resumereq
					  false,    // hartreset
					  false,    // ackhavereset
					  false,    // hasel
					  0,        // hartsello
					  0,        // hartselhi
					  false,    // setresethaltreq
					  false,    // clrresethaltreq
					  false,    // ndmreset
					  true);    // dmactive_N
    if (logfile_fp != NULL) {
	fprint_dmcontrol (logfile_fp,
			  "gdbstub_be_step: write dmcontrol := ", dmcontrol, "\n");
	fflush (logfile_fp);
    }
    dmi_write (dm_addr_dmcontrol, dmcontrol);

    // Poll dmstatus until 'allhalted'
    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "gdbstub_be_step: polling dmstatus until 'allhalted'\n");
	fflush (logfile_fp);
    }
    uint32_t dmstatus;
    poll_dmstatus ("gdbstub_be_stop", DMSTATUS_ALLHALTED, DMSTATUS_ALLHALTED, & dmstatus);

    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "gdbstub_be_step () => ok\n");
	fflush (logfile_fp);
    }
    run_mode = PAUSED;
    return status_ok;
}

// ================================================================
// Stop the HW execution

uint32_t  gdbstub_be_stop (const uint8_t xlen)
{
    if (! initialized) return status_ok;

    // Write 'haltreq' to dmcontrol
    uint32_t dmcontrol = fn_mk_dmcontrol (true,     // haltreq
					  false,    // resumereq
					  false,    // hartreset
					  false,    // ackhavereset
					  false,    // hasel
					  0,        // hartsello
					  0,        // hartselhi
					  false,    // setresethaltreq
					  false,    // clrresethaltreq
					  false,    // ndmreset
					  true);    // dmactive_N
    if (logfile_fp != NULL) {
	fprint_dmcontrol (logfile_fp, "gdbstub_be_stop: write ", dmcontrol, "\n");
	fflush (logfile_fp);
    }
    dmi_write (dm_addr_dmcontrol, dmcontrol);

    // Poll dmstatus until 'allhalted'
    uint32_t dmstatus;
    poll_dmstatus ("gdbstub_be_stop", DMSTATUS_ALLHALTED, DMSTATUS_ALLHALTED, & dmstatus);

    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "gdbstub_be_stop () => ok\n");
	fflush (logfile_fp);
    }
    run_mode = PAUSED;
    return status_ok;
}

// ================================================================
// Get stop-reason from HW
// (HW normally stops due to GDB ^C, after a 'step', or at a breakpoint)

int32_t  gdbstub_be_get_stop_reason (const uint8_t xlen, uint8_t *p_stop_reason)
{
    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "gdbstub_be_get_stop_reason ()\n");
	fflush (logfile_fp);
    }

    // Read dmstatus
    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "    gdbstub_be_get_stop_reason (): check dmstatus.allhalted\n");
	fflush (logfile_fp);
    }
    // Poll dmstatus until 'allhalted'
    uint32_t dmstatus;
    poll_dmstatus ("gdbstub_be_get_stop_reason", DMSTATUS_ALLHALTED, DMSTATUS_ALLHALTED, & dmstatus);

    if (! (dmstatus & DMSTATUS_ALLHALTED)) {
	// Still running
        if (verbosity > 1) {
	    if (logfile_fp != NULL) {
		fprintf (logfile_fp,
			 "    gdbstub_be_get_stop_reason () => still running (%d) \n",
			 numHaltChecks);
		fflush (logfile_fp);
	    }
        }

        if (((~ CPU_TIMEOUT) != 0) && (numHaltChecks >= CPU_TIMEOUT)) {
	    if (logfile_fp != NULL) {
		fprintf (logfile_fp,
			 "ERROR: gdbstub_be_get_stop_reason () => CPU TIMEOUT \n");
		fflush (logfile_fp);
	    }
	    return -1;
        } else {
           numHaltChecks ++;
        }
	return -2;
    }
    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "    gdbstub_be_get_stop_reason (): halted\n");
	fflush (logfile_fp);
    }

    run_mode = PAUSED;

    // Read dcsr
    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "    gdbstub_be_get_stop_reason () => read dcsr.cause\n");
	fflush (logfile_fp);
    }

    uint64_t dcsr64;
    uint8_t  cmderr;

    uint32_t status = gdbstub_be_reg_read (xlen, csr_addr_dcsr, & dcsr64, & cmderr);
    if (status == status_err) return -1;

    uint32_t dcsr = (uint32_t) dcsr64;
    uint8_t cause = fn_dcsr_cause (dcsr);
    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "    gdbstub_be_get_stop_reason () => halted; dcsr.cause = %0d\n",
		 cause);
	fflush (logfile_fp);
    }

    *p_stop_reason = cause;
    return status_ok;
}

// ================================================================
// This is not a debugger function at all, just an aid for humans
// perusing the logfile.  A GDB command can result in several DMI
// commands. This function writes a separation marker into the log
// file, so that it is easy to group sets of DMIs command and
// responses corresponding to a single GDB command.

static int command_num = 0;

uint32_t  gdbstub_be_start_command (const uint8_t xlen)
{
    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "======== START_COMMAND %0d\n", command_num);
	fflush (logfile_fp);
    }
    command_num++;

    return status_ok;
}

// ================================================================
// Read a value from the PC

uint32_t  gdbstub_be_PC_read (const uint8_t xlen, uint64_t *p_PC)
{
    *p_PC = 0;
    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "gdbstub_be_PC_read (csr 0x7b1)\n");
	fflush (logfile_fp);
    }

    // Read 'dpc' in debug module, = CSR 0X7b1
    uint8_t  cmderr;
    uint32_t status = gdbstub_be_reg_read (xlen, csr_addr_dpc, p_PC, & cmderr);
    if (status == status_err) {
	if (logfile_fp != NULL) {
	    fprint_abstractcs_cmderr (logfile_fp,
				      "    ERROR: gdbstub_be_PC_read (csr 0x7b1) => ",
				      cmderr, "\n");
	    fflush (logfile_fp);
	}
    } else {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "    gdbstub_be_PC_read (csr 0x7b1) => 0x%0" PRIx64 "\n", *p_PC);
	    fflush (logfile_fp);
	}
    }

    return status;
}

// ================================================================
// Read a value from a GPR register in SoC

uint32_t  gdbstub_be_GPR_read (const uint8_t xlen, uint8_t regnum, uint64_t *p_regval)
{
    *p_regval = 0;
    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "gdbstub_be_GPR_read (gpr 0x%0x)\n", regnum);
	fflush (logfile_fp);
    }

    assert (regnum < 32);

    // Debug module encodes GPR x as 0x1000 + x
    uint8_t  cmderr;
    uint16_t hwregnum = (uint16_t) (regnum + dm_command_access_reg_regno_gpr_0);
    uint32_t status = gdbstub_be_reg_read (xlen, hwregnum, p_regval, & cmderr);

    if (status == status_err) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "    ERROR: gdbstub_be_GPR_read (gpr 0x%0x)", regnum);
	    fprint_abstractcs_cmderr (logfile_fp, " => ", cmderr, "\n");
	    fflush (logfile_fp);
	}
    }
    else
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "    gdbstub_be_GPR_read (gpr 0x%0x) => 0x%0" PRIx64 "\n",
		     regnum, *p_regval);
	    fflush (logfile_fp);
	}

    return status;
}

// ================================================================
// Read a value from a FPR register in SoC

uint32_t  gdbstub_be_FPR_read (const uint8_t xlen, uint8_t regnum, uint64_t *p_regval)
{
    *p_regval = 0;
    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "gdbstub_be_FPR_read (fpr 0x%0x)\n", regnum);
	fflush (logfile_fp);
    }

    assert (regnum < 32);

    // Debug module encodes FPR x as 0x1020 + x
    uint8_t  cmderr;
    uint16_t hwregnum = (uint16_t) (regnum + dm_command_access_reg_regno_fpr_0);
    uint32_t status = gdbstub_be_reg_read (xlen, hwregnum, p_regval, & cmderr);

    if (status == status_err) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "    ERROR: gdbstub_be_FPR_read (fpr 0x%0x)", regnum);
	    fprint_abstractcs_cmderr (logfile_fp, " => ", cmderr, "\n");
	    fflush (logfile_fp);
	}
    }
    else
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "    gdbstub_be_FPR_read (fpr 0x%0x) => 0x%0" PRIx64 "\n",
		     regnum, *p_regval);
	    fflush (logfile_fp);
	}

    return status;
}

// ================================================================
// Read a value from a CSR in SoC

uint32_t  gdbstub_be_CSR_read (const uint8_t xlen, uint16_t regnum, uint64_t *p_regval)
{
    *p_regval = 0;
    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "gdbstub_be_CSR_read (csr 0x%0x)\n", regnum);
	fflush (logfile_fp);
    }

    assert (regnum < 0xFFF);

    // Debug module encodes CSR x as x
    uint8_t  cmderr;
    uint16_t hwregnum = (uint16_t) (regnum + dm_command_access_reg_regno_csr_0);
    uint32_t status = gdbstub_be_reg_read (xlen, hwregnum, p_regval, & cmderr);

    if (status == status_err) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "    ERROR: gdbstub_be_CSR_read (csr 0x%0x)",
		     regnum);
	    fprint_abstractcs_cmderr (logfile_fp, " => ", cmderr, "\n");
	    fflush (logfile_fp);
	}
    }
    else
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "    gdbstub_be_CSR_read (csr 0x%0x) => 0x%0" PRIx64 "\n",
		     regnum, *p_regval);
	    fflush (logfile_fp);
	}

    return status;
}

// ================================================================
// Read one byte from SoC memory at address 'addr' into 'data'

uint32_t  gdbstub_be_mem_read_subword (const uint8_t   xlen,
				       const uint64_t  addr,
				       uint32_t       *data,
				       const size_t    len)
{
    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "gdbstub_be_mem_read_subword (addr 0x%0" PRIx64 ", data, len %0zu)\n",
		 addr, len);
	fflush (logfile_fp);
    }

    uint32_t status = 0;

    const uint64_t  addr_lsb_mask = 0x3;
    const uint64_t  addr_lim      = ((addr + 4) & (~ addr_lsb_mask));
    if ((addr + len) > addr_lim) {
	fprintf (stderr, "    ERROR: requested range straddles 32-bit words\n");
	return status_err;
    }

    DM_sbaccess  sbaccess;
    if (len == 1) {
	sbaccess = DM_SBACCESS_8_BIT;
    }
    else if (len == 2) {
	if ((addr & 0x1) != 0) {
	    fprintf (stderr, "    ERROR: requested address is not aligned for requested size\n");
	    return status_err;
	}
	sbaccess = DM_SBACCESS_16_BIT;
    }
    else if (len == 4) {
	if ((addr & 0x3) != 0) {
	    fprintf (stderr, "    ERROR: requested address is not aligned for requested size\n");
	    return status_err;
	}
	sbaccess = DM_SBACCESS_32_BIT;
    }
    else /* (len == 3) */ {
	fprintf (stderr, "    ERROR: requested len is %0zu, should be 1, 2 or 4 only\n", len);
	return status_err;
    }

    // Write SBCS

    status = gdbstub_be_wait_for_sb_nonbusy (NULL);
    if (status == status_err) return status;
    uint32_t sbcs = fn_mk_sbcs (true,                      // sbbusyerr (W1C)
				true,                      // sbreadonaddr
				sbaccess,                  // sbaccess (size)
				false,                     // sbautoincrement
				false,                     // sbreadondata
				DM_SBERROR_UNDEF7_W1C);    // Clear sberror
    if (logfile_fp != NULL) {
	fprint_sbcs (logfile_fp, "    Write ", sbcs, "\n");
	fflush (logfile_fp);
    }
    dmi_write (dm_addr_sbcs, sbcs);

    // Write address to sbaddress1/0 (which will start a bus read)
    status = gdbstub_be_wait_for_sb_nonbusy (NULL);
    if (status == status_err) return status;
    if (xlen == 64) {
	// Write upper 64b of address to sbaddress1
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp, "    Write to sbaddress1: 0x%08" PRIx32 "\n", (uint32_t) (addr >> 32));
	    fflush (logfile_fp);
	}
	dmi_write (dm_addr_sbaddress1, (uint32_t) (addr >> 32));
    }
    // Write lower 32b of the address to sbaddress0
    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "    Write to sbaddress0: 0x%08" PRIx32 "\n", (uint32_t) addr);
	fflush (logfile_fp);
    }
    dmi_write (dm_addr_sbaddress0, (uint32_t) addr);

    status = gdbstub_be_wait_for_sb_nonbusy (NULL);
    if (status == status_err) return status;
    uint32_t x = dmi_read (dm_addr_sbdata0);

    // Return the data
    *data = x;

    return status_ok;
}

// ================================================================
// Read 'len' bytes from SoC memory starting at address 'addr' into 'data'.
// No alignment restriction on 'addr'; no restriction on 'len'.
// Only performs 32-bit reads on the Debug Module.

uint32_t  gdbstub_be_mem_read (const uint8_t   xlen,
			       const uint64_t  addr,
			       char           *data,
			       const size_t    len)
{
    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "gdbstub_be_mem_read (addr 0x%0" PRIx64 ", data, len %0zu)\n",
		 addr, len);
	fflush (logfile_fp);
    }

    if (len == 0)
	return status_ok;

    uint32_t status = 0;

    const uint64_t addr_lsb_mask = 0x3;
    const uint64_t addr_lim      = addr + len;
    uint64_t       addr4         = (addr & (~ addr_lsb_mask));            // 32b-aligned at/below addr
    const uint64_t addr_lim4     = ((addr_lim + 3) & (~ addr_lsb_mask));  // 32b-aligned at/below addr_lim
    size_t         jd            = 0;                                     // index into data []

    // Write SBCS
    status = gdbstub_be_wait_for_sb_nonbusy (NULL);
    if (status == status_err) return status;
    uint32_t sbcs = fn_mk_sbcs (true,                      // sbbusyerr (W1C)
				true,                      // sbreadonaddr
				DM_SBACCESS_32_BIT,        // sbaccess (size)
				true,                      // sbautoincrement
				true,                      // sbreadondata
				DM_SBERROR_UNDEF7_W1C);    // Clear sberror
    if (logfile_fp != NULL) {
	fprint_sbcs (logfile_fp, "    Write ", sbcs, "\n");
	fflush (logfile_fp);
    }
    dmi_write (dm_addr_sbcs, sbcs);

    // Write the initial address to sbaddress0 (which will start a bus read)
    status = gdbstub_be_wait_for_sb_nonbusy (NULL);
    if (status == status_err) return status;
    if (xlen == 64) {
	// Write upper 32b of address to sbaddress1
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp, "    Write to sbaddress1: 0x%08" PRIx32 "\n", (uint32_t) (addr4 >> 32));
	    fflush (logfile_fp);
	}
	dmi_write (dm_addr_sbaddress1, (uint32_t) (addr4 >> 32));
    }
    // Write lower 32b of the address to sbaddress0
    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "    Write to sbaddress0: 0x%08" PRIx32 "\n", (uint32_t) addr4);
	fflush (logfile_fp);
    }
    dmi_write (dm_addr_sbaddress0, (uint32_t) addr4);

    // Repeatedly read sbdata0
    while (addr4 < addr_lim4) {
	assert (jd < len);
	status = gdbstub_be_wait_for_sb_nonbusy (NULL);
	if (status == status_err) return status;
	uint32_t x = dmi_read (dm_addr_sbdata0);

	// If this is first word and addr is unaligned, copy relevant bytes (< 4)
	if (addr4 < addr) {
	    assert ((addr - addr4) < 4);
	    uint8_t  *p_x    = (uint8_t *) (& x);
	    size_t    offset = (size_t) (addr - addr4);
	    memcpy (& (data [0]), & (p_x [offset]), 4 - offset);
	    jd    += (4 - offset);
	    /*
	    if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
                     "gdbstub_be_mem_read: addr4 0x%0" PRIx64 ", initial %0zu bytes\n",
		     addr4, (4 - offset));
            }
	    */
	}
	// If this is intermediate whole-32b word, copy all 4 bytes
	else if ((addr4 + 4) <= addr_lim) {
	    memcpy (& (data [jd]), & x, 4);
	    jd    += 4;
	    /*
	    if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
	             "gdbstub_be_mem_read: addr4 0x%0" PRIx64 ", 4 bytes\n", addr4);
            }
	    */
	}
	// If this is last word and remainder is < 4 bytes, copy relevant bytes (< 4)
	else {
	    memcpy (& (data [jd]), & x, addr_lim - addr4);
	    jd    += (addr_lim - addr4);
	    assert (jd >= len);
	    /*
	    if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
                     "gdbstub_be_mem_read: addr4 0x%0" PRIx64 ", final %0" PRId64 " bytes\n",
		     addr4, (addr_lim - addr4));
            }
	    */
	}
	addr4 += 4;
    }

    // Log it
    if (logfile_fp != NULL) {
	fprint_mem_data (logfile_fp, verbosity, data, jd);
	fflush (logfile_fp);
    }

    return status_ok;
}

// ================================================================
// Write a value into the RISC-V PC

uint32_t  gdbstub_be_PC_write (const uint8_t xlen, uint64_t regval)
{
    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "gdbstub_be_PC_write (data 0x%0" PRIx64 ")\n", regval);
	fflush (logfile_fp);
    }

    // Write 'dpc' in debug module, = CSR 0X7b1
    uint8_t  cmderr;
    uint32_t status = gdbstub_be_reg_write (xlen, csr_addr_dpc, regval, & cmderr);

    if (status == status_err) {
	if (logfile_fp != NULL) {
	    fprint_abstractcs_cmderr (logfile_fp,
				      "    ERROR: gdbstub_be_PC_write (csr 0x7b1) => ",
				      status, "\n");
	    fflush (logfile_fp);
	}
    }
    else
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "    gdbstub_be_PC_write (csr 0x7b1) => 0x%0" PRIx64 "\n",
		     regval);
	    fflush (logfile_fp);
	}

    return status;
}

// ================================================================
// Write a value into a RISC-V GPR register

uint32_t  gdbstub_be_GPR_write (const uint8_t xlen, uint8_t regnum, uint64_t regval)
{
    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "gdbstub_be_GPR_write (gpr 0x%0x, data 0x%0" PRIx64 ")\n",
		 regnum, regval);
	fflush (logfile_fp);
    }

    assert (regnum < 32);

    // Debug module encodes GPR x as 0x1000 + x
    uint8_t  cmderr;
    uint16_t hwregnum = (uint16_t) (regnum + dm_command_access_reg_regno_gpr_0);
    uint32_t status = gdbstub_be_reg_write (xlen, hwregnum, regval, & cmderr);

    if (status == status_err) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "    ERROR: gdbstub_be_GPR_write (gpr 0x%0x)",
		     regnum);
	    fprint_abstractcs_cmderr (logfile_fp, " => ", cmderr, "\n");
	    fflush (logfile_fp);
	}
    }
    return status;
}

// ================================================================
// Write a value into a RISC-V FPR register

uint32_t  gdbstub_be_FPR_write (const uint8_t xlen, uint8_t regnum, uint64_t regval)
{
    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "gdbstub_be_FPR_write (fpr 0x%0x, data 0x%0" PRIx64 ")\n",
		 regnum, regval);
	fflush (logfile_fp);
    }

    assert (regnum < 32);

    // Debug module encodes FPR x as 0x1000 + x
    uint8_t  cmderr;
    uint16_t hwregnum = (uint16_t) (regnum + dm_command_access_reg_regno_fpr_0);
    uint32_t status = gdbstub_be_reg_write (xlen, hwregnum, regval, & cmderr);

    if (status == status_err) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "    ERROR: gdbstub_be_FPR_write (fpr 0x%0x)",
		     regnum);
	    fprint_abstractcs_cmderr (logfile_fp, " => ", cmderr, "\n");
	    fflush (logfile_fp);
	}
    }

    return status;
}

// ================================================================
// Write a value into a RISC-V CSR register

uint32_t  gdbstub_be_CSR_write (const uint8_t xlen, uint16_t regnum, uint64_t regval)
{
    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "gdbstub_be_CSR_write (csr 0x%0x, data 0x%0" PRIx64 ")\n",
		 regnum, regval);
	fflush (logfile_fp);
    }

    assert (regnum < 0xFFF);

    // Debug module encodes CSR x as x
    uint8_t  cmderr;
    uint16_t hwregnum = (uint16_t) (regnum + dm_command_access_reg_regno_csr_0);
    uint32_t status = gdbstub_be_reg_write (xlen, hwregnum, regval, & cmderr);

    if (status == status_err) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "    ERROR: gdbstub_be_CSR_write (csr 0x%0x)",
		     regnum);
	    fprint_abstractcs_cmderr (logfile_fp, " => ", cmderr, "\n");
	    fflush (logfile_fp);
	}
    }

    return status;
}

// ================================================================
// Write 'len' bytes of 'data' into RISC-V system memory, starting at address 'addr'
// where 'len' is 1, 2 or 4 only, and addr is aligned.

uint32_t  gdbstub_be_mem_write_subword (const uint8_t   xlen,
					const uint64_t  addr,
					const uint32_t  data,
					const size_t    len)
{
    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "gdbstub_be_mem_write_subword (addr 0x%0" PRIx64 ", data 0x%0" PRIx32 ", len %0zu)\n",
		 addr, data, len);
	fflush (logfile_fp);
    }

    if ((len != 1) && (len != 2) && (len != 4)) {
	fprintf (stderr, "    ERROR: len (%0zu) should be 1, 2 or 4 only\n", len);
	return status_err;
    }
    
    uint32_t status = 0;

    DM_sbaccess  sbaccess;
    if (len == 1) {
	sbaccess = DM_SBACCESS_8_BIT;
    }
    else if (len == 2) {
	if ((addr & 0x1) != 0) {
	    fprintf (stderr, "    ERROR: requested address is not aligned for requested size\n");
	    return status_err;
	}
	sbaccess = DM_SBACCESS_16_BIT;
    }
    else /* (len == 4) */ {
	if ((addr & 0x3) != 0) {
	    fprintf (stderr, "    ERROR: requested address is not aligned for requested size\n");
	    return status_err;
	}
	sbaccess = DM_SBACCESS_32_BIT;
    }

    // Write SBCS
    status = gdbstub_be_wait_for_sb_nonbusy (NULL);
    if (status == status_err) return status;
    uint32_t sbcs = fn_mk_sbcs (true,                      // sbbusyerr (W1C)
				false,                     // sbreadonaddr
				sbaccess,                  // sbaccess (size)
				false,                     // sbautoincrement
				false,                     // sbreadondata
				DM_SBERROR_UNDEF7_W1C);    // Clear sberror
    if (logfile_fp != NULL) {
	fprint_sbcs (logfile_fp, "    Write ", sbcs, "\n");
	fflush (logfile_fp);
    }
    dmi_write (dm_addr_sbcs, sbcs);

    // Write address to sbaddress1/0
    status = gdbstub_be_wait_for_sb_nonbusy (NULL);
    if (status == status_err) return status;
    if (xlen == 64) {
	// Write upper 32b of address to sbaddress1
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp, "    Write to sbaddress1: 0x%08" PRIx32 "\n", (uint32_t) (addr >> 32));
	    fflush (logfile_fp);
	}
	dmi_write (dm_addr_sbaddress1, (uint32_t) (addr >> 32));
    }
    // Write lower 32b of the address to sbaddress0
    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "    Write to sbaddress0: 0x%08" PRIx32 "\n", (uint32_t) addr);
	fflush (logfile_fp);
    }
    dmi_write (dm_addr_sbaddress0, (uint32_t) addr);

    // Write the data
    dmi_write (dm_addr_sbdata0, data);

    status = gdbstub_be_wait_for_sb_nonbusy (NULL);
    return status;
}

// ================================================================
// Write 'len' bytes of 'data' into RISC-V system memory, starting at address 'addr'
// Only performs 32-bit writes on the Debug Module.

uint32_t  gdbstub_be_mem_write (const uint8_t   xlen,
				const uint64_t  addr,
				const char     *data,
				const size_t    len)
{
    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "gdbstub_be_mem_write (addr 0x%0" PRIx64 ", data, len %0zu)\n",
		 addr, len);
	fflush (logfile_fp);
    }

    if (len == 0)
	return status_ok;

    // Log it
    if (logfile_fp != NULL) {
	fprint_mem_data (logfile_fp, verbosity, data, len);
	fflush (logfile_fp);
    }

    uint32_t status = 0;

    const uint64_t addr_lim  = addr + len;
    uint64_t       addr4     = (addr & (~ ((uint64_t) 0x3)));        // 32b-aligned at/below addr
    const uint64_t addr_lim4 = (addr_lim & (~ ((uint64_t) 0x3)));    // 32b-aligned at/below addr_lim
    size_t         jd        = 0;                       // index into data []

    // ----------------
    // Write any initial  unaligned bytes by doing a 32b read-modify-write

    if (addr != addr4) {
	uint32_t x;
	status = gdbstub_be_mem32_read ("gdbstub_be_mem32_read", xlen, addr4, & x);
	if (status != status_ok) return status;
	uint8_t  *p_x    = (uint8_t *) (& x);
	size_t    offset = (size_t) (addr - addr4);
	memcpy (& (p_x [offset]), & (data [0]), 4 - offset);
	status = gdbstub_be_mem32_write ("gdbstub_be_mem_write", xlen, addr4, x);
	if (status != status_ok) return status;

	addr4 += 4;
	jd    += (4 - offset);
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp, "    Write initial sub-word (%0zu bytes)\n", (4 - offset));
	    fflush (logfile_fp);
	}
    }

    // ----------------
    // Write aligned whole-32-bit words

    if (addr4 < addr_lim4)
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp, "    Write words (%0" PRIx64 " bytes)\n", (addr_lim4 - addr4));
	    fflush (logfile_fp);
	}

    // Write SBCS
    status = gdbstub_be_wait_for_sb_nonbusy (NULL);
    if (status == status_err) return status;
    uint32_t sbcs = fn_mk_sbcs (true,                      // sbbusyerr (W1C)
				false,                     // sbreadonaddr
				DM_SBACCESS_32_BIT,        // sbaccess (size)
				true,                      // sbautoincrement
				false,                     // sbreadondata
				DM_SBERROR_UNDEF7_W1C);    // Clear sberror
    if (logfile_fp != NULL) {
	fprint_sbcs (logfile_fp, "    Write ", sbcs, "\n");
	fflush (logfile_fp);
    }
    dmi_write (dm_addr_sbcs, sbcs);

    // Write address to sbaddress1/0
    status = gdbstub_be_wait_for_sb_nonbusy (NULL);
    if (status == status_err) return status;
    if (xlen == 64) {
	// Write upper 64b of address to sbaddress1
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp, "    Write to sbaddress1: 0x%08" PRIx32 "\n", (uint32_t) (addr4 >> 32));
	    fflush (logfile_fp);
	}
	dmi_write (dm_addr_sbaddress1, (uint32_t) (addr4 >> 32));
    }
    // Write lower 64b of the address to sbaddress0
    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "    Write to sbaddress0: 0x%08" PRIx32 "\n", (uint32_t) addr4);
	fflush (logfile_fp);
    }
    dmi_write (dm_addr_sbaddress0, (uint32_t) addr4);

    while (addr4 < addr_lim4) {
	// status = gdbstub_be_wait_for_sb_nonbusy (NULL);
	// if (status == status_err) return status;

	uint32_t *p = (uint32_t *) (& (data [jd]));
	uint32_t  x = *p;
	if (verbosity > 1)
	    if (logfile_fp != NULL) {
		fprintf (logfile_fp,
			 "    Write to addr: 0x%08" PRIx64 " <= data 0x%08x\n",
			 addr4, x);
		fflush (logfile_fp);
	    }

	// Show progress every 1 MB
	if ((addr4 & 0xFFFFF) == 0)
	    fprintf (stdout,
		     "    ... mem [0x%08" PRIx64 "] <= 0x%08x\n",
		     addr4, x);

	dmi_write (dm_addr_sbdata0, x);

	addr4 += 4;
	jd    += 4;
    }

    // ----------------
    // Write any final unaligned bytes by doing a 32b read-modify-write

    if (addr4 < addr_lim) {
	uint32_t x;
	gdbstub_be_mem32_read ("gdbstub_be_mem_write", xlen, addr4, & x);
	uint8_t  *p_x    = (uint8_t *) (& x);
	size_t    n      = (size_t) (addr_lim - addr4);
	memcpy (p_x, & (data [jd]), n);
	gdbstub_be_mem32_write ("gdbstub_be_mem_write", xlen, addr4, x);
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp, "    Write final sub-word (%0zu bytes)\n", n);
	    fflush (logfile_fp);
	}
    }

    // ----------------
    // Check for errors
    status = gdbstub_be_wait_for_sb_nonbusy (& sbcs);
    if (status != status_ok) return status;

    if (fn_sbcs_sbbusyerror (sbcs)) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp, "    ERROR: sbcs.sbbusyerror\n");
	    fflush (logfile_fp);
	}
	return status_err;
    }

    DM_sberror sberror = fn_sbcs_sberror (sbcs);
    if (sberror != DM_SBERROR_NONE) {
	if (logfile_fp != NULL) {
	    fprint_sberror (logfile_fp, "    ERROR: sbcs.sberror: ", sberror, "\n");
	    fflush (logfile_fp);
	}
	return status_err;
    }

    // ----------------
    return status_ok;
}

// ****************************************************************
// ****************************************************************
// ****************************************************************
// Raw reads and writes of the DMI interface (for debugging)

// ================================================================
// Raw DMI read

uint32_t  gdbstub_be_dmi_read (const uint16_t  dmi_addr, uint32_t *p_data)
{
    *p_data = 0;
    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "gdbstub_be_dmi_read (dmi addr 0x%0x)\n", dmi_addr);
	fflush (logfile_fp);
    }

    uint32_t data = dmi_read (dmi_addr);
    *p_data = data;

    return status_ok;
}

// ================================================================
// Raw DMI write

uint32_t  gdbstub_be_dmi_write (const uint16_t  dmi_addr, uint32_t  dmi_data)
{
    if (! initialized) return status_ok;

    if (logfile_fp != NULL) {
	fprintf (logfile_fp,
		 "gdbstub_be_dmi_write (dmi 0x%0x, data 0x%0" PRIx32 ")\n",
		 dmi_addr, dmi_data);
	fflush (logfile_fp);
    }

    dmi_write (dmi_addr, dmi_data);
    return status_ok;
}

// ================================================================
