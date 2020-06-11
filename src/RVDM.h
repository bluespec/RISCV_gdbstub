// Copyright (c) 2016-2020 Bluespec, Inc. All Rights Reserved
// Author: Rishiyur Nikhil

// ================================================================

#pragma once

// ================================================================
// Definitions for RISC-V Debug Module

// Ref:
//    RISC-V External Debug Support
//    Version 0.13
//    ed66f39bddd874be8262cc22b8cb08b8d510ff15
//    Tue Oct 2 23:17:49 2018 -0700

// ================================================================
// Debug Module address map

// ----------------
// Run Control

extern const uint16_t dm_addr_dmcontrol;
extern const uint16_t dm_addr_dmstatus;
extern const uint16_t dm_addr_hartinfo;
extern const uint16_t dm_addr_haltsum;
extern const uint16_t dm_addr_hawindowsel;
extern const uint16_t dm_addr_hawindow;
extern const uint16_t dm_addr_devtreeaddr0;
extern const uint16_t dm_addr_authdata;
extern const uint16_t dm_addr_haltregion0;
extern const uint16_t dm_addr_haltregion31;
extern const uint16_t dm_addr_verbosity;        // NON-STANDARD

// ----------------
// Abstract commands (read/write RISC-V registers and RISC-V CSRs)

extern const uint16_t dm_addr_abstractcs;
extern const uint16_t dm_addr_command;

extern const uint16_t dm_addr_data0;
extern const uint16_t dm_addr_data1;
extern const uint16_t dm_addr_data2;
extern const uint16_t dm_addr_data3;
extern const uint16_t dm_addr_data4;
extern const uint16_t dm_addr_data5;
extern const uint16_t dm_addr_data6;
extern const uint16_t dm_addr_data7;
extern const uint16_t dm_addr_data8;
extern const uint16_t dm_addr_data9;
extern const uint16_t dm_addr_data10;
extern const uint16_t dm_addr_data11;

extern const uint16_t dm_addr_abstractauto;
extern const uint16_t dm_addr_progbuf0;

// ----------------
// System Bus access (read/write RISC-V memory/devices)

extern const uint16_t dm_addr_sbcs;
extern const uint16_t dm_addr_sbaddress0;
extern const uint16_t dm_addr_sbaddress1;
extern const uint16_t dm_addr_sbaddress2;
extern const uint16_t dm_addr_sbdata0;
extern const uint16_t dm_addr_sbdata1;
extern const uint16_t dm_addr_sbdata2;
extern const uint16_t dm_addr_sbdata3;

// ----------------------------------------------------------------
// Print dm address name

extern
void fprint_dm_addr_name (FILE *fp, char *pre, const uint16_t dm_addr, char *post);

// ----------------------------------------------------------------
// Debug CSR addresses

extern const uint16_t csr_addr_dcsr;         // Debug control and status
extern const uint16_t csr_addr_dpc;          // Debug PC
extern const uint16_t csr_addr_dscratch0;    // Debug scratch
extern const uint16_t csr_addr_dscratch1;    // Debug scratch

// ================================================================
// Run Control DM register fields

extern
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
			  bool       dmactive);

extern bool     fn_dmcontrol_haltreq         (uint32_t dm_word);
extern bool     fn_dmcontrol_resumereq       (uint32_t dm_word);
extern bool     fn_dmcontrol_hartreset       (uint32_t dm_word);
extern bool     fn_dmcontrol_ackhavereset    (uint32_t dm_word);
extern bool     fn_dmcontrol_hasel           (uint32_t dm_word);
extern uint16_t fn_dmcontrol_hartsello       (uint32_t dm_word);
extern uint16_t fn_dmcontrol_hartselhi       (uint32_t dm_word);
extern bool     fn_dmcontrol_setresethaltreq (uint32_t dm_word);
extern bool     fn_dmcontrol_clrresethaltreq (uint32_t dm_word);
extern bool     fn_dmcontrol_ndmreset        (uint32_t dm_word);
extern bool     fn_dmcontrol_dmactive        (uint32_t dm_word);

extern
void fprint_dmcontrol (FILE *fp, char *pre, uint32_t dmcontrol, char *post);

// ----------------------------------------------------------------
// 'dmstatus' register

#define DMSTATUS_IMPEBREAK        0x00400000

#define DMSTATUS_ALLHAVERESET     0x00080000
#define DMSTATUS_ANYHAVERESET     0x00040000
#define DMSTATUS_ALLRESUMEACK     0x00020000
#define DMSTATUS_ANYRESUMEACK     0x00010000

#define DMSTATUS_ALLNONEXISTENT   0x00008000
#define DMSTATUS_ANYNONEXISTENT   0x00004000
#define DMSTATUS_ALLUNAVAIL       0x00002000
#define DMSTATUS_ANYUNAVAIL       0x00001000

