// Copyright (c) 2016-2020 Bluespec, Inc. All Rights Reserved
// Author: Rishiyur Nikhil

// ================================================================
// Definitions for RISC-V Debug Module

// Ref:
//    RISC-V External Debug Support
//    Version 0.13
//    ed66f39bddd874be8262cc22b8cb08b8d510ff15
//    Tue Oct 2 23:17:49 2018 -0700

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

// ----------------
// Project includes

#include  "RVDM.h"

// ================================================================
// Debug Module address map

// ----------------
// Run Control

const uint16_t dm_addr_dmcontrol    = 0x10;
const uint16_t dm_addr_dmstatus     = 0x11;
const uint16_t dm_addr_hartinfo     = 0x12;
const uint16_t dm_addr_haltsum      = 0x13;
const uint16_t dm_addr_hawindowsel  = 0x14;
const uint16_t dm_addr_hawindow     = 0x15;
const uint16_t dm_addr_devtreeaddr0 = 0x19;
const uint16_t dm_addr_authdata     = 0x30;
const uint16_t dm_addr_haltregion0  = 0x40;
const uint16_t dm_addr_haltregion31 = 0x5F;
const uint16_t dm_addr_verbosity    = 0x60;        // NON-STANDARD

// ----------------
// Abstract commands (read/write RISC-V registers and RISC-V CSRs)

const uint16_t dm_addr_abstractcs   = 0x16;
const uint16_t dm_addr_command      = 0x17;

const uint16_t dm_addr_data0        = 0x04;
const uint16_t dm_addr_data1        = 0x05;
const uint16_t dm_addr_data2        = 0x06;
const uint16_t dm_addr_data3        = 0x07;
const uint16_t dm_addr_data4        = 0x08;
const uint16_t dm_addr_data5        = 0x09;
const uint16_t dm_addr_data6        = 0x0a;
const uint16_t dm_addr_data7        = 0x0b;
const uint16_t dm_addr_data8        = 0x0c;
const uint16_t dm_addr_data9        = 0x0d;
const uint16_t dm_addr_data10       = 0x0d;
const uint16_t dm_addr_data11       = 0x0f;

const uint16_t dm_addr_abstractauto = 0x18;
const uint16_t dm_addr_progbuf0     = 0x20;

// ----------------
// System Bus access (read/write RISC-V memory/devices)

const uint16_t dm_addr_sbcs         = 0x38;
const uint16_t dm_addr_sbaddress0   = 0x39;
const uint16_t dm_addr_sbaddress1   = 0x3a;
const uint16_t dm_addr_sbaddress2   = 0x3b;
const uint16_t dm_addr_sbdata0      = 0x3c;
const uint16_t dm_addr_sbdata1      = 0x3d;
const uint16_t dm_addr_sbdata2      = 0x3e;
const uint16_t dm_addr_sbdata3      = 0x3f;

// ----------------------------------------------------------------
// Print dm address name

