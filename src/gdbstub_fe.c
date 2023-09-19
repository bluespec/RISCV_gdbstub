// Copyright (c) 2016-2023 Bluespec, Inc. All Rights Reserved
// Author: Rishiyur Nikhil
//
// ================================================================
// C lib includes

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
#include <poll.h>

// ----------------
// Local includes

#include "gdbstub_be.h"
#include "gdbstub_fe.h"

// ================================================================
// Terminology: In the following, 'RSP' = GDB's Remote Serial Protocol
// ================================================================

//================================================================
// gdbstub globals

static int   gdb_fd;
static int   stop_fd;
static FILE *logfile;
static int   verbosity = 0;

static const char control_C = 0x3;

static bool waiting_for_stop_reason = false;

// ================================================================
// Help functions to print byte strings for debugging.

// Print a byte, using ASCII char if printable, escaped hex code if not

static
void fprint_byte (FILE *fp, const char x)
{
    if ((' ' <= x) && (x <= '~')) {
	fprintf (fp, "%c", x);
	if (x == '\\')
	    fprintf (fp, "\\");
    }
    else
	fprintf (fp, "\\x%02x", x);
}

// Print a string of bytes, using ASCII printables if possible

static
void fprint_bytes (FILE *fp, const char *pre, const char *buf, const size_t buf_len, const char *post)
{
    if (pre != NULL)
	fprintf (fp, "%s", pre);

    size_t j;
    for (j = 0; j < buf_len; j++)
	fprint_byte (fp, buf [j]);

    if (post != NULL)
	fprintf (fp, "%s", post);

    fflush (fp);
}

// Print a packet, treating $X... packets specially.
// $X data bytes are printed only in hex format, and only up to 64 bytes (if verbosity = 0)

static
void fprint_packet (FILE *fp, const char *pre, const char *buf, const size_t buf_len, const char *post)
{
    if ((buf_len >= 2) && (buf [0] == '$') && (buf [1] == 'X')) {
	if (pre != NULL)
	    fprintf (fp, "%s", pre);

	const size_t trailer_len = 3;    // '#nn' at end of packet

	// Print '$X addr, len :'
	size_t j;
	for (j = 0; (j < buf_len); j++) {
	    fprintf (fp, "%c", buf [j]);
	    if (buf [j] == ':') break;
	}

	assert ((j < (buf_len - trailer_len)) && (buf [j] == ':'));
	j++; // Just past the ':'

	size_t jmax = 64;
	if ((verbosity != 0) || ((buf_len - trailer_len - j) < 64))
	    jmax = buf_len - trailer_len;

	for ( ; j < jmax; j++)
	    fprintf (fp, "\\x%02x", buf [j]);
	if (jmax < (buf_len - trailer_len))
	    fprintf (fp, "... (recompile gdbstub_fe.c w. 'verbosity=1' to log all data bytes)");

	// Packet trailer
	fprintf (fp, "%c%c%c", buf [buf_len - 3], buf [buf_len - 2], buf [buf_len - 1]);

	if (post != NULL)
	    fprintf (fp, "%s", post);

	fflush (fp);
    }
    else
	fprint_bytes (fp, pre, buf, buf_len, post);
}

// ================================================================
// GDB RSP packets have '$' as the opening char,
//     a series of payload bytes
//     and "#xx" at the end,
// where xx is the unsigned 8-bit checksum of all actual payload bytes.

// Further, for transmission, if a payload byte happens to be '$',
// '#', '}' or '*', it is 'escaped' into two bytes, '}' followed by
// the original byte XOR'd with 0x20.
// Note: checksums are computed on the escaped bytes.

// Max payload size before bytes are 'escaped'.

#define GDB_RSP_PKT_BUF_MAX   (16384)

// Max payload size after bytes are 'escaped' (what actually goes on the wire).

#define GDB_RSP_WIRE_BUF_MAX  ((GDB_RSP_PKT_BUF_MAX * 2) + 4)

// ================================================================
// Copies GDB RSP chars from src to dst, escaping any chars as necessary.
// Returns the actual number of chars copied into dst, -1 if error.

static
ssize_t gdb_escape (char *dst, const size_t dst_size, const char *src, const size_t src_len)
{
    unsigned char *udst = (unsigned char *) dst;
    const unsigned char *usrc = (const unsigned char *) src;
    size_t js = 0, jd = 0;

    while (js < src_len) {
	unsigned char ch = usrc [js];
	if ((ch == '$') || (ch == '#') || (ch == '*') || (ch == '}')) {
	    if ((jd + 1) >= dst_size)
		goto err_dst_too_small;
	    dst [jd]     = '}';
	    udst [jd + 1] = (ch ^ 0x20);
	    jd += 2;
	}
	else {
	    if (jd >= dst_size)
		goto err_dst_too_small;
	    udst [jd] = ch;
	    jd += 1;
	}
	js++;
    }
    return (ssize_t) jd;

 err_dst_too_small:
    if (logfile) {
	fprintf (logfile, "ERROR: gdbstub_fe.gdb_escape: destination buffer too small\n");
	fprintf (logfile, "    src [src_len %0zu] = \"", src_len);
	size_t j;
	for (j = 0; j < src_len; j++) fprintf (logfile, "%c", src [j]);
	fprintf (logfile, "\"\n");
	fprintf (logfile, "    dst_size = %0zu\n", dst_size);
	fprintf (logfile, "    At src [%0zu], dst [%0zu]\n", js, jd);
    }
    return -1;
}

// ================================================================
// Copies GDB RSP chars from src to dst, un-escaping any escaped chars,
// and appending a final a 0 byte.
// Returns the actual number of chars copied into dst, -1 if error.
//    (includes terminating 0 byte)

static
ssize_t gdb_unescape (char *dst, const size_t dst_size, const char *src, const size_t src_len)
{
    unsigned char *udst = (unsigned char *) dst;
    const unsigned char *usrc = (const unsigned char *) src;
    size_t js = 0, jd = 0;

    while (js < src_len) {
	unsigned char ch;
	if (src [js] == '}') {
	    if ((js + 1) >= src_len)
		goto err_ends_in_escape_char;

	    ch = usrc [js + 1] ^ 0x20;
	    js += 2;
	}
	else {
	    ch = usrc [js];
	    js += 1;
	}

	if (jd >= dst_size)
	    goto err_dst_too_small;
	udst [jd++] = ch;
    }
    // Insert terminating 0 byte
    if ((jd + 1) >= dst_size)
	goto err_dst_too_small;
    dst [jd++] = 0;
    return (ssize_t) jd;

 err_dst_too_small:
    if (logfile) {
	fprintf (logfile, "ERROR: gdbstub_fe.gdb_unescape: destination buffer too small\n");
	fprintf (logfile, "    src [src_len %0zu] = \"", src_len);
	size_t j;
	for (j = 0; j < src_len; j++) fprintf (logfile, "%c", src [j]);
	fprintf (logfile, "\"\n");
	fprintf (logfile, "    dst_size = %0zu\n", dst_size);
	fprintf (logfile, "    At src [%0zu], dst [%0zu]\n", js, jd);
    }
    return -1;

 err_ends_in_escape_char:
    if (logfile) {
	fprintf (logfile, "ERROR: gdbstub_fe.gdb_unescape: last char of src is escape char\n");
	fprintf (logfile, "    src [src_len %0zu] = \"", src_len);
	size_t j;
	for (j = 0; j < src_len; j++) fprintf (logfile, "%c", src [j]);
	fprintf (logfile, "\"\n");
    }
    return -1;
}

