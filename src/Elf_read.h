// Copyright (c) 2013-2019 Bluespec, Inc. All Rights Reserved

// This program reads an ELF file into an in-memory byte-array.
// This can then sent to a debugger.

// ================================================================
// Features of the ELF binary

typedef struct {
    char     *mem_buf;
    uint8_t   bitwidth;
    uint64_t  min_addr;
    uint64_t  max_addr;

    uint64_t  pc_start;       // Addr of label  '_start'
    uint64_t  pc_exit;        // Addr of label  'exit'
    uint64_t  tohost_addr;    // Addr of label  'tohost'
} Elf_Features;

// ================================================================
// Read the ELF file into the array buffer
// Return 1 on success, 0 on failure

extern
int elf_readfile (FILE          *logfile_fp,
		  const  char   *elf_filename,
		  Elf_Features  *p_features);

// ================================================================