void fprint_dm_addr_name (FILE *fp, char *pre, const uint16_t dm_addr, char *post)
{
    fprintf (fp, "%s", pre);

    // ----------------
    // Run Control

    if (dm_addr == dm_addr_dmcontrol) fprintf (fp, "dmcontrol");
    else if (dm_addr == dm_addr_dmstatus) fprintf (fp, "dmstatus");
    else if (dm_addr == dm_addr_hartinfo) fprintf (fp, "hartinfo");
    else if (dm_addr == dm_addr_haltsum) fprintf (fp, "haltsum");
    else if (dm_addr == dm_addr_hawindowsel) fprintf (fp, "hawindowsel");
    else if (dm_addr == dm_addr_hawindow) fprintf (fp, "hawindow");
    else if (dm_addr == dm_addr_devtreeaddr0) fprintf (fp, "devtreeaddr0");
    else if (dm_addr == dm_addr_authdata) fprintf (fp, "authdata");
    else if (dm_addr == dm_addr_haltregion0) fprintf (fp, "haltregion0");
    else if (dm_addr == dm_addr_haltregion31) fprintf (fp, "haltregion31");
    else if (dm_addr == dm_addr_verbosity) fprintf (fp, "verbosity");

    // ----------------
    // Abstract commands (read/write RISC-V registers and RISC-V CSRs)

    else if (dm_addr == dm_addr_abstractcs) fprintf (fp, "abstractcs");
    else if (dm_addr == dm_addr_command) fprintf (fp, "command");

    else if (dm_addr == dm_addr_data0) fprintf (fp, "data0");
    else if (dm_addr == dm_addr_data1) fprintf (fp, "data1");
    else if (dm_addr == dm_addr_data2) fprintf (fp, "data2");
    else if (dm_addr == dm_addr_data3) fprintf (fp, "data3");
    else if (dm_addr == dm_addr_data4) fprintf (fp, "data4");
    else if (dm_addr == dm_addr_data5) fprintf (fp, "data5");
    else if (dm_addr == dm_addr_data6) fprintf (fp, "data6");
    else if (dm_addr == dm_addr_data7) fprintf (fp, "data7");
    else if (dm_addr == dm_addr_data8) fprintf (fp, "data8");
    else if (dm_addr == dm_addr_data9) fprintf (fp, "data9");
    else if (dm_addr == dm_addr_data10) fprintf (fp, "data10");
    else if (dm_addr == dm_addr_data11) fprintf (fp, "data11");

    else if (dm_addr == dm_addr_abstractauto) fprintf (fp, "abstractauto");
    else if (dm_addr == dm_addr_progbuf0) fprintf (fp, "progbuf0");

    // ----------------
    // System Bus access (read/write RISC-V memory/devices)

    else if (dm_addr == dm_addr_sbcs) fprintf (fp, "sbcs");
    else if (dm_addr == dm_addr_sbaddress0) fprintf (fp, "sbaddress0");
    else if (dm_addr == dm_addr_sbaddress1) fprintf (fp, "sbaddress1");
    else if (dm_addr == dm_addr_sbaddress2) fprintf (fp, "sbaddress2");
    else if (dm_addr == dm_addr_sbdata0) fprintf (fp, "sbdata0");
    else if (dm_addr == dm_addr_sbdata1) fprintf (fp, "sbdata1");
    else if (dm_addr == dm_addr_sbdata2) fprintf (fp, "sbdata2");
    else if (dm_addr == dm_addr_sbdata3) fprintf (fp, "sbdata3");

    else fprintf (fp, "dmi addr 0x%0x", dm_addr);

    fprintf (fp, "%s", post);
}

// ----------------------------------------------------------------
// Debug CSR addresses

const uint16_t csr_addr_dcsr        = 0x7B0;    // Debug control and status
const uint16_t csr_addr_dpc         = 0x7B1;    // Debug PC
const uint16_t csr_addr_dscratch0   = 0x7B2;    // Debug scratch0
const uint16_t csr_addr_dscratch1   = 0x7B2;    // Debug scratch1

// ================================================================
// Run Control DM register fields

// ----------------------------------------------------------------
// 'dmcontrol' register

uint32_t fn_mk_dmcontrol (bool       haltreq,
			  bool       resumereq,
			  bool       hartreset,
			  bool       ackhavereset,
			  bool       hasel,
			  uint16_t   hartsello,
			  uint16_t   hartselhi,
			  bool       setresethaltreq,
			  bool       clrresethaltreq,
			  bool       ndmreset,
			  bool       dmactive)
{
    return ((  (((uint32_t) haltreq)         & 0x1)   << 31)
	    | ((((uint32_t) resumereq)       & 0x1)   << 30)
	    | ((((uint32_t) hartreset)       & 0x1)   << 29)
	    | ((((uint32_t) ackhavereset)    & 0x1)   << 28)
	    | ((((uint32_t) hasel)           & 0x1)   << 26)
	    | ((((uint32_t) hartsello)       & 0x3FF) << 16)
	    | ((((uint32_t) hartselhi)       & 0x3FF) <<  6)
	    | ((((uint32_t) setresethaltreq) & 0x1)   <<  3)
	    | ((((uint32_t) clrresethaltreq) & 0x1)   <<  2)
	    | ((((uint32_t) ndmreset)        & 0x1)   <<  1)
	    | ((((uint32_t) dmactive)        & 0x1)   <<  0));
}