// ================================================================
// Compute 8-bit unsigned checksum of chars in a buffer

static
uint8_t gdb_checksum (const char *buf, const size_t size)
{
    uint8_t c = 0;

    size_t j;
    for (j = 0; j < size; j++)
	c = (uint8_t) (c + ((uint8_t *) buf) [j]);

    return c;
}

// ================================================================
// Find the first token (whitespace-delimited) in a buffer
// Return 0 if no token found.
//        n, index of char just after token, if token found
//                (even if the token length is > DEST_MAX-1)
// Return a copy of the token in dest, including a null-termination.
//    (but only up to the the first DEST_MAX-1 chars of the token)

static
size_t find_token (char *dest, const size_t DEST_MAX, const char *src, const size_t src_len)
{
    size_t js = 0;
    size_t jd = 0;

    // Skip leading whitespace if any
    while ((js < src_len)
	   && ((src [js] == ' ') || (src [js] == '\t'))) {
	js++;
    }
    if (js == src_len)
	return 0;    // no token found

    // Token found; copy it
    while ((js < src_len)
	   && ((src [js] != ' ') && (src [js] != '\t'))) {
	if (jd < DEST_MAX + 1) {
	    dest [jd] = src [js];
	    jd++;
	}
	js++;
    }
    dest [jd] = 0;
    return js;
}

// ================================================================
// Send '+' (ack) or '-' (nak) to GDB
// Return 0 if ok, -1 if err

static
int send_ack_nak (char ack_char)
{
    size_t n_iters = 0;
    while (true) {
	ssize_t n = write (gdb_fd, & ack_char, 1);
	if (n < 0) {
	    if (logfile) {
		fprintf (logfile, "ERROR: gdbstub_fe.send_ack_nak: write (ack_char '%c') failed\n", ack_char);
	    }
	    perror (NULL);
	    return -1;
	}
	else if (n == 0) {
	    if (n_iters > 1000000) {
		if (logfile) {
		    fprintf (logfile, "ERROR: gdbstub_fe.send_ack_nak: nothing sent in 1,000,000 write () attempts\n");
		}
		return -1;
	    }
	    usleep (5);
	    n_iters++;
	}
	else {
	    if (logfile) {
		fprintf (logfile, "w %c\n", ack_char);
		fflush (logfile);
	    }
	    return 0;
	}
    }
}

// ================================================================
// Receive '+' (ack) or '-' (nak) from GDB
// Return '+' if ack, '-' if nak, 'E' if err

static
char recv_ack_nak (void)
{
    const size_t n_iters_max = 1000000;
    size_t n_iters = 0;
    char ack_char;
    while (true) {
	ssize_t n = read (gdb_fd, & ack_char, 1);
	if (n < 0) {
	    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
		// Nothing available yet
		if (n_iters > n_iters_max) {
		    if (logfile) {
			fprintf (logfile, "ERROR: gdbstub_fe.recv_ack_nak: nothing received in %0zu read () attempts\n",
				 n_iters_max);
		    }
		    return 'E';
		}
		else {
		    usleep (5);
		    n_iters++;
		}
	    }
	    else {
		if (logfile) {
		    fprintf (logfile, "ERROR: gdbstub_fe.recv_ack_nak: read () failed\n");
		}
		return 'E';
	    }
	}
	else if (n == 0) {
	    if (n_iters > n_iters_max) {
		if (logfile) {
		    fprintf (logfile, "ERROR: gdbstub_fe.recv_ack_nak: nothing received in %0zu read () attempts\n",
			     n_iters_max);
		}
		return 'E';
	    }
	    usleep (5);
	    n_iters++;
	}
	else if ((ack_char == '+') || (ack_char == '-')) {
	    if (logfile) {
		fprintf (logfile, "r %c\n", ack_char);
		fflush (logfile);
	    }
	    return ack_char;
	}
	else {
	    if (logfile) {
		fprintf (logfile, "ERROR: gdbstub_fe.recv_ack_nak: received unexpected char 0x%0x ('%c') \n",
			 ack_char, ack_char);
	    }
	    return 'E';
	}
    }
}

// ================================================================
// Integer value of an ASCII hex digit

static
uint8_t value_of_hex_digit (char ch)
{
    if      ((ch >= 'a') && (ch <= 'f')) return (uint8_t) (ch - 'a' + 10);
    else if ((ch >= 'A') && (ch <= 'F')) return (uint8_t) (ch - 'A' + 10);
    else if ((ch >= '0') && (ch <= '9')) return (uint8_t) (ch - '0');
    else {
	if (logfile) {
	    fprintf (logfile, "ERROR: gdbstub_fe.value_of_hex_digit () argument is not a hex digit\n");
	    fprintf (logfile, "    arg value is: ");
	    fprint_byte (logfile, ch);
	    fprintf (logfile, "\n");
	}
	return 0xFF;
    }
}

// ================================================================
// Convert a value (upto 64-bits) into ASCII hex digits (2 digits per byte)
// in little-endian order.  'buf' must be at least:
//     2 chars if gdbstub_be_word_size_bits =  8
//     4 chars if gdbstub_be_word_size_bits = 16
//     8 chars if gdbstub_be_word_size_bits = 32
//    16 chars if gdbstub_be_word_size_bits = 64

static
const char hexchars[] = "0123456789abcdef";

static
void val_to_hex16 (const uint64_t val, const uint8_t xlen, char *buf)
{
    assert ((xlen == 8)
	    || (xlen == 16)
	    || (xlen == 32)
	    || (xlen == 64));

    buf[0]  = hexchars [(val >>  4) & 0xF];
    buf[1]  = hexchars [(val >>  0) & 0xF];
    if (xlen == 8) return;

    buf[2]  = hexchars [(val >> 12) & 0xF];
    buf[3]  = hexchars [(val >>  8) & 0xF];
    if (xlen == 16) return;

    buf[4]  = hexchars [(val >> 20) & 0xF];
    buf[5]  = hexchars [(val >> 16) & 0xF];
    buf[6]  = hexchars [(val >> 28) & 0xF];
    buf[7]  = hexchars [(val >> 24) & 0xF];
    if (xlen == 32) return;

    buf[8]  = hexchars [(val >> 36) & 0xF];
    buf[9]  = hexchars [(val >> 32) & 0xF];
    buf[10] = hexchars [(val >> 44) & 0xF];
    buf[11] = hexchars [(val >> 40) & 0xF];
    buf[12] = hexchars [(val >> 52) & 0xF];
    buf[13] = hexchars [(val >> 48) & 0xF];
    buf[14] = hexchars [(val >> 60) & 0xF];
    buf[15] = hexchars [(val >> 56) & 0xF];
}

// ================================================================
// Convert ASCII hex digits (2 digits per byte) into a value (upto 64-bits)
// in little-endian order.  'buf' must be at least:
//     2 chars if word_size = 8
//     4 chars if word_size = 16
//     8 chars if word_size = 32
//    16 chars if word_size = 64
// Returns status_ok  - if ok
//         status_err - if error (ASCII char is not a hex digit)

