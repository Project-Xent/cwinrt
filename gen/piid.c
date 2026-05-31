#include "piid.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- SHA-1 (RFC 3174) ---------------------------------------------------- */

static uint32_t sha1_rol(uint32_t v, int b) { return (v << b) | (v >> (32 - b)); }

/* Per-round nonlinear function f(b,c,d) for round index t (0..79). */
static uint32_t sha1_round_f(int t, uint32_t b, uint32_t c, uint32_t d) {
	if (t < 20) return (b & c) | ((~b) & d);
	if (t < 40) return b ^ c ^ d;
	if (t < 60) return (b & c) | (b & d) | (c & d);
	return b ^ c ^ d;
}

/* Per-round constant K for round index t (0..79). */
static uint32_t sha1_round_k(int t) {
	static uint32_t const k [4] = {0x5A827999u, 0x6ED9EBA1u, 0x8F1BBCDCu, 0xCA62C1D6u};
	return k [t / 20];
}

static void sha1_expand(uint32_t w [80], uint8_t const *block) {
	int t;
	for (t = 0; t < 16; t++)
		w [t] = (( uint32_t ) block [t * 4] << 24) | (( uint32_t ) block [t * 4 + 1] << 16)
		      | (( uint32_t ) block [t * 4 + 2] << 8) | ( uint32_t ) block [t * 4 + 3];
	for (t = 16; t < 80; t++) w [t] = sha1_rol(w [t - 3] ^ w [t - 8] ^ w [t - 14] ^ w [t - 16], 1);
}

static void sha1_block(uint32_t h [5], uint8_t const *block) {
	uint32_t w [80];
	uint32_t a = h [0], b = h [1], c = h [2], d = h [3], e = h [4];
	int      t;
	sha1_expand(w, block);
	for (t = 0; t < 80; t++) {
		uint32_t tmp = sha1_rol(a, 5) + sha1_round_f(t, b, c, d) + e + sha1_round_k(t) + w [t];
		e = d;
		d = c;
		c = sha1_rol(b, 30);
		b = a;
		a = tmp;
	}
	h [0] += a;
	h [1] += b;
	h [2] += c;
	h [3] += d;
	h [4] += e;
}

/* padded length: data + 0x80 + zeros to 56 mod 64 + 8-byte big-endian bit length */
static size_t sha1_padded_len(size_t len) {
	size_t padded = len + 1;
	while ((padded % 64) != 56) padded++;
	return padded + 8;
}

/* Build the padded SHA-1 message into buf (size sha1_padded_len(len)). */
static void sha1_pad(uint8_t *buf, uint8_t const *data, size_t len, size_t padded) {
	uint64_t ml = ( uint64_t ) len * 8u;
	size_t   i;
	int      j;
	if (len) memcpy(buf, data, len);
	buf [len] = 0x80;
	for (i = len + 1; i < padded - 8; i++) buf [i] = 0;
	for (j = 0; j < 8; j++) buf [padded - 1 - j] = ( uint8_t ) (ml >> (8 * j));
}

static void sha1_emit_digest(uint32_t const h [5], uint8_t digest [20]) {
	int j;
	for (j = 0; j < 5; j++) {
		digest [j * 4]     = ( uint8_t ) (h [j] >> 24);
		digest [j * 4 + 1] = ( uint8_t ) (h [j] >> 16);
		digest [j * 4 + 2] = ( uint8_t ) (h [j] >> 8);
		digest [j * 4 + 3] = ( uint8_t ) (h [j]);
	}
}

void cwinrt_sha1(uint8_t const *data, size_t len, uint8_t digest [20]) {
	uint32_t h [5]   = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
	size_t   padded  = sha1_padded_len(len);
	uint8_t *buf     = ( uint8_t * ) malloc(padded);
	size_t   i;

	if (!buf) {
		memset(digest, 0, 20);
		return;
	}
	sha1_pad(buf, data, len, padded);
	for (i = 0; i < padded; i += 64) sha1_block(h, buf + i);
	free(buf);
	sha1_emit_digest(h, digest);
}

void cwinrt_uuid_v5(uint8_t const ns [16], char const *name, size_t namelen, uint8_t out [16]) {
	/* SHA-1(namespace || name); first 16 bytes, with v5 + RFC-4122 variant bits. */
	uint8_t *buf;
	uint8_t  digest [20];
	uint8_t  stackbuf [256];
	size_t   total = 16 + namelen;

	buf = (total <= sizeof(stackbuf)) ? stackbuf : ( uint8_t * ) malloc(total);
	if (!buf) {
		memset(out, 0, 16);
		return;
	}
	memcpy(buf, ns, 16);
	if (namelen) memcpy(buf + 16, name, namelen);
	cwinrt_sha1(buf, total, digest);
	if (buf != stackbuf) free(buf);
	memcpy(out, digest, 16);
	out [6] = ( uint8_t ) ((out [6] & 0x0F) | 0x50); /* version 5 */
	out [8] = ( uint8_t ) ((out [8] & 0x3F) | 0x80); /* RFC-4122 variant */
}

/* ---- WinRT signature strings -------------------------------------------- */