bool     fn_dmcontrol_haltreq         (uint32_t dm_word) { return ((dm_word >> 31) & 0x1); }
bool     fn_dmcontrol_resumereq       (uint32_t dm_word) { return ((dm_word >> 30) & 0x1); }
bool     fn_dmcontrol_hartreset       (uint32_t dm_word) { return ((dm_word >> 29) & 0x1); }
bool     fn_dmcontrol_ackhavereset    (uint32_t dm_word) { return ((dm_word >> 28) & 0x1); }
bool     fn_dmcontrol_hasel           (uint32_t dm_word) { return ((dm_word >> 26) & 0x1); }
uint16_t fn_dmcontrol_hartsello       (uint32_t dm_word) { return ((dm_word >> 16) & 0x3FF); }
uint16_t fn_dmcontrol_hartselhi       (uint32_t dm_word) { return ((dm_word >>  6) & 0x3FF); }
bool     fn_dmcontrol_setresethaltreq (uint32_t dm_word) { return ((dm_word >>  3) & 0x1); }
bool     fn_dmcontrol_clrresethaltreq (uint32_t dm_word) { return ((dm_word >>  2) & 0x1); }
bool     fn_dmcontrol_ndmreset        (uint32_t dm_word) { return ((dm_word >>  1) & 0x1); }
bool     fn_dmcontrol_dmactive        (uint32_t dm_word) { return ((dm_word >>  0) & 0x1); }

void fprint_dmcontrol (FILE *fp, char *pre, uint32_t dmcontrol, char *post)
{
    fprintf (fp, "%sDMCONTROL{0x%08x= ", pre, dmcontrol);

    if (fn_dmcontrol_haltreq         (dmcontrol)) fprintf (fp, " haltreq");
    if (fn_dmcontrol_resumereq       (dmcontrol)) fprintf (fp, " resumereq");
    if (fn_dmcontrol_hartreset       (dmcontrol)) fprintf (fp, " hartreset");
    if (fn_dmcontrol_ackhavereset    (dmcontrol)) fprintf (fp, " ackhavereset");
    if (fn_dmcontrol_hasel           (dmcontrol)) fprintf (fp, " hasel");
    fprintf (fp, " hartsello 0x%0x", fn_dmcontrol_hartsello (dmcontrol));
    fprintf (fp, " hartselhi 0x%0x", fn_dmcontrol_hartselhi (dmcontrol));
    if (fn_dmcontrol_setresethaltreq (dmcontrol)) fprintf (fp, " setresethaltreq");
    if (fn_dmcontrol_clrresethaltreq (dmcontrol)) fprintf (fp, " clrresethaltreq");
    if (fn_dmcontrol_ndmreset        (dmcontrol)) fprintf (fp, " ndmreset");
    if (fn_dmcontrol_dmactive        (dmcontrol)) fprintf (fp, " dmactive");

    fprintf (fp, "}%s", post);
}

// ----------------------------------------------------------------
// 'dmstatus' register

static bool fn_dmstatus_impebreak       (uint32_t x)  { return ((x >> 22) & 0x1); }
static bool fn_dmstatus_allhavereset    (uint32_t x)  { return ((x >> 19) & 0x1); }
static bool fn_dmstatus_anyhavereset    (uint32_t x)  { return ((x >> 18) & 0x1); }
static bool fn_dmstatus_allresumeack    (uint32_t x)  { return ((x >> 17) & 0x1); }
static bool fn_dmstatus_anyresumeack    (uint32_t x)  { return ((x >> 16) & 0x1); }
static bool fn_dmstatus_allnonexistent  (uint32_t x)  { return ((x >> 15) & 0x1); }
static bool fn_dmstatus_anynonexistent  (uint32_t x)  { return ((x >> 14) & 0x1); }
static bool fn_dmstatus_allunavail      (uint32_t x)  { return ((x >> 13) & 0x1); }
static bool fn_dmstatus_anyunavail      (uint32_t x)  { return ((x >> 12) & 0x1); }
static bool fn_dmstatus_allrunning      (uint32_t x)  { return ((x >> 11) & 0x1); }
static bool fn_dmstatus_anyrunning      (uint32_t x)  { return ((x >> 10) & 0x1); }
static bool fn_dmstatus_allhalted       (uint32_t x)  { return ((x >>  9) & 0x1); }
static bool fn_dmstatus_anyhalted       (uint32_t x)  { return ((x >>  8) & 0x1); }
static bool fn_dmstatus_authenticated   (uint32_t x)  { return ((x >>  7) & 0x1); }
static bool fn_dmstatus_authbusy        (uint32_t x)  { return ((x >>  6) & 0x1); }
static bool fn_dmstatus_hasresethaltreq (uint32_t x)  { return ((x >>  5) & 0x1); }
static bool fn_dmstatus_confstrptrvalid (uint32_t x)  { return ((x >>  4) & 0x1); }
static uint8_t fn_dmstatus_version      (uint32_t x)  { return (x & 0xF); }