static
uint32_t hex16_to_val (const char *buf, const uint8_t xlen, uint64_t *p_val)
{
    assert ((xlen == 8)
	    || (xlen == 16)
	    || (xlen == 32)
	    || (xlen == 64));

    uint64_t val = 0;
    const size_t num_ASCII_hex_digits = xlen / (8 / 2);

    // Check that they are all ASCII hex digits
    size_t j;
    for (j = 0; j < num_ASCII_hex_digits; j++) {
	if (! isxdigit (buf [j]))
	    return status_err;
    }

    // Convert
    for (j = 0; j < num_ASCII_hex_digits; j += 2) {
	uint64_t val_hi_4_bits = value_of_hex_digit (buf [j]);
	uint64_t val_lo_4_bits = value_of_hex_digit (buf [j + 1]);
	val |= (val_hi_4_bits << ((j * 4) + 4));
	val |= (val_lo_4_bits << (j * 4));
    }

    *p_val = val;
    return status_ok;
}

// ================================================================
// Convert 'len' hex digits (2 per byte) in 'src' into bytes in 'dest'

static
void hex2bin (char *dest, const char *src, size_t len)
{
    uint8_t *udest = (uint8_t *) dest;
    uint8_t x, y;
    int jd = 0;

    size_t js;
    for (js = 0; js < len; js += 2) {
	x = value_of_hex_digit (src [js]);
	y = value_of_hex_digit (src [js + 1]);
	udest [jd] = (uint8_t) ((x << 4) | y);
	jd++;
    }
}

// ================================================================
// Convert 'len' bytes in 'src' into hex digits (2 per byte) in 'dest'

static
void bin2hex (char *dest, const char *src, size_t len)
{
    const uint8_t *usrc = (const uint8_t *) src;
    size_t jd = 0;

    size_t js;
    for (js = 0; js < len; js++) {
	char ch_upper = hexchars [(usrc [js] >> 4) & 0x0F];
	char ch_lower = hexchars [(usrc [js] >> 0) & 0x0F];
	dest [jd]     = ch_upper;
	dest [jd + 1] = ch_lower;
	jd += 2;
    }
}

// ================================================================
// Send a GDB RSP packet to GDB ("$....#xx").
// After sending, get a '+' (ack) or '-' (nak) response from GDB.
// Returns status_ok  - if ok
//         status_err - if error

static
uint32_t send_RSP_packet_to_GDB (const char *buf, const size_t buf_len)
{
    char wire_buf [GDB_RSP_WIRE_BUF_MAX];

    wire_buf [0] = '$';

    // Copy the payload from buf to wire_buf, escaping bytes as necessary
    ssize_t s_wire_len = gdb_escape (& (wire_buf [1]), (GDB_RSP_WIRE_BUF_MAX - 1), buf, buf_len);
    if ((s_wire_len < 0) || ((s_wire_len + 4) >= GDB_RSP_WIRE_BUF_MAX)) {
	if (logfile) {
	    fprintf (logfile, "ERROR: gdbstub_fe.send_RSP_packet_to_GDB: packet too large\n");
	    fprintf (logfile, "    Encoded packet will not fit in wire_buf [%0d]\n", GDB_RSP_WIRE_BUF_MAX);
	}
	goto err_exit;
    }

    size_t wire_len = (size_t) s_wire_len;

    // Compute and insert the checksum
    uint8_t checksum = gdb_checksum (& (wire_buf [1]), wire_len);
    char ckstr [3];
    snprintf (ckstr, sizeof (ckstr), "%02X", checksum);
    wire_buf [wire_len + 1] = '#';
    wire_buf [wire_len + 2] = ckstr [0];
    wire_buf [wire_len + 3] = ckstr [1];

    while (true) {
	// Write the packet out to GDB
	size_t n_sent = 0;
	size_t n_iters = 0;
	while (n_sent < (wire_len + 4)) {
	    ssize_t n = write (gdb_fd, & (wire_buf [n_sent]), (wire_len + 4 - n_sent));
	    if (n < 0) {
		if (logfile) {
		    fprintf (logfile, "ERROR: gdbstub_fe.send_RSP_packet_to_GDB: write (wire_buf) failed\n");
		}
		goto err_exit;
	    }
	    else if (n == 0) {
		if (n_iters > 1000000) {
		    if (logfile) {
			fprintf (logfile,
				 "ERROR: gdbstub_fe.send_RSP_packet_to_GDB: nothing sent in 1,000,000 write () attempts\n");
		    }
		    goto err_exit;
		}
		usleep (5);
		n_iters++;
	    }
	    else {
		n_sent += (size_t) n;
	    }
	}
	// Debug
	if (logfile) {
	    fprint_bytes (logfile, "w ", wire_buf, wire_len + 4, "\n");
	}

	// Receive '+' (ack) or '-' (nak) from GDB
	char ch = recv_ack_nak ();
	if (ch == '+')
	    return status_ok;
	else {
	    if (logfile) {
		fprintf (logfile, "Received nak ('-') from GDB\n");
	    }
	    continue; // goto err_exit;
	}
    }

 err_exit:
    if (logfile) {
	fprintf (logfile, "    buf [buf_len %0zu] = \"", buf_len);
	size_t j;
	for (j = 0; j < buf_len; j++) fprintf (logfile, "%c", buf [j]);
	fprintf (logfile, "\"\n");
    }
    return status_err;
}

// ================================================================
// Receive a GDB RSP packet from GDB ("$....#xx")
// Since packets are of varying length, arrive as characters serially,
// and don't have any length field, a 'read()' command on the file
// descriptor may read a part of, exactly, or more than a packet.
// Thus, we use 'gdb_rsp_pkt_buf' as a 'sliding window' and parse packets
// within that buf.

// Receive a GDB RSP packet in 'buf'
// If we receive a complete packet, respond to GDB with:
//    '+'    if it's a valid packet
//    '-'    otherwise
// Returns
//      n (> 0) = # of chars of valid packet received into 'buf'
//          Excludes leading $ and trailing #nn checksum
//          Includes trailing 0 byte that we add.
//      0 if we have received nothing or an as-yet incomplete packet
//     -1 on error or EOF
//     -2 on stop request

