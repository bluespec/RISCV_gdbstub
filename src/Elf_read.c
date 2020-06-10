// Copyright (c) 2013-2019 Bluespec, Inc. All Rights Reserved

// This program reads an ELF file into an in-memory byte-array.
// This can then sent to a debugger.

// ================================================================
// Standard C includes

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <fcntl.h>
#include <gelf.h>

// ----------------
// Project includes

#include "Elf_read.h"

// ================================================================
// Memory buffer into which we load the ELF file.

// 1 Gigabyte size
// #define MAX_MEM_SIZE (((uint64_t) 0x400) * ((uint64_t) 0x400) * ((uint64_t) 0x400))
#define MAX_MEM_SIZE ((uint64_t) 0x90000000)

static uint8_t mem_buf [MAX_MEM_SIZE];

// ================================================================
// Load an ELF file.
// Return 1 on success, 0 on failure

static
int c_mem_load_elf (FILE          *logfile_fp,
		    const char    *elf_filename,
		    const char    *start_symbol,
		    const char    *exit_symbol,
		    const char    *tohost_symbol,
		    Elf_Features  *p_features)
{
    int fd;
    // int n_initialized = 0;
    Elf *e;

    // Default start, exit and tohost symbols
    if (start_symbol == NULL)
	start_symbol = "_start";
    if (exit_symbol == NULL)
	exit_symbol = "exit";
    if (tohost_symbol == NULL)
	tohost_symbol = "tohost";
    
    // Verify the elf library version
    if (elf_version (EV_CURRENT) == EV_NONE) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "ERROR: c_mem_load_elf: Failed to initialize the libelf library.\n");
	}
	return 0;
    }

    // Open the file for reading
    fd = open (elf_filename, O_RDONLY, 0);
    if (fd < 0) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "ERROR: c_mem_load_elf: could not open elf input file: %s\n",
		     elf_filename);
	}
	return 0;
    }

    // Initialize the Elf pointer with the open file
    e = elf_begin (fd, ELF_C_READ, NULL);
    if (e == NULL) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "ERROR: c_mem_load_elf: elf_begin() initialization failed!\n");
	}
	return 0;
    }

    // Verify that the file is an ELF file
    if (elf_kind (e) != ELF_K_ELF) {
        elf_end (e);
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "ERROR: c_mem_load_elf: specified file '%s' is not an ELF file!\n",
		     elf_filename);
	}
	return 0;
    }

    // Get the ELF header
    GElf_Ehdr ehdr;
    if (gelf_getehdr (e, & ehdr) == NULL) {
        elf_end (e);
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "ERROR: c_mem_load_elf: get_getehdr() failed: %s\n",
		     elf_errmsg(-1));
	}
	return 0;
    }

    // Is this a 32b or 64 ELF?
    if (gelf_getclass (e) == ELFCLASS32) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp, "c_mem_load_elf: %s is a 32-bit ELF file\n", elf_filename);
	}
	p_features->bitwidth = 32;
    }
    else if (gelf_getclass (e) == ELFCLASS64) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp, "c_mem_load_elf: %s is a 64-bit ELF file\n", elf_filename);
	}
	p_features->bitwidth = 64;
    }
    else {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "ERROR: c_mem_load_elf: ELF file '%s' is not 32b or 64b\n",
		     elf_filename);
	}
	elf_end (e);
	return 0;
    }

    // Verify we are dealing with a RISC-V ELF
    if (ehdr.e_machine != 243) {
	// EM_RISCV is not defined, but this returns 243 when used with a valid elf file.
        elf_end (e);
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "ERROR: c_mem_load_elf: %s is not a RISC-V ELF file\n",
		     elf_filename);
	}
	return 0;
    }

    // Verify we are dealing with a little endian ELF
    if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
        elf_end (e);
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp,
		     "ERROR: c_mem_load_elf: %s is big-endian 64-bit RISC-V executable, not supported\n",
		     elf_filename);
	}
	return 0;
    }

    // Grab the string section index
    size_t shstrndx;
    shstrndx = ehdr.e_shstrndx;

    // Iterate through each of the sections looking for code that should be loaded
    Elf_Scn  *scn   = 0;
    GElf_Shdr shdr;

    p_features->min_addr    = 0xFFFFFFFFFFFFFFFFllu;
    p_features->max_addr    = 0x0000000000000000llu;
    p_features->pc_start    = 0xFFFFFFFFFFFFFFFFllu;
    p_features->pc_exit     = 0xFFFFFFFFFFFFFFFFllu;
    p_features->tohost_addr = 0xFFFFFFFFFFFFFFFFllu;

    while ((scn = elf_nextscn (e,scn)) != NULL) {
        // get the header information for this section
        gelf_getshdr (scn, & shdr);

	char *sec_name = elf_strptr (e, shstrndx, shdr.sh_name);
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp, "Section %-16s: ", sec_name);
	}

	Elf_Data *data = 0;
	// If we find a code/data section, load it into the model
	if (   ((shdr.sh_type == SHT_PROGBITS)
		|| (shdr.sh_type == SHT_NOBITS)
		|| (shdr.sh_type == SHT_INIT_ARRAY)
		|| (shdr.sh_type == SHT_FINI_ARRAY))
	    && ((shdr.sh_flags & SHF_WRITE)
		|| (shdr.sh_flags & SHF_ALLOC)
		|| (shdr.sh_flags & SHF_EXECINSTR))) {
	    data = elf_getdata (scn, data);

	    // n_initialized += data->d_size;
	    if (shdr.sh_addr < p_features->min_addr)
		p_features->min_addr = shdr.sh_addr;
	    if (p_features->max_addr < (shdr.sh_addr + data->d_size - 1))   // shdr.sh_size + 4))
		p_features->max_addr = shdr.sh_addr + data->d_size - 1;    // shdr.sh_size + 4;

	    if (p_features->max_addr >= MAX_MEM_SIZE) {
		if (logfile_fp != NULL) {
		    fprintf (logfile_fp,
			     "INTERNAL ERROR: p_features->max_addr (0x%0" PRIx64 ") > buffer size (0x%0" PRIx64 ")\n",
			     p_features->max_addr, MAX_MEM_SIZE);
		    fprintf (logfile_fp,
			     "    Please increase the #define in this program, recompile, and run again\n");
		    fprintf (logfile_fp, "    Abandoning this run\n");
		}
		return 0;
	    }

	    if (shdr.sh_type != SHT_NOBITS) {
		memcpy (& (mem_buf [shdr.sh_addr]), data->d_buf, data->d_size);
	    }
	    if (logfile_fp != NULL) {
		fprintf (logfile_fp, "addr %16" PRIx64 " to addr %16" PRIx64 "; size 0x%8lx (= %0ld) bytes\n",
			 shdr.sh_addr, shdr.sh_addr + data->d_size, data->d_size, data->d_size);
	    }

	}

	// If we find the symbol table, search for symbols of interest
	else if (shdr.sh_type == SHT_SYMTAB) {
	    if (logfile_fp != NULL) {
		fprintf (logfile_fp, "Searching for addresses of '%s', '%s' and '%s' symbols\n",
			 start_symbol, exit_symbol, tohost_symbol);
	    }

 	    // Get the section data
	    data = elf_getdata (scn, data);

	    // Get the number of symbols in this section
	    int symbols = shdr.sh_size / shdr.sh_entsize;

	    // search for the uart_default symbols we need to potentially modify.
	    GElf_Sym sym;
	    int i;
	    for (i = 0; i < symbols; ++i) {
	        // get the symbol data
	        gelf_getsym (data, i, &sym);

		// get the name of the symbol
		char *name = elf_strptr (e, shdr.sh_link, sym.st_name);

		// Look for, and remember PC of the start symbol
		if (strcmp (name, start_symbol) == 0) {
		    p_features->pc_start = sym.st_value;
		}
		// Look for, and remember PC of the exit symbol
		else if (strcmp (name, exit_symbol) == 0) {
		    p_features->pc_exit = sym.st_value;
		}
		// Look for, and remember addr of 'tohost' symbol
		else if (strcmp (name, tohost_symbol) == 0) {
		    p_features->tohost_addr = sym.st_value;
		}
	    }

	    FILE *fp_symbol_table = fopen ("symbol_table.txt", "w");
	    if (fp_symbol_table != NULL) {
		if (logfile_fp != NULL) {
		    fprintf (logfile_fp, "Writing symbols to:    symbol_table.txt\n");
		}
		if (p_features->pc_start == -1)
		    if (logfile_fp != NULL) {
			fprintf (logfile_fp, "    No '_start' label found\n");
		    }
		else
		    fprintf (fp_symbol_table, "_start    0x%0" PRIx64 "\n", p_features->pc_start);

		if (p_features->pc_exit == -1)
		    if (logfile_fp != NULL) {
			fprintf (logfile_fp, "    No 'exit' label found\n");
		    }
		else
		    fprintf (fp_symbol_table, "exit      0x%0" PRIx64 "\n", p_features->pc_exit);

		if (p_features->tohost_addr == -1)
		    if (logfile_fp != NULL) {
			fprintf (logfile_fp, "    No 'tohost' symbol found\n");
		    }
		else
		    fprintf (fp_symbol_table, "tohost    0x%0" PRIx64 "\n", p_features->tohost_addr);

		fclose (fp_symbol_table);
	    }
	}
	else {
	    if (logfile_fp != NULL) {
		fprintf (logfile_fp, "ELF section ignored\n");
	    }
	}
    }

    elf_end (e);

    p_features->mem_buf = & (mem_buf [0]);

    if (logfile_fp != NULL) {
	fprintf (logfile_fp, "Min addr:            %16" PRIx64 " (hex)\n", p_features->min_addr);
	fprintf (logfile_fp, "Max addr:            %16" PRIx64 " (hex)\n", p_features->max_addr);
    }
    return 1;
}