void fprint_dmstatus (FILE *fp, char *pre, uint32_t dmstatus, char *post)
{
    fprintf (fp, "%sDMSTATUS{0x%08x= ", pre, dmstatus);
    if (fn_dmstatus_impebreak       (dmstatus)) fprintf (fp, " impebreak");
    if (fn_dmstatus_allhavereset    (dmstatus)) fprintf (fp, " allhavereset");
    if (fn_dmstatus_anyhavereset    (dmstatus)) fprintf (fp, " anyhavereset");
    if (fn_dmstatus_allresumeack    (dmstatus)) fprintf (fp, " allresumeack");
    if (fn_dmstatus_anyresumeack    (dmstatus)) fprintf (fp, " anyresumeack");
    if (fn_dmstatus_allnonexistent  (dmstatus)) fprintf (fp, " allnonexistent");
    if (fn_dmstatus_anynonexistent  (dmstatus)) fprintf (fp, " anynonexistent");
    if (fn_dmstatus_allunavail      (dmstatus)) fprintf (fp, " allunavail");
    if (fn_dmstatus_anyunavail      (dmstatus)) fprintf (fp, " anyunavail");
    if (fn_dmstatus_allrunning      (dmstatus)) fprintf (fp, " allrunning");
    if (fn_dmstatus_anyrunning      (dmstatus)) fprintf (fp, " anyrunning");
    if (fn_dmstatus_allhalted       (dmstatus)) fprintf (fp, " allhalted");
    if (fn_dmstatus_anyhalted       (dmstatus)) fprintf (fp, " anyhalted");
    if (fn_dmstatus_authenticated   (dmstatus)) fprintf (fp, " authenticated");
    if (fn_dmstatus_authbusy        (dmstatus)) fprintf (fp, " authbusy");
    if (fn_dmstatus_hasresethaltreq (dmstatus)) fprintf (fp, " hasresethaltreq");
    if (fn_dmstatus_confstrptrvalid (dmstatus)) fprintf (fp, " confstrptrvalid");
   
    uint32_t version = fn_dmstatus_version (dmstatus);
    if      (version == 0)  fprintf (fp, " No Debug Module");
    else if (version == 1)  fprintf (fp, " Debug Module v0.11");
    else if (version == 2)  fprintf (fp, " Debug Module v0.13");
    else if (version == 15) fprintf (fp, " Debug Module vUNKNOWN");
    else                    fprintf (fp, " Debug Module vBOGUS");
    fprintf (fp, "}%s", post);
}

// ================================================================
// Abstract Command register fields

// ----------------------------------------------------------------
// 'dm_abstractcs' register

uint32_t fn_mk_abstractcs (DM_abstractcs_cmderr cmderr)
{
    return ((cmderr & 0x7) << 8);
}

uint8_t fn_abstractcs_progbufsize (uint32_t dm_word) { return ((dm_word >> 24) & 0x1F); }
bool    fn_abstractcs_busy        (uint32_t dm_word) { return ((dm_word >> 12) & 0x01); }
DM_abstractcs_cmderr fn_abstractcs_cmderr (uint32_t dm_word)
{
    return ((dm_word >> 8) & 0x7);
}

uint8_t fn_abstractcs_datacount (uint32_t dm_word) { return (dm_word & 0x1F); }

void fprint_abstractcs_cmderr (FILE *fp,
			       const char *pre,
			       const DM_abstractcs_cmderr cmderr,
			       const char *post)
{
    fprintf (fp, "%s", pre);
    switch (cmderr) {
    case DM_ABSTRACTCS_CMDERR_NONE:          fprintf (fp, "ABSTRACTCS_CMDERR_NONE"); break;
    case DM_ABSTRACTCS_CMDERR_BUSY:          fprintf (fp, "ABSTRACTCS_CMDERR_BUSY"); break;
    case DM_ABSTRACTCS_CMDERR_NOT_SUPPORTED: fprintf (fp, "ABSTRACTCS_CMDERR_NOT_SUPPORTED"); break;
    case DM_ABSTRACTCS_CMDERR_EXCEPTION:     fprintf (fp, "ABSTRACTCS_CMDERR_EXCEPTION"); break;
    case DM_ABSTRACTCS_CMDERR_HALT_RESUME:   fprintf (fp, "ABSTRACTCS_CMDERR_HALT_RESUME"); break;
    case DM_ABSTRACTCS_CMDERR_UNDEF5:        fprintf (fp, "ABSTRACTCS_CMDERR_UNDEF5"); break;
    case DM_ABSTRACTCS_CMDERR_UNDEF6:        fprintf (fp, "ABSTRACTCS_CMDERR_UNDEF6"); break;
    case DM_ABSTRACTCS_CMDERR_OTHER:         fprintf (fp, "ABSTRACTCS_CMDERR_OTHER"); break;
    default:                                 fprintf (fp, "ABSTRACTCS_CMDERR %0d", cmderr);
    }
    fprintf (fp, "%s", post);
}