#define DEBUG_recv_RSP_packet_from_GDB false
static
ssize_t recv_RSP_packet_from_GDB (char *buf, const size_t buf_size)
{
    // The sliding window
    static char wire_buf [GDB_RSP_WIRE_BUF_MAX];
    static size_t free_ptr = 0;

    // Invariant: all chars [0..] are relevant.
    // Established by moving relevant chars down to [0..] before returning.
    // Specifically, [0] contains the '$' of the next packet.

    ssize_t n;

    fd_set rfds, wfds, efds;

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);

    FD_SET(gdb_fd, &rfds);
    int fd_max = gdb_fd;
    if (stop_fd > 0) {
	FD_SET(stop_fd, &rfds);
	if (stop_fd > fd_max) {
	    fd_max = stop_fd;
	}
    }

    int timeout = 1; // ms
    struct timeval tv;
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;

    if (select(fd_max + 1, &rfds, &wfds, &efds, &tv) > 0) {
	if (stop_fd >= 0 && FD_ISSET(stop_fd, &rfds)) {
	    return -2;
	}
	n = read (gdb_fd, & (wire_buf [free_ptr]), (GDB_RSP_WIRE_BUF_MAX - free_ptr));
	if (n < 0) {
	    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
		// Nothing available
	    }
	    else {
		if (logfile) {
		    fprintf (logfile, "ERROR: gdbstub_fe.recv_RSP_packet_from_GDB: read () failed\n");
		}
		return -1;
	    }
	}
	else if (n == 0) {
	    // eof
	    if (logfile) {
		fprintf (logfile, "recv_RSP_packet_from_GDB: read () ==> EOF\n");
	    }
	    return -1;
	}
	else {
	    free_ptr += (size_t) n;
	}
    }

    // Scan for the starting '$' of the packet, or ^C
    size_t start = 0;
    while ((wire_buf [start] != '$') && (wire_buf [start] != control_C) && (start < free_ptr)) {
	start++;
    }

    if (DEBUG_recv_RSP_packet_from_GDB && logfile) {
	fprintf (logfile,
		"recv_RSP_packet_from_GDB:DBG: free_ptr=%zu, n=%zd, start=%zu\n",
		free_ptr, n, start);
    }

    // discard garbage before packet, if any
    if (start != 0) {
	if (logfile) {
	    fprintf (logfile, "WARNING: gdbstub_fe.recv_RSP_packet_from_GDB: %0zu junk chars before '$'; ignoring:\n",
		     start);
	    fprint_bytes (logfile, "    [", wire_buf, start, "]\n");
	}

	memmove (wire_buf, & (wire_buf [start]), free_ptr - start);
	free_ptr -= start;
    }

    if (free_ptr == 0) {
	// no '$' or '^C' found
	return 0;
    }

    // Debug:
    if (DEBUG_recv_RSP_packet_from_GDB && logfile) {
	fprint_bytes (logfile, "recv_RSP_packet_from_GDB:DBG: ", wire_buf, (free_ptr-1), "\n");
    }
    // Check for ^C
    if (wire_buf [0] == control_C) {
	if (buf_size < 2) {
	    if (logfile) {
		fprintf (logfile, "ERROR: gdbstub_fe.recv_RSP_packet_from_GDB: buf_size too small: %0zu\n", buf_size);
	    }
	    return -1;
	}

	// Debug:
	if (logfile) {
	    fprintf (logfile, "r \\x%02x\n", control_C);
	    if (DEBUG_recv_RSP_packet_from_GDB) {
		fprintf (logfile, "recv_RSP_packet_from_GDB: returning ctrl+c\n");
	    }
	}

	// Discard the packet
	memmove (wire_buf, & (wire_buf [1]), (free_ptr - 1));
	free_ptr--;

	buf [0] = control_C;
	buf [1] = 0;
	return 1;
    }

    // assert (wire_buf [0]  == '$');

    // Scan for the ending '#' of the packet from [1] onwards
    size_t end = 1;
    while (wire_buf [end] != '#') {
	if (end == (free_ptr - 1))
	    return 0;
	end++;
    }
    // assert (wire_buf [end]  == '#');

    // Check if we've received the two checksum chars after '#'
    if ((free_ptr - end) < 3) {
	// not yet
	return 0;
    }

    // We've received a complete packet
    // We will send either a '+' or a '-' acknowledgement.

    // Debug:
    if (logfile) {
	fprint_packet (logfile, "r ", wire_buf, end + 3, "\n");
    }

    // Compute the checksum of the received chars
    uint8_t computed_checksum = gdb_checksum (& (wire_buf [1]), (end - 1));

    // Decode the received checksum
    char ckstr[3] = {(char) wire_buf [end + 1], (char) wire_buf [end + 2], 0};
    uint8_t received_checksum = (uint8_t) strtoul (ckstr, NULL, 16);

    char ack_char;

    ssize_t ret;    // final return value
    if (computed_checksum != received_checksum) {
	// checksum failed
	ack_char = '-';
	ret = -1;
	if (logfile) {
	    fprintf (logfile,
		     "ERROR: gdbstub_fe.recv_RSP_packet_from_GDB: computed checksum 0x%02x; received checksum 0x%02x\n",
		     computed_checksum,
		     received_checksum);
	}
    }
    else {
	// checksum passed
	ack_char = '+';
	// Copy contents to output buf, unescaping as necessary
	ret = gdb_unescape (buf, buf_size, & (wire_buf [1]), (end - 1));
    }

    n = send_ack_nak (ack_char);
    if (n < 0)
	ret = -1;

    // Discard the packet
    memmove (wire_buf, & (wire_buf [end]), (free_ptr - (end + 3)));
    free_ptr -= (end + 3);

    return ret;
}

// ****************************************************************
// ****************************************************************
// The following are handlers for each of the GDB RSP commands received.
// They are listed in alphabetic (ASCII) order of RSP commands.
// ****************************************************************
// ****************************************************************

// ================================================================
// Send "OK" or "ENN" response (NN = status) to GDB

static
void send_OK_or_error_response (uint32_t status)
{
    if (status == status_ok) {
	send_RSP_packet_to_GDB ("OK", 2);
    }
    else {
	char response [4];
	snprintf (response, 4, "E%02x", status);
	send_RSP_packet_to_GDB (response, 3);
    }
}

// ================================================================
// Send a stop-reason response packet to GDB

static
void send_stop_reason (const uint8_t stop_reason)
{
    char response [8];
    snprintf (response, 8, "T%02x", stop_reason);
    send_RSP_packet_to_GDB (response, strlen (response));
}

// ================================================================
// '^C': respond to '^C' received from GDB (interrupt)

static
void handle_RSP_control_C (const char *buf, const size_t buf_len)
{
    uint32_t status = gdbstub_be_stop (gdbstub_be_xlen);
    if (status != status_ok) {
	send_OK_or_error_response (status_err);
	return;
    }
    waiting_for_stop_reason = true;
}

// ================================================================
// '?': Respond to '$?#xx' packet received from GDB (query stop-reason)

static
void handle_RSP_stop_reason (const char *buf, const size_t buf_len)
{
    uint8_t stop_reason;
    int32_t sr = gdbstub_be_get_stop_reason (gdbstub_be_xlen, & stop_reason, false);
    if (sr == 0) {
        send_stop_reason (stop_reason);
        waiting_for_stop_reason = false;
        return;
    }
    else if (sr == -1) {
        send_OK_or_error_response (status_err);
        waiting_for_stop_reason = false;
        return;
    }
    else {
        // HW has not stopped yet
        assert (sr == -2);
        waiting_for_stop_reason = true;
        return;
    }
}

// ================================================================
// 'c': respond to '$c [addr]' packet received from GDB (continue)
// addr is resume-PC, and is optional; if missing, resume from current PC

