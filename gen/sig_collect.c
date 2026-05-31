#include "sig.h"

#include "sig_read.h"
#include "winmd_int.h"

#include <string.h>

/* Bounded set of distinct TypeDefOrRef tokens referenced by a signature. */
typedef struct sig_tokset {
	uint32_t *out;
	uint32_t *n;
	uint32_t  max;
} sig_tokset;

static int collect_push(sig_tokset *ts, uint32_t coded) {
	uint32_t i;
	if (!coded || !ts->out || !ts->n) return -1;
	for (i = 0; i < *ts->n; i++)
		if (ts->out [i] == coded) return 0;
	if (*ts->n >= ts->max) return 0;
	ts->out [(*ts->n)++] = coded;
	return 0;
}

static int collect_read_type(sig_reader *r, winmd_meta const *m, sig_tokset *ts);

/* ELEMENT_TYPE_GENERICINST in token-collection mode: the open type then its
   argument list. */
static int collect_read_genericinst(sig_reader *r, winmd_meta const *m, sig_tokset *ts) {
	uint32_t argc;
	uint32_t i;
	if (collect_read_type(r, m, ts) != 0) return -1;
	if (sig_read_compressed(r, &argc) != 0) return -1;
	for (i = 0; i < argc; i++)
		if (collect_read_type(r, m, ts) != 0) return -1;
	return 0;
}

static int collect_read_type(sig_reader *r, winmd_meta const *m, sig_tokset *ts) {
	uint8_t elt;
	if (sig_read_u8(r, &elt) != 0) return -1;
	if (elt == ELT_BYREF || elt == ELT_SZARRAY) return collect_read_type(r, m, ts);
	if (elt == ELT_GENERICINST) return collect_read_genericinst(r, m, ts);
	if (elt == ELT_VALUETYPE || elt == ELT_CLASS) {
		uint32_t tok;
		if (sig_read_compressed(r, &tok) != 0) return -1;
		collect_push(ts, tok);
	}
	return 0;
}

/* Read `count` consecutive types, collecting their tokens. */
static int collect_read_n(sig_reader *r, winmd_meta const *m, sig_tokset *ts, uint32_t count) {
	uint32_t i;
	for (i = 0; i < count; i++)
		if (collect_read_type(r, m, ts) != 0) return -1;
	return 0;
}

int winmd_sig_collect_type_tokens(
  winmd_meta const *m, uint32_t blob_ix, uint32_t *out, uint32_t *inout_count, uint32_t max
) {
	sig_reader sr;
	sig_tokset ts = {out, inout_count, max};
	uint8_t    conv;
	uint32_t   gen = 0;
	uint32_t   nparam;
	if (!m || !out || !inout_count) return -1;
	memset(&sr, 0, sizeof(sr));
	if (sig_reader_from_blob(m, blob_ix, &sr) != 0) return -1;
	if (sig_read_u8(&sr, &conv) != 0) return -1;
	if ((conv & 0x10) && sig_read_compressed(&sr, &gen) != 0) return -1;
	if (collect_read_n(&sr, m, &ts, gen) != 0) return -1;
	if (sig_read_compressed(&sr, &nparam) != 0) return -1;
	if (collect_read_type(&sr, m, &ts) != 0) return -1; /* return type */
	return collect_read_n(&sr, m, &ts, nparam);
}

int winmd_field_collect_type_tokens(
  winmd_meta const *m, uint32_t blob_ix, uint32_t *out, uint32_t *inout_count, uint32_t max
) {
	sig_reader sr;
	sig_tokset ts = {out, inout_count, max};
	uint8_t    cc;
	if (!m || !out || !inout_count) return -1;
	memset(&sr, 0, sizeof(sr));
	if (sig_reader_from_blob(m, blob_ix, &sr) != 0) return -1;
	if (sig_read_u8(&sr, &cc) != 0) return -1; /* FIELD calling convention (0x06) */
	return collect_read_type(&sr, m, &ts);
}