void fprint_abstractcs (FILE *fp, char *pre, uint32_t abstractcs, char *post)
{
    fprintf (fp, "%sABSTRACT_CS{0x%08x= ", pre, abstractcs);
    fprintf (fp, " progbufsize %0d", fn_abstractcs_progbufsize (abstractcs));
    if (fn_abstractcs_busy (abstractcs)) fprintf (fp, " busy");
    fprint_abstractcs_cmderr (fp, " ", fn_abstractcs_cmderr (abstractcs), "");
    fprintf (fp, " datacount %0d", fn_abstractcs_datacount (abstractcs));
    fprintf (fp, "}%s", post);
}

// ----------------------------------------------------------------
// 'command' register

const uint16_t dm_command_access_reg_regno_csr_0   = 0x0000;
const uint16_t dm_command_access_reg_regno_csr_FFF = 0x0FFF;
const uint16_t dm_command_access_reg_regno_gpr_0   = 0x1000;
const uint16_t dm_command_access_reg_regno_gpr_1F  = 0x101F;
const uint16_t dm_command_access_reg_regno_fpr_0   = 0x1020;
const uint16_t dm_command_access_reg_regno_fpr_1F  = 0x103F;

uint32_t fn_mk_command_access_reg (DM_command_access_reg_size  size,
				   bool                        aarpostincrement,
				   bool                        postexec,
				   bool                        transfer,
				   bool                        write,
				   uint16_t                    regno)
{
    return ((  ((uint32_t) DM_COMMAND_CMDTYPE_ACCESS_REG) << 24)
	    | ((((uint32_t) size)             & 0x7)      << 20)
	    | ((((uint32_t) aarpostincrement) & 0x1)      << 19)
	    | ((((uint32_t) postexec)         & 0x1)      << 18)
	    | ((((uint32_t) transfer)         & 0x1)      << 17)
	    | ((((uint32_t) write)            & 0x1)      << 16)
	    | ((((uint32_t) regno)            & 0xFFFF)   <<  0));
}

DM_command_cmdtype         fn_command_cmdtype         (uint32_t dm_word) { return ((dm_word >> 24) & 0xFF); }
DM_command_access_reg_size fn_command_access_reg_size (uint32_t dm_word) { return ((dm_word >> 20) & 0x7);  }

bool fn_command_access_reg_postincrement (uint32_t dm_word) { return ((dm_word >> 19) & 0x1); }
bool fn_command_access_reg_postexec      (uint32_t dm_word) { return ((dm_word >> 18) & 0x1); }
bool fn_command_access_reg_transfer      (uint32_t dm_word) { return ((dm_word >> 17) & 0x1); }
bool fn_command_access_reg_write         (uint32_t dm_word) { return ((dm_word >> 16) & 0x1); }
uint16_t fn_command_access_reg_regno     (uint32_t dm_word) { return (dm_word & 0xFFFF); }

void fprint_command (FILE *fp, char *pre, uint32_t command, char *post)
{
    fprintf (fp, "%sCOMMAND{0x%08x= ", pre, command);
    if (fn_command_cmdtype (command) == DM_COMMAND_CMDTYPE_ACCESS_REG) {
	fprintf (fp, "access_reg_reg_size %0d", fn_command_access_reg_size (command));
	if (fn_command_access_reg_postincrement (command)) fprintf (fp, " postincrement");
	if (fn_command_access_reg_postexec      (command)) fprintf (fp, " postexec");
	if (fn_command_access_reg_transfer      (command)) fprintf (fp, " transfer");
	if (fn_command_access_reg_write         (command)) fprintf (fp, " write");
	else                                               fprintf (fp, " read");
	uint16_t regno = fn_command_access_reg_regno (command);
	if (regno <= dm_command_access_reg_regno_csr_FFF)
	    fprintf (fp, " CSR 0x%0x", regno);
	else if (regno <= dm_command_access_reg_regno_gpr_1F)
	    fprintf (fp, " GPR 0x%0x", regno - dm_command_access_reg_regno_gpr_1F);
	else if (regno <= dm_command_access_reg_regno_fpr_1F)
	    fprintf (fp, " FPR 0x%0x", regno - dm_command_access_reg_regno_fpr_1F);
	else
	    fprintf (fp, " Unknown regno 0x%0x", regno);
    }

    // TODO: other CMDTYPEs

    fprintf (fp, "}%s", post);
}