static
void handle_RSP_c_continue (const char *buf, const size_t buf_len)
{
    uint32_t  status;
    uint64_t  PC_val;

    // "c" (no addr given)
    if (0 == strcmp ("c", buf)) {
    }
    // "c addr": parse the PC value
    else if (1 == sscanf (buf, "c%" SCNx64 "", & PC_val)) {
	gdbstub_be_PC_write (gdbstub_be_xlen, PC_val);
    }
    else {
	// Neither "c" nor "c addr"
	send_OK_or_error_response (status_err);
	return;
    }

    // Send 'continue' command to HW side
    status = gdbstub_be_continue (gdbstub_be_xlen);
    if (status != status_ok) {
	send_OK_or_error_response (status);
	return;
    }

    // Go into 'waiting for stop-reason' mode
    waiting_for_stop_reason = true;
}

// ================================================================
// 'D': respond to '$Dxx' packet received from GDB (shutdown)

static
void handle_RSP_shutdown (const char *buf, const size_t buf_len)
{
    uint32_t status = gdbstub_be_final (gdbstub_be_xlen);
    send_OK_or_error_response (status);
}

// ================================================================
// 'g': respond to '$g' packet received from GDB (read all regs)

// Returns RISC-V regs in the following sequence:
//     GPRs:  0..0x1F
//     PC:   0x20
//     FPRs: 0x21..0x40

static
void handle_RSP_g_read_all_registers (const char *buf, const size_t buf_len)
{
    uint32_t     status;
    uint64_t     value;
    char         response [33 * 16];
    const size_t num_ASCII_hex_digits = gdbstub_be_xlen / (8 / 2);

    // GPRs
    uint8_t j;
    for (j = 0; j < 32; j++) {
	status = gdbstub_be_GPR_read (gdbstub_be_xlen, j, & value);
	if (status != status_ok) {
	    send_OK_or_error_response (status_err);
	    return;
	}
	val_to_hex16 (value, gdbstub_be_xlen, & (response [j * num_ASCII_hex_digits]));
    }

    // PC
    status = gdbstub_be_PC_read (gdbstub_be_xlen, & value);
    if (status != status_ok) {
	send_OK_or_error_response (status_err);
	return;
    }
    val_to_hex16 (value, gdbstub_be_xlen, & (response [32 * num_ASCII_hex_digits]));

    // TODO: FPRs

    // Send assembled response
    send_RSP_packet_to_GDB (response, 33 * num_ASCII_hex_digits);
}

// ================================================================
// 'G': respond to '$Gxx...' packet received from GDB (write all regs)

// 'xx...' contains RISC-V reg values in the following sequence:
//     GPRs:  0..0x1F
//     PC:   0x20
//     FPRs: 0x21..0x40

static
void handle_RSP_G_write_all_registers (const char *buf, const size_t buf_len)
{
    uint32_t  status;
    uint64_t  GPR_vals [32];
    uint64_t  PC_val;
    // TODO
    //uint64_t  FPR_vals [32];
    //uint64_t  FSR_val;
    const size_t num_ASCII_hex_digits = gdbstub_be_xlen / (8 / 2);

    // Check that the packet has the right number of hex digits for all the regs
    if (buf_len != 33 * num_ASCII_hex_digits) {
	if (logfile) {
	    fprintf (logfile, "ERROR: gdbstub_fe.handle_RSP_G_write_all_registers (): invalid buf_len (%0zu)\n", buf_len);
	    fprintf (logfile, "    Expecting exactly 33 x %0zu hex digits\n", num_ASCII_hex_digits);
	}
	goto error_response;
    }

    // Parse all the GPR values
    uint8_t j;
    for (j = 0; j < 32; j++) {
	status = hex16_to_val (& (buf [j * num_ASCII_hex_digits]), gdbstub_be_xlen, & (GPR_vals [j]));
	if (status != status_ok) {
	    if (logfile) {
		fprintf (logfile, "ERROR: gdbstub_fe.handle_RSP_G_write_all_registers (): error parsing val for reg %0u\n",
			 j);
	    }
	    goto error_response;
	}
    }

    // Parse the PC value
    status = hex16_to_val (& (buf [32 * num_ASCII_hex_digits]), gdbstub_be_xlen, & PC_val);
    if (status != status_ok) {
	if (logfile) {
	    fprintf (logfile, "ERROR: gdbstub_fe.handle_RSP_G_write_all_registers (): error parsing val for PC\n");
	}
	goto error_response;
    }

    // Write GPRs to HW
    for (j = 0; j < 32; j++) {
	status = gdbstub_be_GPR_write (gdbstub_be_xlen, j, GPR_vals [j]);
	if (status != status_ok) {
	    if (logfile) {
		fprintf (logfile, "ERROR: gdbstub_fe.handle_RSP_G_write_all_registers (): error writing val for reg %0u\n",
			 j);
	    }
	    goto error_response;
	}
    }

    // Write PC to HW
    status = gdbstub_be_PC_write (gdbstub_be_xlen, PC_val);
    if (status != status_ok) {
	if (logfile) {
	    fprintf (logfile, "ERROR: gdbstub_fe.handle_RSP_G_write_all_registers (): error writing val for PC\n");
	}
	goto error_response;
    }

    // TODO: FPRs

    // All ok, send OK response
    send_OK_or_error_response (status_ok);

 error_response:
    if (logfile) {
	fprint_bytes (logfile, "    buf: ", buf, buf_len-1, "\n");
    }
    send_OK_or_error_response (status_err);
    return;
}

// ================================================================
// 'm': respond to '$m addr, len #xx' packet received from GDB (read memory)

static
void handle_RSP_m_read_mem (const char *buf, const size_t buf_len)
{
    // Parse the addr and len in the RSP command
    uint64_t addr;
    size_t length;

    if (2 != sscanf (buf, "m%" SCNx64 ",%zx", & addr, & length)) {
	if (logfile) {
	    fprintf (logfile, "ERROR: gdbstub_fe.packet '$m...' packet from GDB: unable to parse addr, len\n");
	}
	send_OK_or_error_response (status_err);
	return;
    }

    // Truncate length to what will fit in our RSP buffers
    if ((length * 2) >= GDB_RSP_PKT_BUF_MAX) {
	length = (GDB_RSP_PKT_BUF_MAX - 1) / 2;
    }

    char buf_bin [GDB_RSP_PKT_BUF_MAX / 2];

    // Get memory data from HW
    uint32_t status = gdbstub_be_mem_read (gdbstub_be_xlen, addr, buf_bin, length);
    if (status != status_ok) {
	if (logfile) {
	    fprintf (logfile, "ERROR: gdbstub_fe.packet '$m...' packet from GDB: error reading HW memory\n");
	}
	send_OK_or_error_response (status_err);
	return;
    }

    // Encode bytes into hex chars
    char response [GDB_RSP_PKT_BUF_MAX];
    bin2hex (response, buf_bin, length);

    // Send response to GDB
    send_RSP_packet_to_GDB (response, length * 2);
}

// ================================================================
// 'M': respond to '$M addr, len:XX...#xx' packet received from GDB (write mem, hex data)

