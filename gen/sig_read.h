#pragma once

#include "winmd_int.h"

#include <stdint.h>

/* ECMA-335 II.23.1.16 element types used by the signature decoders. */
enum
{
	ELT_END         = 0x00,
	ELT_VOID        = 0x01,
	ELT_BOOLEAN     = 0x02,
	ELT_CHAR        = 0x03,
	ELT_I1          = 0x04,
	ELT_U1          = 0x05,
	ELT_I2          = 0x06,
	ELT_U2          = 0x07,
	ELT_I4          = 0x08,
	ELT_U4          = 0x09,
	ELT_I8          = 0x0a,
	ELT_U8          = 0x0b,
	ELT_R4          = 0x0c,
	ELT_R8          = 0x0d,
	ELT_STRING      = 0x0e,
	ELT_PTR         = 0x0f,
	ELT_BYREF       = 0x10,
	ELT_VALUETYPE   = 0x11,
	ELT_CLASS       = 0x12,
	ELT_VAR         = 0x13,
	ELT_MVAR        = 0x14,
	ELT_GENERICINST = 0x15,
	ELT_OBJECT      = 0x1c,
	ELT_SZARRAY     = 0x1d,
};

typedef struct sig_reader {
	uint8_t const *p;
	uint8_t const *end;
} sig_reader;

static inline int sig_read_u8(sig_reader *r, uint8_t *v) {
	if (r->p >= r->end) return -1;
	*v = *r->p++;
	return 0;
}

static inline int sig_read_compressed(sig_reader *r, uint32_t *v) {
	uint8_t b0;
	if (sig_read_u8(r, &b0) != 0) return -1;
	if ((b0 & 0x80) == 0) {
		*v = b0;
		return 0;
	}
	if ((b0 & 0xc0) == 0x80) {
		uint8_t b1;
		if (sig_read_u8(r, &b1) != 0) return -1;
		*v = (( uint32_t ) (b0 & 0x3f) << 8) | b1;
		return 0;
	}
	{
		uint8_t b1, b2, b3;
		if (sig_read_u8(r, &b1) != 0 || sig_read_u8(r, &b2) != 0 || sig_read_u8(r, &b3) != 0) return -1;
		*v = (( uint32_t ) (b0 & 0x1f) << 24) | (( uint32_t ) b1 << 16) | (( uint32_t ) b2 << 8) | b3;
	}
	return 0;
}

static inline int sig_reader_from_blob(winmd_meta const *m, uint32_t blob_ix, sig_reader *sr) {
	uint8_t const *blob;
	uint32_t       blob_len;
	if (!m || !blob_ix || blob_ix >= m->blobs.size) return -1;
	blob    = m->blobs.data + blob_ix;
	sr->p   = blob;
	sr->end = m->blobs.data + m->blobs.size;
	if (sig_read_compressed(sr, &blob_len) != 0) return -1;
	if (sr->p + blob_len > sr->end) sr->end = sr->p + blob_len;
	return 0;
}