// ================================================================
// System Bus Access DM register fields

void fprint_sberror (FILE *fp, const char *pre, const DM_sberror sberror, const char *post)
{
    fprintf (fp, "%s", pre);
    switch (sberror) {
    case DM_SBERROR_NONE:             fprintf (fp, "SBERROR_NONE"); break;
    case DM_SBERROR_TIMEOUT:          fprintf (fp, "SBERROR_TIMEOUT"); break;
    case DM_SBERROR_BADADDR:          fprintf (fp, "SBERROR_BADADDR"); break;
    case DM_SBERROR_ALIGNMENT:        fprintf (fp, "SBERROR_ALIGNMENT"); break;
    case DM_SBERROR_UNSUPPORTED_SIZE: fprintf (fp, "SBERROR_UNSUPPORTED_SIZE"); break;
    case DM_SBERROR_UNDEF5:           fprintf (fp, "SBERROR_UNDEF5"); break;
    case DM_SBERROR_UNDEF6:           fprintf (fp, "SBERROR_UNDEF6"); break;
    case DM_SBERROR_UNDEF7_W1C:       fprintf (fp, "SBERROR_UNDEF7_W1C"); break;
    default:                          fprintf (fp, "SBERROR %0d", sberror);
    }
    fprintf (fp, "%s", post);
}

uint32_t fn_mk_sbcs (bool        sbbusyerror,
		     bool        sbreadonaddr,
		     DM_sbaccess sbaccess,
		     bool        sbautoincrement,
		     bool        sbreadondata,
		     DM_sberror  sberror)
{
    return ((  (((uint32_t) 1)               & 0x7)  << 29)    // R        sbversion
	    | ((((uint32_t) sbbusyerror)     & 0x1)  << 22)    // R/W1C
	    | ((((uint32_t) 0)               & 0x1)  << 21)    // R        sbbusy
	    | ((((uint32_t) sbreadonaddr)    & 0x1)  << 20)    // R/W
	    | ((((uint32_t) sbaccess)        & 0x7)  << 17)    // R/W
	    | ((((uint32_t) sbautoincrement) & 0x1)  << 16)    // R/W
	    | ((((uint32_t) sbreadondata)    & 0x1)  << 15)    // R/W
	    | ((((uint32_t) sberror)         & 0x7)  << 12)    // R/W1C
	    | ((((uint32_t) 0)               & 0x7F) << 5)     // R        sbasize
	    | ((((uint32_t) 0)               & 0x1F) << 4)     // R        sbaccess128
	    | ((((uint32_t) 0)               & 0x1F) << 3)     // R        sbaccess64
	    | ((((uint32_t) 0)               & 0x1F) << 2)     // R        sbaccess32
	    | ((((uint32_t) 0)               & 0x1F) << 1)     // R        sbaccess16
	    | ((((uint32_t) 0)               & 0x1F) << 0));   // R        sbaccess8
}

uint8_t     fn_sbcs_sbversion       (uint32_t dm_word) { return (uint8_t) ((dm_word >> 29) & 0x7); }
bool        fn_sbcs_sbbusyerror     (uint32_t dm_word) { return ((dm_word >> 22) & 0x1); }
bool        fn_sbcs_sbbusy          (uint32_t dm_word) { return ((dm_word >> 21) & 0x1); }
bool        fn_sbcs_sbreadonaddr    (uint32_t dm_word) { return ((dm_word >> 20) & 0x1); }
DM_sbaccess fn_sbcs_sbaccess        (uint32_t dm_word) { return ((dm_word >> 17) & 0x7); }
bool        fn_sbcs_sbautoincrement (uint32_t dm_word) { return ((dm_word >> 16) & 0x1); }
bool        fn_sbcs_sbreadondata    (uint32_t dm_word) { return ((dm_word >> 15) & 0x1); }
DM_sberror  fn_sbcs_sberror         (uint32_t dm_word) { return ((dm_word >> 12) & 0x7); }
uint8_t     fn_sbcs_sbasize         (uint32_t dm_word) { return (uint8_t) ((dm_word >> 5) & 0x7F); }
bool        fn_sbcs_sbaccess128     (uint32_t dm_word) { return ((dm_word >> 4) & 0x1);  }
bool        fn_sbcs_sbaccess64      (uint32_t dm_word) { return ((dm_word >> 3) & 0x1);  }
bool        fn_sbcs_sbaccess32      (uint32_t dm_word) { return ((dm_word >> 2) & 0x1);  }
bool        fn_sbcs_sbaccess16      (uint32_t dm_word) { return ((dm_word >> 1) & 0x1);  }
bool        fn_sbcs_sbaccess8       (uint32_t dm_word) { return ((dm_word >> 0) & 0x1);  }