static void
handle_RSP_M_write_mem_hex_data (const char *buf, const size_t buf_len)
{
    // Parse the addr and len in the RSP command
    uint64_t addr;
    size_t length;

    if (2 != sscanf (buf, "M%" SCNx64 ",%zx", & addr, & length)) {
	if (logfile) {
	    fprintf (logfile, "ERROR: gdbstub_fe: packet '$M...' packet from GDB: unable to parse addr, len\n");
	}
	send_OK_or_error_response (status_err);
	return;
    }

    // Find ':' separating length from bin data
    char *p = memchr (buf, ':', buf_len);
    if (p == NULL) {
	if (logfile) {
	    fprintf (logfile, "ERROR: gdbstub_fe: packet '$M addr, len ...' packet from GDB: no ':' following len\n");
	    fprintf (logfile, "    addr = 0x%0" PRIx64 ", len = 0x%zu\n", addr, length);
	}
	send_OK_or_error_response (status_err);
	return;
    }

    // Check that it has the correct number of hex digits
    size_t num_hex_data_digits = (buf_len - 1) - ((size_t) ((p + 1) - buf));
    if (num_hex_data_digits != (length * 2)) {
	if (logfile) {
	    fprintf (logfile,
		     "ERROR: gdbstub_fe.packet '$M addr, len: ...' packet from GDB: fewer than (len*2) hex digits\n");
	    fprintf (logfile, "    addr = 0x%0" PRIx64 ", len = 0x%zu\n", addr, length);
	    fprintf (logfile, "    # of hex data digits = %0zu; len * 2 = 0x%zu\n",
		     num_hex_data_digits, length * 2);
	}
	send_OK_or_error_response (status_err);
	return;
    }

    // Convert from hex data digits to binary data
    char buf_bin [GDB_RSP_PKT_BUF_MAX];
    hex2bin (buf_bin, (p + 1), length * 2);

    // Write the data to the HW side
    uint32_t status = gdbstub_be_mem_write (gdbstub_be_xlen, addr, buf_bin, length);
    send_OK_or_error_response (status);
}

// ================================================================
// 'p': respond to '$p n#xx' packet received from GDB (read register n)
// Note: n = 0x00..0x1F        for GPRs
//       n = 0x20              for PC
//       n = 0x21..0x40        for FPRs
//       n = 0x41..0x41+0xFFF  for CSRs
//       n = 0x1014            for PRIV

static
void handle_RSP_p_read_register (const char *buf, const size_t buf_len)
{
    uint32_t  regnum;
    uint64_t  value;
    char      response [16];
    const size_t num_ASCII_hex_digits = gdbstub_be_xlen / (8 / 2);

    if (1 != sscanf (buf, "p%x", & regnum)) {
	send_OK_or_error_response (status_err);
	return;
    }

    if (regnum < 0x20) {
	uint8_t gprnum = (uint8_t) regnum;
	uint32_t status = gdbstub_be_GPR_read (gdbstub_be_xlen, gprnum, & value);
	if (status != status_ok) {
	    send_OK_or_error_response (status_err);
	    return;
	}
    }
    else if (regnum == 0x20) {
	uint32_t status = gdbstub_be_PC_read (gdbstub_be_xlen, & value);
	if (status != status_ok) {
	    send_OK_or_error_response (status_err);
	    return;
	}
    }

    else if ((0x21 <= regnum) && (regnum <= 0x40)) {
	uint8_t fprnum = (uint8_t) (regnum - 0x21);
	uint32_t status = gdbstub_be_FPR_read (gdbstub_be_xlen, fprnum, & value);
	if (status != status_ok) {
	    send_OK_or_error_response (status_err);
	    return;
	}
    }

    else if ((0x41 <= regnum) && (regnum <= (0x41 + 0xFFF))) {
	uint16_t csr_addr = (uint16_t) (regnum - 0x41);
	uint32_t status = gdbstub_be_CSR_read (gdbstub_be_xlen, csr_addr, & value);
	if (status != status_ok) {
	    send_OK_or_error_response (status_err);
	    return;
	}
    }
    else if (regnum == 0x1041) {
	uint32_t status = gdbstub_be_PRIV_read (gdbstub_be_xlen, & value);
	if (status != status_ok) {
	    send_OK_or_error_response (status_err);
	    return;
	}
    }
    else {
	if (logfile) {
	    fprintf (logfile, "ERROR: gdbstub_fe.handle_RSP_p_read_register: unknown reg number: 0x%0x\n",
		     regnum);
	}
	send_OK_or_error_response (status_err);
	return;
    }

    val_to_hex16 (value, gdbstub_be_xlen, response);

    send_RSP_packet_to_GDB (response, num_ASCII_hex_digits);
}

// ================================================================
// 'P': respond to '$P n = r#xx' packet received from GDB (write register n with value r)
// Note: n = 0x00..0x1F        for GPRs
//       n = 0x20              for PC
//       n = 0x21..0x40        for FPRs
//       n = 0x41..0x41+0xFFF  for CSRs

static
void handle_RSP_P_write_register (const char *buf, const size_t buf_len)
{
    uint32_t status;
    uint32_t regnum;
    uint64_t regval;

    // Parse the regnum
    if (1 != sscanf (buf, "P%x", & regnum)) {
	if (logfile) {
	    fprintf (logfile, "ERROR: gdbstub_fe.handle_RSP_P_write_register (): error parsing register num\n");
	}
	status = 0x01;
	goto done;
    }

    // Find and skip past '='
    char *p = memchr (buf, '=', buf_len);
    if (p == NULL) {
	if (logfile) {
	    fprintf (logfile, "ERROR: gdbstub_fe.handle_RSP_P_write_register (): no '=' after register num\n");
	}
	status = 0x01;
	goto done;
    }
    p++;

    uint8_t reglen = gdbstub_be_xlen;
    // PRIV is a virtual 1-byte register
    if (regnum == 0x1041)
	reglen = 8;

    // Parse the register value
    status = hex16_to_val (p, reglen, & regval);
    if (status != status_ok) {
	if (logfile) {
	    fprintf (logfile, "ERROR: gdbstub_fe.handle_RSP_P_write_register (): error parsing value for register %0d\n",
		     regnum);
	}
	status = 0x01;
	goto done;
    }

    // Write the register
    if (regnum < 0x20) {
	uint8_t gprnum = (uint8_t) regnum;
	status = gdbstub_be_GPR_write (gdbstub_be_xlen, gprnum, regval);
    }
    else if (regnum == 0x20) {
	status = gdbstub_be_PC_write (gdbstub_be_xlen, regval);
    }
    else if ((0x21 <= regnum) && (regnum <= 0x40)) {
	uint8_t fprnum = (uint8_t) (regnum - 0x21);
	status = gdbstub_be_FPR_write (gdbstub_be_xlen, fprnum, regval);
    }
    else if ((0x41 <= regnum) && (regnum <= (0x41 + 0xFFF))) {
	uint16_t csr_addr = (uint16_t) (regnum - 0x41);
	status = gdbstub_be_CSR_write (gdbstub_be_xlen, csr_addr, regval);
    }
    else if (regnum == 0x1041) {
	status = gdbstub_be_PRIV_write (gdbstub_be_xlen, regval);
    }
    else
	status = 01;

 done:
    if ((status != status_ok) && logfile) {
	fprintf (logfile, "ERROR: gdbstub_fe.handle_RSP_P_write_register: gdbstub_be write error\n");
	fprintf (logfile, "    regnum 0x%0x, regval 0x%0" PRIx64 "\n", regnum, regval);
    }

    send_OK_or_error_response (status);
}

// ================================================================
// 'q': respond to '$q...#xx' packet received from GDB (general query)
// These are expressed as 'monitor' commands in GDB.