// ================================================================
// Min and max byte addrs for various mem sizes

#define BASE_ADDR_B  0x80000000lu

// For 16 MB memory at 0x_8000_0000
#define MIN_MEM_ADDR_16MB  BASE_ADDR_B
#define MAX_MEM_ADDR_16MB  (BASE_ADDR_B + 0x1000000lu)

// For 256 MB memory at 0x_8000_0000
#define MIN_MEM_ADDR_256MB  BASE_ADDR_B
#define MAX_MEM_ADDR_256MB  (BASE_ADDR_B + 0x10000000lu)

// ================================================================
// Read the ELF file into the array buffer
// Return 1 on success, 0 on failure

int elf_readfile (FILE          *logfile_fp,
		  const  char   *elf_filename,
		  Elf_Features  *p_features)
{
    // Zero out the memory buffer before loading the ELF file
    bzero (mem_buf, MAX_MEM_SIZE);

    int result = c_mem_load_elf (logfile_fp, elf_filename, "_start", "exit", "tohost", p_features);
    if (result == 0)
	return 0;

    if ((p_features->min_addr < BASE_ADDR_B) || (MAX_MEM_ADDR_256MB <= p_features->max_addr)) {
	if (logfile_fp != NULL) {
	    fprintf (logfile_fp, "ERROR: elf_readfile(): addresses out of expected range\n");
	    fprintf (logfile_fp, "    Expected range: 0x%0" PRIx64 " to 0x%0" PRIx64 "\n",
		     BASE_ADDR_B, MAX_MEM_ADDR_256MB);
	    fprintf (logfile_fp, "    Actual   range: 0x%0" PRIx64 " to 0x%0" PRIx64 "\n",
		     p_features->min_addr, p_features->max_addr);
	}

	fprintf (stderr, "ERROR: elf_readfile(): addresses out of expected range\n");
	fprintf (stderr, "    See logfile for details\n");

	return 0;
    }
    return 1;
}

// ================================================================