void fprint_sbcs (FILE *fp, const char *pre, const uint32_t sbcs, const char *post)
{
    fprintf (fp, "%sSBCS{", pre);
    fprintf (fp, "version %0d", fn_sbcs_sbversion (sbcs));
    if (fn_sbcs_sbbusyerror  (sbcs)) fprintf (fp, " busyerror");
    if (fn_sbcs_sbbusy       (sbcs)) fprintf (fp, " busy");
    if (fn_sbcs_sbreadonaddr (sbcs)) fprintf (fp, " readonaddr");

    fprintf (fp, " sbaccess ");
    uint8_t sbaccess = fn_sbcs_sbaccess (sbcs);
    switch (sbaccess) {
    case 0: fprintf (fp, "8b"); break;
    case 1: fprintf (fp, "16b"); break;
    case 2: fprintf (fp, "32b"); break;
    case 3: fprintf (fp, "64b"); break;
    case 4: fprintf (fp, "128b"); break;
    default: fprintf (fp, "(code %0d?)", sbaccess); break;
    }

    if (fn_sbcs_sbautoincrement (sbcs)) fprintf (fp, " autoincrement");
    if (fn_sbcs_sbreadondata    (sbcs)) fprintf (fp, " readondata");

    uint8_t sberror = fn_sbcs_sberror (sbcs);
    if (sberror != 0) {
	fprintf (fp, " sberror ");
	switch (sberror) {
	case 1: fprintf (fp, "timeout"); break;
	case 2: fprintf (fp, "badaddr"); break;
	case 3: fprintf (fp, "alignment"); break;
	case 4: fprintf (fp, "unsupported_size"); break;
	case 7: fprintf (fp, "other"); break;
	default: fprintf (fp, "(code %0d?)", sberror); break;
	}
    }

    fprintf (fp, " sbasize %0db", fn_sbcs_sbasize (sbcs));

    fprintf (fp, " supported sizes");
    if (fn_sbcs_sbaccess128 (sbcs)) fprintf (fp, " 128b");
    if (fn_sbcs_sbaccess64  (sbcs)) fprintf (fp, " 64b");
    if (fn_sbcs_sbaccess32  (sbcs)) fprintf (fp, " 32b");
    if (fn_sbcs_sbaccess16  (sbcs)) fprintf (fp, " 16b");
    if (fn_sbcs_sbaccess8   (sbcs)) fprintf (fp, " 8b");

    fprintf (fp, "}%s", post);
}

// ================================================================
// DCSR fields

const uint32_t dcsr_step_bit = (1 << 2);

uint32_t fn_mk_dcsr (DM_DCSR_XDebugVer xdebugver,
		     bool              ebreakm,
		     bool              ebreaks,
		     bool              ebreaku,
		     bool              stepie,
		     bool              stopcount,
		     bool              stoptime,
		     DM_DCSR_Cause     cause,
		     bool              mprven,
		     bool              nmip,
		     bool              step,
		     DM_DCSR_PRV       prv)
{
    return ((  (((uint32_t) xdebugver) & 0xF) << 28)
	    | ((((uint32_t) ebreakm)   & 0x1) << 15)
	    | ((((uint32_t) ebreaks)   & 0x1) << 13)
	    | ((((uint32_t) ebreaku)   & 0x1) << 12)
	    | ((((uint32_t) stepie)    & 0x1) << 11)
	    | ((((uint32_t) stopcount) & 0x1) << 10)
	    | ((((uint32_t) stoptime)  & 0x1) <<  9)
	    | ((((uint32_t) cause)     & 0x7) <<  6)
	    | ((((uint32_t) mprven)    & 0x1) <<  4)
	    | ((((uint32_t) nmip)      & 0x1) <<  3)
	    | ((((uint32_t) step)      & 0x1) <<  2)
	    | ((((uint32_t) prv)       & 0x3) <<  0));
}