#define WORD_MAX 128

static void
handle_RSP_qRcmd (const char *buf, const size_t buf_len)
{
    uint32_t status = status_ok;
    char response [GDB_RSP_PKT_BUF_MAX];

    char cmd [WORD_MAX];
    size_t n = find_token (cmd, WORD_MAX, buf, buf_len);

    if (n == 0)
	status = status_err;

    else if (strcmp (cmd, "help") == 0) {
	const char *msg = gdbstub_be_help ();
	response [0] = 'O';
	size_t len = strlen (msg);
	bin2hex (& (response [1]), msg, len);
	send_RSP_packet_to_GDB (response, 1 + (2 * len));
	status = status_ok;
    }
    else if (strcmp (cmd, "verbosity") == 0) {
	uint32_t verbosity;
	int m = sscanf (& (buf [n]), "%" SCNu32, & verbosity);
	if (m != 1)
	    status = status_err;
	else
	    status = gdbstub_be_verbosity (verbosity);
    }
    else if (strcmp (cmd, "xlen") == 0) {
	uint8_t xlen;
	int m = sscanf (& (buf [n]), "%" SCNu8, & xlen);
	if (m != 1)
	    status = status_err;
	else {
	    gdbstub_be_xlen = xlen;
	    status = status_ok;
	}
    }
    else if (strcmp (cmd, "reset_dm") == 0) {
	status = gdbstub_be_dm_reset (gdbstub_be_xlen);
    }
    else if (strcmp (cmd, "reset_ndm") == 0) {
	bool haltreq = true;    // TODO: arg to reset_ndm?
	status = gdbstub_be_ndm_reset (gdbstub_be_xlen, haltreq);
    }
    else if (strcmp (cmd, "reset_hart") == 0) {
	bool haltreq = true;    // TODO: arg to reset_ndm?
	status = gdbstub_be_hart_reset (gdbstub_be_xlen, haltreq);
    }
    else if (strcmp (cmd, "elf_load") == 0) {
	status = gdbstub_be_elf_load (& (buf [n]));
    }

    else {
	// Unrecognized command
	// if (logfile) {
	//     fprintf (logfile, "Monitor command not recognized\n");
	// }
	send_RSP_packet_to_GDB ("", 0);
	return;
    }

    // ----------------
    // Final response for the qRcmd command
    if (status == status_ok) {
	// Ok
	send_OK_or_error_response (status_ok);
    }
    else {
	// Packet format error
	send_OK_or_error_response (status_err);
    }
}

static void
handle_RSP_q (const char *buf, const size_t buf_len)
{
    if (strncmp ("qAttached", buf, strlen ("qAttached")) == 0) {
	char response [] = "1";    // i.e., gdbstub is attached to an existing process
	send_RSP_packet_to_GDB (response, strlen (response));
    }

    else if (strncmp ("qSupported", buf, strlen("qSupported")) == 0) {
	char response [32];
	snprintf (response, 32, "PacketSize=%x", GDB_RSP_PKT_BUF_MAX);
	send_RSP_packet_to_GDB (response, strlen (response));
    }

    else if (strncmp ("qRcmd,", buf, strlen ("qRcmd,")) == 0) {
	// This is the RSP packet for 'monitor' commands
	// Convert from hex data digits to binary data
	size_t n1 = strlen ("qRcmd,");
	size_t n2 = (buf_len - 1) - n1;    // Note: buf_len includes terminating 0 byte
	size_t n3 = n2 / 2;
	assert ((n2 & 0x1) == 0);    // Even number of hex digits (ASCII codes)
	char buf_bin [GDB_RSP_PKT_BUF_MAX];
	const char *p = & (buf [n1]);
	hex2bin (buf_bin, p, n2);
	buf_bin [n3] = 0;

	handle_RSP_qRcmd (buf_bin, n3);
    }

    else {
	if (logfile) {
	    fprintf (logfile, "WARNING: gdbstub_fe.handle_RSP_q: Unrecognized packet (%0zu chars): ", buf_len - 1);
	    fprint_bytes (logfile, "", buf, buf_len - 1, "\n");
	}

	char response [] = "";
	send_RSP_packet_to_GDB (response, strlen (response));
    }
}

// ================================================================
// 's': respond to '$s [addr]' packet received from GDB (step)
// addr is resume-PC, and is optional; if missing, resume from current PC

static
void handle_RSP_s_step (const char *buf, const size_t buf_len)
{
    uint32_t  status;
    uint64_t  PC_val;

    // "s" (no addr given)
    if (0 == strcmp ("s", buf)) {
	/* DELETE
	// Read the current PC from the HW
	status = gdbstub_be_read_PC (& PC);
	if (status != status_ok) {
	    send_OK_or_error_response (status);
	    return;
	}
	*/
    }
    // "s addr": parse the PC value
    else if (1 == sscanf (buf, "s%" SCNx64 "", & PC_val)) {
	gdbstub_be_PC_write (gdbstub_be_xlen, PC_val);
    }
    else {
	// Neither "s" nor "s addr"
	send_OK_or_error_response (status_err);
	return;
    }

    // Send 'step' command to HW side
    status = gdbstub_be_step (gdbstub_be_xlen);
    if (status != status_ok) {
	send_OK_or_error_response (status);
	return;
    }

    // Go into 'waiting for stop-reason' mode
    waiting_for_stop_reason = true;
}

// ================================================================
// 'X': respond to '$X addr, len:XX...#xx' packet received from GDB (write mem, binary data)

static void
handle_RSP_X_write_mem_bin_data (const char *buf, const size_t buf_len)
{
    // Parse the addr and len in the RSP command
    uint64_t addr, length;

    if (2 != sscanf (buf, "X%" SCNx64 ",%" SCNx64 "", & addr, & length)) {
	if (logfile) {
	    fprintf (logfile, "ERROR: gdbstub_fe.packet '$X...' packet from GDB: unable to parse addr, len\n");
	}
	send_OK_or_error_response (status_err);
	return;
    }

    // Find ':' separating length from bin data
    char *p = memchr (buf, ':', buf_len);
    if (p == NULL) {
	if (logfile) {
	    fprintf (logfile, "ERROR: gdbstub_fe.packet '$X addr, len ...' packet from GDB: no ':' following len\n");
	    fprintf (logfile, "    addr = 0x%0" PRIx64 ", len = 0x%0" PRIx64 "\n", addr, length);
	}
	send_OK_or_error_response (status_err);
	return;
    }
    // Check that packet has 'length' data bytes
    size_t num_bin_data_bytes = (buf_len - 1) - ((size_t) ((p + 1) - buf));
    if (num_bin_data_bytes != length) {
	if (logfile) {
	    fprintf (logfile,
		     "ERROR: gdbstub_fe.packet '$X addr, len: ...' packet from GDB: fewer than len binary data bytes\n");
	    fprintf (logfile, "    addr = 0x%0" PRIx64 ", len = 0x%0" PRIx64 "\n", addr, length);
	    fprintf (logfile, "    # of binary data data bytes = %0zu\n", num_bin_data_bytes);
	}
	send_OK_or_error_response (status_err);
	return;
    }

    // Write the data to the HW side
    uint32_t status = gdbstub_be_mem_write (gdbstub_be_xlen, addr, (p + 1), length);
    send_OK_or_error_response (status);
}