#define DMSTATUS_ALLRUNNING       0x00000800
#define DMSTATUS_ANYRUNNING       0x00000400
#define DMSTATUS_ALLHALTED        0x00000200
#define DMSTATUS_ANYHALTED        0x00000100

#define DMSTATUS_AUTHENTICATED    0x00000080
#define DMSTATUS_AUTHBUSY         0x00000040
#define DMSTATUS_HASRESETHALTREQ  0x00000020
#define DMSTATUS_CONFSTRPTRVALID  0x00000010

#define DMSTATUS_VERSION          0x0000000F

extern void fprint_dmstatus (FILE *fp, char *pre, uint32_t dmstatus, char *post);

// ================================================================
// Abstract Command register fields

// ----------------------------------------------------------------
// 'dm_abstractcs' register

typedef enum {DM_ABSTRACTCS_CMDERR_NONE          = 0,
	      DM_ABSTRACTCS_CMDERR_BUSY          = 1,
	      DM_ABSTRACTCS_CMDERR_NOT_SUPPORTED = 2,
	      DM_ABSTRACTCS_CMDERR_EXCEPTION     = 3,
	      DM_ABSTRACTCS_CMDERR_HALT_RESUME   = 4,
	      DM_ABSTRACTCS_CMDERR_UNDEF5        = 5,
	      DM_ABSTRACTCS_CMDERR_UNDEF6        = 6,
	      DM_ABSTRACTCS_CMDERR_OTHER         = 7
} DM_abstractcs_cmderr;

typedef enum {DM_COMMAND_CMDTYPE_ACCESS_REG   = 0,
	      DM_COMMAND_CMDTYPE_QUICK_ACCESS = 1,
	      DM_COMMAND_CMDTYPE_ACCESS_MEM   = 2
} DM_command_cmdtype;

extern
void fprint_abstractcs_cmderr (FILE *fp, const char *pre, const DM_abstractcs_cmderr cmderr, const char *post);

extern
uint32_t fn_mk_abstractcs (DM_abstractcs_cmderr cmderr);

extern
uint8_t fn_abstractcs_progbufsize (uint32_t dm_word);

extern
bool fn_abstractcs_busy (uint32_t dm_word);

extern
DM_abstractcs_cmderr fn_abstractcs_cmderr (uint32_t dm_word);

extern
uint8_t fn_abstractcs_datacount (uint32_t dm_word);

extern
void fprint_abstractcs (FILE *fp, char *pre, uint32_t abstractcs, char *post);

// ----------------------------------------------------------------
// 'command' register

typedef enum {DM_COMMAND_ACCESS_REG_SIZE_UNDEF0   = 0,
	      DM_COMMAND_ACCESS_REG_SIZE_UNDEF1   = 1,
	      DM_COMMAND_ACCESS_REG_SIZE_LOWER32  = 2,
	      DM_COMMAND_ACCESS_REG_SIZE_LOWER64  = 3,
	      DM_COMMAND_ACCESS_REG_SIZE_LOWER128 = 4,
	      DM_COMMAND_ACCESS_REG_SIZE_UNDEF5   = 5,
	      DM_COMMAND_ACCESS_REG_SIZE_UNDEF6   = 6,
	      DM_COMMAND_ACCESS_REG_SIZE_UNDEF7   = 7
} DM_command_access_reg_size;

extern const uint16_t dm_command_access_reg_regno_csr_0;
extern const uint16_t dm_command_access_reg_regno_csr_FFF;
extern const uint16_t dm_command_access_reg_regno_gpr_0;
extern const uint16_t dm_command_access_reg_regno_gpr_1F;
extern const uint16_t dm_command_access_reg_regno_fpr_0;
extern const uint16_t dm_command_access_reg_regno_fpr_1F;

extern
uint32_t fn_mk_command_access_reg (DM_command_access_reg_size  size,
				   bool                        aarpostincrement,
				   bool                        postexec,
				   bool                        transfer,
				   bool                        write,
				   uint16_t                    regno);

extern DM_command_cmdtype fn_command_cmdtype (uint32_t dm_word);
extern DM_command_access_reg_size fn_command_access_reg_size (uint32_t dm_word);
extern bool     fn_command_access_reg_postincrement (uint32_t dm_word);
extern bool     fn_command_access_reg_postexec      (uint32_t dm_word);
extern bool     fn_command_access_reg_transfer      (uint32_t dm_word);
extern bool     fn_command_access_reg_write         (uint32_t dm_word);
extern uint16_t fn_command_access_reg_regno         (uint32_t dm_word);

extern
void fprint_command (FILE *fp, char *pre, uint32_t command, char *post);

// ================================================================
// System Bus Access DM register fields