DM_DCSR_XDebugVer fn_dcsr_xdebugver (uint32_t dm_word) { return ((dm_word >> 28) & 0xF); }
bool fn_dcsr_ebreakm        (uint32_t dm_word) { return ((dm_word >> 15) & 0x1); }
bool fn_dcsr_ebreaks        (uint32_t dm_word) { return ((dm_word >> 13) & 0x1); }
bool fn_dcsr_ebreaku        (uint32_t dm_word) { return ((dm_word >> 12) & 0x1); }
bool fn_dcsr_stepie         (uint32_t dm_word) { return ((dm_word >> 11) & 0x1); }
bool fn_dcsr_stopcount      (uint32_t dm_word) { return ((dm_word >> 10) & 0x1); }
bool fn_dcsr_stoptime       (uint32_t dm_word) { return ((dm_word >>  9) & 0x1); }
DM_DCSR_Cause fn_dcsr_cause (uint32_t dm_word) { return ((dm_word >>  6) & 0x7); }
bool fn_dcsr_mprven         (uint32_t dm_word) { return ((dm_word >>  4) & 0x1); }
bool fn_dcsr_nmip           (uint32_t dm_word) { return ((dm_word >>  3) & 0x1); }
bool fn_dcsr_step           (uint32_t dm_word) { return ((dm_word >>  2) & 0x1); }
DM_DCSR_PRV fn_dcsr_prv     (uint32_t dm_word) { return ((dm_word >>  0) & 0x3); }

void fprint_DM_DCSR_Cause (FILE *fp, char *pre, DM_DCSR_Cause  cause, char *post)
{
    fprintf (fp, "%s", pre);
    switch (cause) {
    case DM_DCSR_CAUSE_RESERVED0:  fprintf (fp, "CAUSE_RESERVED0");  break;
    case DM_DCSR_CAUSE_EBREAK:     fprintf (fp, "CAUSE_EBREAK");     break;
    case DM_DCSR_CAUSE_TRIGGER:    fprintf (fp, "CAUSE_TRIGGER");    break;
    case DM_DCSR_CAUSE_HALTREQ:    fprintf (fp, "CAUSE_HALTREQ");    break;
    case DM_DCSR_CAUSE_STEP:       fprintf (fp, "CAUSE_STEP");       break;
    case DM_DCSR_CAUSE_RESERVED5:  fprintf (fp, "CAUSE_RESERVED5");  break;
    case DM_DCSR_CAUSE_RESERVED6:  fprintf (fp, "CAUSE_RESERVED6");  break;
    case DM_DCSR_CAUSE_RESERVED7:  fprintf (fp, "CAUSE_RESERVED7");  break;
    default:                       fprintf (fp, "CAUSE %0d", cause); break;
    }
    fprintf (fp, "%s", post);
}

void fprint_dcsr (FILE *fp, char *pre, uint32_t dcsr, char *post)
{
    fprintf (fp, "%sDCSR{0x%08x= ", pre, dcsr);

    DM_DCSR_XDebugVer ver = fn_dcsr_xdebugver (dcsr);
    if      (ver == DM_DCSR_XDEBUGVER_NONE)      fprintf (fp, "No_debugger");
    else if (ver == DM_DCSR_XDEBUGVER_V_0_13)    fprintf (fp, "Debugger v0.13");
    else if (ver == DM_DCSR_XDEBUGVER_V_UNKNOWN) fprintf (fp, "Debugger vUNKNOWN");

    if (fn_dcsr_ebreakm    (dcsr)) fprintf (fp, " ebreakm");
    if (fn_dcsr_ebreaks    (dcsr)) fprintf (fp, " ebreaks");
    if (fn_dcsr_ebreaku    (dcsr)) fprintf (fp, " ebreaku");
    if (fn_dcsr_stepie     (dcsr)) fprintf (fp, " stepie");
    if (fn_dcsr_stopcount  (dcsr)) fprintf (fp, " stopcount");
    if (fn_dcsr_stoptime   (dcsr)) fprintf (fp, " stoptime");
    fprint_DM_DCSR_Cause (fp, " ", fn_dcsr_cause (dcsr), "");
    if (fn_dcsr_mprven     (dcsr)) fprintf (fp, " mprven");
    if (fn_dcsr_nmip       (dcsr)) fprintf (fp, " nmip");
    if (fn_dcsr_step       (dcsr)) fprintf (fp, " step");
    fprintf (fp, " priv %0d", fn_dcsr_prv (dcsr));

    fprintf (fp, "}%s", post);
}

// ================================================================