// ================================================================
// Main loop. This is just called once,
// The void *result and void *arg allow this to be passed into
// pthread_create() so it can be run as a separate thread.
// 'arg' should be a pointer to a Lib_Gdbstub_Params struct.

void *main_gdbstub (void *arg)
{
    Gdbstub_FE_Params *params = (Gdbstub_FE_Params *) arg;
    logfile = params->logfile;
    gdb_fd  = params->gdb_fd;
    stop_fd = params->stop_fd;

    if (logfile) {
	fprintf (logfile, "main_gdbstub: for RV%0d\n", gdbstub_be_xlen);
    }
    if ((gdbstub_be_xlen != 32) && (gdbstub_be_xlen != 64)) {
	if (logfile) {
	    fprintf (logfile, "ERROR: gdbstub_fe.main_gdbstub: invalid RVnn; nn should be 32 or 64 only\n");
	}
	goto done;
    }

    char gdb_rsp_pkt_buf [GDB_RSP_PKT_BUF_MAX];

    if (logfile) {
	fprintf (logfile, "gdbstub v2.0\n");
	fflush (logfile);
    }

    // Initialize the gdbstub_be (we own logfile)
    uint32_t status = gdbstub_be_init (logfile, false);
    if (status != status_ok) {
	if (logfile) {
	    fprintf (logfile, "ERROR: gdbstub_fe.main_gdbstub: error in gdbstub_be_startup\n");
	}
	goto done;
    }

    // Receive initial '+' from GDB
    char ch = recv_ack_nak ();
    if (ch != '+') {
	if (logfile) {
	    fprintf (logfile, "ERROR: gdbstub_fe.main_gdbstub: Expecting initial '+', but received %c from GDB\n", ch);
	}
	goto done;
    }

    // Loop, processing packets from GDB
    while (true) {
	// If waiting for stop-reason, poll for stop-reason
	if (waiting_for_stop_reason) {
            // Moved the sleep up here before the first stop_reason query to
            // give enough time for the continue command to start the CPU
            usleep (10);
	    uint8_t stop_reason;
	    int sr = gdbstub_be_get_stop_reason (gdbstub_be_xlen, & stop_reason, true);
	    if (sr == 0) {
		send_stop_reason (stop_reason);
		waiting_for_stop_reason = false;
	    }
	    else if (sr == -1) {
                // Timeout - interrupt the CPU. Send a "stop" command to the
                // CPU.
                uint32_t status = gdbstub_be_stop (gdbstub_be_xlen);
                if (status != status_ok) {
                    send_OK_or_error_response (status_err);
		    waiting_for_stop_reason = false;
                }
		// send_OK_or_error_response (status_err);
		// waiting_for_stop_reason = false;
	    }
	    else {
		// HW has not stopped yet
		assert (sr == -2);
		// if (logfile) {
		//     fprintf (logfile, "main_gdbstub: HW has not stopped yet.\n");
		// }
	    }
	}

	// Receive RSP packet from GDB and despatch to appropriate handler
	ssize_t sn = recv_RSP_packet_from_GDB (gdb_rsp_pkt_buf, GDB_RSP_PKT_BUF_MAX);

	if (sn == -2) {
	    if (logfile) {
		fprintf (logfile, "gdbstub_fe.main_gdbstub: stopping as requested\n");
	    }
	    break;
	} else if (sn < 0) {
	    if (logfile) {
		fprintf (logfile, "ERROR: gdbstub_fe.on RSP Packet from GDB\n");
	    }
            break;
        }
        else if (sn == 0) {
	    // Complete packet not yet arrived from GDB
	    // if (logfile) {
	    //     fprintf (logfile, "Complete packet not yet arrived from GDB\n");
	    // }
	    usleep (10);
	    continue;
	} else {
	    size_t n = (size_t) sn;
	    // if (logfile) {
	    //     fprint_bytes (logfile, "RX from GDB: '", gdb_rsp_pkt_buf, n - 1, "'\n");
	    // }
	    if (gdb_rsp_pkt_buf [0] == control_C) {
                handle_RSP_control_C (gdb_rsp_pkt_buf, n);
            }
            else if (gdb_rsp_pkt_buf [0] == '?') {
                handle_RSP_stop_reason (gdb_rsp_pkt_buf, n);
            }
            else if (gdb_rsp_pkt_buf [0] == 'c') {
                handle_RSP_c_continue (gdb_rsp_pkt_buf, n);
            }
            else if (gdb_rsp_pkt_buf [0] == 'D') {
                handle_RSP_shutdown (gdb_rsp_pkt_buf, n);
            }
            else if (gdb_rsp_pkt_buf [0] == 'g') {
                handle_RSP_g_read_all_registers (gdb_rsp_pkt_buf, n);
            }
            else if (gdb_rsp_pkt_buf [0] == 'G') {
                handle_RSP_G_write_all_registers (gdb_rsp_pkt_buf, n);
            }
            else if (gdb_rsp_pkt_buf [0] == 'm') {
                handle_RSP_m_read_mem (gdb_rsp_pkt_buf, n);
            }
            else if (gdb_rsp_pkt_buf [0] == 'M') {
                handle_RSP_M_write_mem_hex_data (gdb_rsp_pkt_buf, n);
            }
            else if (gdb_rsp_pkt_buf [0] == 'p') {
                handle_RSP_p_read_register (gdb_rsp_pkt_buf, n);
            }
            else if (gdb_rsp_pkt_buf [0] == 'P') {
                handle_RSP_P_write_register (gdb_rsp_pkt_buf, n);
            }
            else if (gdb_rsp_pkt_buf [0] == 'q') {
                handle_RSP_q (gdb_rsp_pkt_buf, n);
            }
            else if (gdb_rsp_pkt_buf [0] == 's') {
                handle_RSP_s_step (gdb_rsp_pkt_buf, n);
            }
            else if (gdb_rsp_pkt_buf [0] == 'X') {
                handle_RSP_X_write_mem_bin_data (gdb_rsp_pkt_buf, n);
            }
            else {
		if (logfile) {
		    fprintf (logfile, "WARNING: gdbstub_fe.main_gdbstub: Unrecognized packet (%0zu chars): ", n - 1);
		    fprint_bytes (logfile, "", gdb_rsp_pkt_buf, n - 1, "\n");
		}

                send_RSP_packet_to_GDB ("", 0);
            }
        }
    }

done:
    if (params->autoclose_logfile_stop_fd) {
	if (logfile) {
	    fclose (logfile);
	}
	if (stop_fd >= 0) {
	    close (stop_fd);
	}
    }
    close (gdb_fd);
    return NULL;
}

bool gdbstub_be_poll_preempt (bool include_commands)
{
    struct pollfd fds[2];
    nfds_t nfds = 0;

    if (include_commands) {
	fds[nfds].fd = gdb_fd;
	fds[nfds].events = POLLIN | POLLHUP;
	++nfds;
    }

    if (stop_fd >= 0) {
	fds[nfds].fd = stop_fd;
	fds[nfds].events = POLLIN;
	++nfds;
    }

    return poll (fds, nfds, 0) > 0;
}

// ================================================================