/* Matches cppwinrt basic_signature_v (winrt/base.h). */
static struct {
	uint8_t     element_type;
	char const *sig;
} const k_basic_sigs [] = {
  {0x02, "b1"},                       /* Boolean */
  {0x03, "c2"},                       /* Char16  */
  {0x04, "i1"},                       /* Int8    */
  {0x05, "u1"},                       /* UInt8   */
  {0x06, "i2"},                       /* Int16   */
  {0x07, "u2"},                       /* UInt16  */
  {0x08, "i4"},                       /* Int32   */
  {0x09, "u4"},                       /* UInt32  */
  {0x0a, "i8"},                       /* Int64   */
  {0x0b, "u8"},                       /* UInt64  */
  {0x0c, "f4"},                       /* Single  */
  {0x0d, "f8"},                       /* Double  */
  {0x0e, "string"},                   /* String  */
  {0x1c, "cinterface(IInspectable)"}, /* Object/IInspectable */
};

char const *cwinrt_piid_basic_sig(uint8_t element_type) {
	size_t i;
	for (i = 0; i < sizeof(k_basic_sigs) / sizeof(k_basic_sigs [0]); i++)
		if (k_basic_sigs [i].element_type == element_type) return k_basic_sigs [i].sig;
	return 0;
}

void cwinrt_piid_guid_str(uint8_t const g [16], char *out, size_t cap) {
	/* winmd layout: Data1 = g[3..0] (LE), Data2 = g[5..4], Data3 = g[7..6]. */
	snprintf(
	  out, cap, "{%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x}", g [3], g [2], g [1], g [0],
	  g [5], g [4], g [7], g [6], g [8], g [9], g [10], g [11], g [12], g [13], g [14], g [15]
	);
}

/* WinRT parameterized namespace GUID, big-endian byte order for v5 hashing. */
static uint8_t const k_winrt_ns [16] = {
  0x11, 0xf4, 0x7a, 0xd5, 0x7b, 0x73, 0x42, 0xc0, 0xab, 0xae, 0x87, 0x8b, 0x1e, 0x16, 0xad, 0xee
};

int cwinrt_sig_pinterface(
  uint8_t const open_guid [16], char const *const *arg_sigs, uint32_t argc, char *out, size_t cap
) {
	char     gstr [48];
	size_t   pos;
	uint32_t i;

	cwinrt_piid_guid_str(open_guid, gstr, sizeof(gstr));
	pos = ( size_t ) snprintf(out, cap, "pinterface(%s", gstr);
	if (pos >= cap) return -1;
	for (i = 0; i < argc; i++) {
		char const *a = arg_sigs [i] ? arg_sigs [i] : "cinterface(IInspectable)";
		if (pos + 1 < cap) out [pos++] = ';';
		pos += ( size_t ) snprintf(out + pos, cap - pos, "%s", a);
		if (pos >= cap) return -1;
	}
	if (pos + 2 >= cap) return -1;
	out [pos++] = ')';
	out [pos]   = '\0';
	return ( int ) pos;
}

void cwinrt_piid_from_sig(char const *sig, size_t siglen, uint8_t out [16]) {
	uint8_t  stackbuf [512];
	uint8_t *buf;
	uint8_t  digest [20];
	size_t   total = 16 + siglen;

	buf = (total <= sizeof(stackbuf)) ? stackbuf : ( uint8_t * ) malloc(total);
	if (!buf) {
		memset(out, 0, 16);
		return;
	}
	memcpy(buf, k_winrt_ns, 16);
	if (siglen) memcpy(buf + 16, sig, siglen);
	cwinrt_sha1(buf, total, digest);
	if (buf != stackbuf) free(buf);

	/* WinRT (cppwinrt generate_guid): to_guid() reads the first 16 SHA-1 bytes as
	   a little-endian guid, then endian_swap() reverses Data1/Data2/Data3 — the
	   net effect on the in-memory (winmd) byte layout is to reverse those three
	   fields' bytes; set_named_guid_fields then stamps version 5 into the high
	   nibble of Data3's high byte and the RFC-4122 variant into Data4[0]. */
	out [0]  = digest [3];
	out [1]  = digest [2];
	out [2]  = digest [1];
	out [3]  = digest [0];
	out [4]  = digest [5];
	out [5]  = digest [4];
	out [6]  = digest [7];
	out [7]  = ( uint8_t ) (0x50u | (digest [6] & 0x0fu)); /* version 5 */
	out [8]  = ( uint8_t ) (0x80u | (digest [8] & 0x3fu)); /* RFC-4122 variant */
	out [9]  = digest [9];
	out [10] = digest [10];
	out [11] = digest [11];
	out [12] = digest [12];
	out [13] = digest [13];
	out [14] = digest [14];
	out [15] = digest [15];
}

int cwinrt_piid_compute(
  uint8_t const open_guid [16], char const *const *arg_sigs, uint32_t argc, uint8_t out [16]
) {
	char sig [1024];
	int  n = cwinrt_sig_pinterface(open_guid, arg_sigs, argc, sig, sizeof(sig));
	if (n < 0) return -1;
	cwinrt_piid_from_sig(sig, ( size_t ) n, out);
	return 0;
}