typedef enum {DM_SBACCESS_8_BIT   = 0,
	      DM_SBACCESS_16_BIT  = 1,
	      DM_SBACCESS_32_BIT  = 2,
	      DM_SBACCESS_64_BIT  = 3,
	      DM_SBACCESS_128_BIT = 4
} DM_sbaccess;

typedef enum {DM_SBERROR_NONE             = 0,
	      DM_SBERROR_TIMEOUT          = 1,
	      DM_SBERROR_BADADDR          = 2,
	      DM_SBERROR_ALIGNMENT        = 3,
	      DM_SBERROR_UNSUPPORTED_SIZE = 4,
	      DM_SBERROR_UNDEF5           = 5,
	      DM_SBERROR_UNDEF6           = 6,
	      DM_SBERROR_UNDEF7_W1C       = 7     // used in writes, to clear sberror
} DM_sberror;

extern
void fprint_sberror (FILE *fp, const char *pre, const DM_sberror sberror, const char *post);

extern
uint32_t fn_mk_sbcs (bool        sbbusyerror,
		     bool        sbreadonaddr,
		     DM_sbaccess sbaccess,
		     bool        sbautoincrement,
		     bool        sbreadondata,
		     DM_sberror  sberror);

extern uint8_t     fn_sbcs_sbversion       (uint32_t dm_word);
extern bool        fn_sbcs_sbbusyerror     (uint32_t dm_word);
extern bool        fn_sbcs_sbbusy          (uint32_t dm_word);
extern bool        fn_sbcs_sbreadonaddr    (uint32_t dm_word);
extern DM_sbaccess fn_sbcs_sbaccess        (uint32_t dm_word);
extern bool        fn_sbcs_sbautoincrement (uint32_t dm_word);
extern bool        fn_sbcs_sbreadonddata   (uint32_t dm_word);
extern DM_sberror  fn_sbcs_sberror         (uint32_t dm_word);
extern uint8_t     fn_sbcs_sbasize         (uint32_t dm_word);
extern bool        fn_sbcs_sbaccess128     (uint32_t dm_word);
extern bool        fn_sbcs_sbaccess64      (uint32_t dm_word);
extern bool        fn_sbcs_sbaccess32      (uint32_t dm_word);
extern bool        fn_sbcs_sbaccess16      (uint32_t dm_word);
extern bool        fn_sbcs_sbaccess8       (uint32_t dm_word);

extern
void fprint_sbcs (FILE *fp, const char *pre, const uint32_t sbcs, const char *post);

// ================================================================
// DCSR fields

typedef enum {DM_DCSR_XDEBUGVER_NONE      = 0,
	      DM_DCSR_XDEBUGVER_V_0_13    = 4,
	      DM_DCSR_XDEBUGVER_V_UNKNOWN = 15
} DM_DCSR_XDebugVer;

typedef enum {DM_DCSR_CAUSE_RESERVED0 = 0,
	      DM_DCSR_CAUSE_EBREAK    = 1,
	      DM_DCSR_CAUSE_TRIGGER   = 2,
	      DM_DCSR_CAUSE_HALTREQ   = 3,
	      DM_DCSR_CAUSE_STEP      = 4,
	      DM_DCSR_CAUSE_RESERVED5 = 5,
	      DM_DCSR_CAUSE_RESERVED6 = 6,
	      DM_DCSR_CAUSE_RESERVED7 = 7
} DM_DCSR_Cause;

typedef enum {
     DM_DCSR_PRV_USER = 0
   , DM_DCSR_PRV_SUPERVISOR = 1
   , DM_DCSR_PRV_MACHINE = 3
} DM_DCSR_PRV;

extern const uint32_t dcsr_step_bit;

extern
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
		     DM_DCSR_PRV       prv);
		     
extern DM_DCSR_XDebugVer fn_dcsr_xdebugver (uint32_t dm_word);
extern bool              fn_dcsr_ebreakm   (uint32_t dm_word);
extern bool              fn_dcsr_ebreaks   (uint32_t dm_word);
extern bool              fn_dcsr_ebreaku   (uint32_t dm_word);
extern bool              fn_dcsr_stepie    (uint32_t dm_word);
extern bool              fn_dcsr_stopcount (uint32_t dm_word);
extern bool              fn_dcsr_stoptime  (uint32_t dm_word);
extern DM_DCSR_Cause     fn_dcsr_cause     (uint32_t dm_word);
extern bool              fn_dcsr_mprven    (uint32_t dm_word);
extern bool              fn_dcsr_nmip      (uint32_t dm_word);
extern bool              fn_dcsr_step      (uint32_t dm_word);
extern DM_DCSR_PRV       fn_dcsr_prv       (uint32_t dm_word);

extern void fprint_DM_DCSR_Cause (FILE *fp, char *pre, DM_DCSR_Cause  cause, char *post);

extern void fprint_dcsr (FILE *fp, char *pre, uint32_t dcsr, char *post);

// ================================================================
