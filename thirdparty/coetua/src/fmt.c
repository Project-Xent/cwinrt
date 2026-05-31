#include "fmt.h"
#include "config.h"
#include "err.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>

/* ═══════════════════════════════════════════════════════
   Internal helpers
   ═══════════════════════════════════════════════════════ */

enum
{
	FMT_SIGNIF = 17,
};

/* write a single char to the strand */
static void fmt_putc(fmt *fp, char c) { concatr(fp->sd, ( rune ) ( uchar ) c); }

static void sd_putc(int sd, char c) { concatr(sd, ( rune ) ( uchar ) c); }

static void sd_puts(int sd, char *s) { concat(sd, s); }

static int  fmt_width_pad(fmt *fp, int len) {
	return ((fp->flags & FMT_WIDTH) && fp->width > len) ? fp->width - len : 0;
}

static void fmt_putpad(fmt *fp, int pad) {
	while (pad-- > 0) fmt_putc(fp, ' ');
}

static void fmt_putfill(fmt *fp, char c, int n) {
	while (n-- > 0) fmt_putc(fp, c);
}

static void fmt_putfield(fmt *fp, slitr s) {
	int pad = fmt_width_pad(fp, ( int ) s.len);
	if (!(fp->flags & FMT_LEFT)) fmt_putpad(fp, pad);
	if (s.len > 0) concats(fp->sd, s);
	if (fp->flags & FMT_LEFT) fmt_putpad(fp, pad);
}

/* ── Numeric formatting ─────────────────────────────── */

static void fmt_int(fmt *fp, uvlong val, int base, bool is_signed, char *digits) {
	char   rev [64];
	int    ndig = 0;
	int    neg  = 0;
	uvlong mag  = val;

	if (is_signed) {
		vlong sv = ( vlong ) val;
		if (sv < 0) {
			neg = 1;
			mag = 0 - ( uvlong ) sv;
		}
	}

	if (mag == 0 && (fp->flags & FMT_PREC) && fp->prec == 0) { }
	else {
		do {
			rev [ndig++]  = digits [mag % ( uvlong ) base];
			mag          /= ( uvlong ) base;
		}
		while (mag > 0);
	}

	int digit_len = ndig;
	if ((fp->flags & FMT_PREC) && fp->prec > digit_len) digit_len = fp->prec;

	int comma_len = 0;
	int group     = 0;
	if ((fp->flags & (FMT_COMMA | FMT_COMMA4)) && digit_len > 0) {
		group     = (fp->flags & FMT_COMMA4) ? 4 : 3;
		comma_len = (digit_len - 1) / group;
	}

	int  prefix_len = 0;
	char prefix [3];
	if (neg) prefix [prefix_len++] = '-';
	else if (fp->flags & FMT_PLUS) prefix [prefix_len++] = '+';
	else if (fp->flags & FMT_SPACE) prefix [prefix_len++] = ' ';
	if ((fp->flags & FMT_ALT) && base == 8 && !(ndig == 1 && rev [0] == '0')) prefix [prefix_len++] = '0';
	if ((fp->flags & FMT_ALT) && base == 16) {
		prefix [prefix_len++] = '0';
		prefix [prefix_len++] = (digits [10] == 'a') ? 'x' : 'X';
	}

	int dlen = prefix_len + digit_len + comma_len;
	int pad  = fmt_width_pad(fp, dlen);
	if (!(fp->flags & FMT_LEFT) && !(fp->flags & FMT_ZERO)) { fmt_putpad(fp, pad); }
	else if ((fp->flags & FMT_ZERO) && !(fp->flags & FMT_LEFT)) {
		if (prefix_len) concats(fp->sd, mkslitr(prefix, ( uvlong ) prefix_len));
		fmt_putfill(fp, '0', pad);
		prefix_len = 0;
	}
	if (prefix_len) concats(fp->sd, mkslitr(prefix, ( uvlong ) prefix_len));

	for (int i = digit_len - 1; i >= 0; i--) {
		if (group && i != digit_len - 1 && (i + 1) % group == 0) fmt_putc(fp, ',');
		if (i >= ndig) fmt_putc(fp, '0');
		else fmt_putc(fp, rev [i]);
	}

	if (fp->flags & FMT_LEFT) fmt_putpad(fp, pad);
}

/* ── Custom verb registry ───────────────────────────── */

static struct {
	rune c;
	bool (*fn)(fmt *);
}          *verb_registry;

static int  nverbs;
static int  verbcap;

static bool verb_registry_init(void) {
	if (verb_registry) return true;
	verbcap       = COETUA_VERB_TABLE_SEED > 0 ? COETUA_VERB_TABLE_SEED : 1;
	verb_registry = calloc(( size_t ) verbcap, sizeof(*verb_registry));
	if (!verb_registry) {
		errmsg("fmtinstall: out of memory");
		verbcap = 0;
		return false;
	}
	return true;
}

static bool verb_registry_grow(void) {
	int newcap = verbcap ? verbcap * 2 : COETUA_VERB_TABLE_SEED;
	if (newcap < 1) newcap = 1;
	if (newcap <= verbcap) {
		errmsg("fmtinstall: descriptor overflow");
		return false;
	}
	void *p = realloc(verb_registry, ( size_t ) newcap * sizeof(*verb_registry));
	if (!p) {
		errmsg("fmtinstall: out of memory");
		return false;
	}
	verb_registry = p;
	memset(verb_registry + verbcap, 0, ( size_t ) (newcap - verbcap) * sizeof(*verb_registry));
	verbcap = newcap;
	return true;
}

void fmtinstall(rune c, bool (*fn)(fmt *)) {
	if (!fn) {
		errmsg("fmtinstall: bad formatter");
		return;
	}
	if (!verb_registry_init()) return;
	for (int i = 0; i < nverbs; i++) {
		if (verb_registry [i].c == c) {
			verb_registry [i].fn = fn;
			return;
		}
	}
	if (nverbs >= verbcap && !verb_registry_grow()) return;
	verb_registry [nverbs].c  = c;
	verb_registry [nverbs].fn = fn;
	nverbs++;
}

/* ── Built-in verbs ─────────────────────────────────── */

static uvlong fmt_arg_signed(fmt *fp) {
	if (fp->flags & FMT_VLONG) return ( uvlong ) va_arg(fp->args, vlong);
	if (fp->flags & FMT_LONG) return ( uvlong ) va_arg(fp->args, long);
	return ( uvlong ) va_arg(fp->args, int);
}

static uvlong fmt_arg_unsigned(fmt *fp) {
	if (fp->flags & FMT_VLONG) return va_arg(fp->args, uvlong);
	if (fp->flags & FMT_LONG) return ( uvlong ) va_arg(fp->args, ulong);
	return ( uvlong ) va_arg(fp->args, uint);
}

static bool fmt_verb_d(fmt *fp) {
	bool   unsign = (fp->flags & FMT_UNSIGN) != 0;
	uvlong v      = unsign ? fmt_arg_unsigned(fp) : fmt_arg_signed(fp);
	fmt_int(fp, v, 10, !unsign, "0123456789");
	return true;
}

static bool fmt_verb_s(fmt *fp) {
	char *s = va_arg(fp->args, char *);
	if (!s) s = "(null)";
	int slen = ( int ) strlen(s);
	if ((fp->flags & FMT_PREC) && fp->prec < slen) slen = fp->prec;
	fmt_putfield(fp, mkslitr(s, ( uvlong ) slen));
	return true;
}

static bool fmt_verb_c(fmt *fp) {
	char c = ( char ) va_arg(fp->args, int);
	fmt_putfield(fp, mkslitr(&c, 1));
	return true;
}

static bool fmt_verb_C(fmt *fp) {
	rune r    = ( rune ) va_arg(fp->args, int);
	int  rlen = runelen(r);
	char buf [4];
	runetochar(buf, &r);
	fmt_putfield(fp, mkslitr(buf, ( uvlong ) rlen));
	return true;
}

static bool fmt_verb_p(fmt *fp) {
	void *ptr = va_arg(fp->args, void *);
	concat(fp->sd, "0x");
	fmt_int(fp, ( uintptr_t ) ptr, 16, false, "0123456789abcdef");
	return true;
}

static bool fmt_verb_r(fmt *fp) {
	char *msg = errmsg(null);
	if (!msg) msg = "(no error)";
	fmt_putfield(fp, mkslitr(msg, ( uvlong ) strlen(msg)));
	return true;
}

static bool fmt_verb_S(fmt *fp) {
	rune *r = va_arg(fp->args, rune *);
	if (!r) {
		fmt_putfield(fp, mkslitr("(null)", 6));
		return true;
	}

	int tmp_sd = mkstrand(0);
	if (tmp_sd < 0) return false;
	for (int i = 0; r [i] && (!(fp->flags & FMT_PREC) || i < fp->prec); i++) concatr(tmp_sd, r [i]);
	slitr sl = obslitr(tmp_sd);
	fmt_putfield(fp, sl);
	rmstrand(tmp_sd);
	return true;
}

typedef struct fmt_spec {
	char *start;
	char *end;
	rune  verb;
	uint  flags;
	int   width;
	int   prec;
	bool  has_width;
	bool  has_prec;
	bool  width_star;
	bool  prec_star;
	bool  takes_arg;
} fmt_spec;

typedef struct {
	int  nargs;
	bool nested_array;
} afmt_plan;

typedef struct {
	void     *base;
	char     *subfmt;
	char     *inner_subfmt;
	afmt_plan plan;
	int       count;
	int       stride;
	uvlong    span;
	bool      compound;
} afmt_state;

typedef struct {
	uchar *base;
	int    stride;
	uint   flags;
} afmt_binding;

static bool fmt_scan_flag(char c) { return strchr("-+ #0hl", c) != null; }

static bool fmt_scan_modifier(char c) { return strchr("hlu,;#", c) != null; }

static bool fmt_scan_digit(char c) { return c >= '0' && c <= '9'; }

static int  fmt_scan_number(char **pp) {
	int   n = 0;
	char *p = *pp;
	while (fmt_scan_digit(*p)) n = n * 10 + (*p++ - '0');
	*pp = p;
	return n;
}

static void fmt_scan_flag_bit(fmt_spec *s, char c) {
	switch (c) {
	case '-' : s->flags |= FMT_LEFT; break;
	case '+' : s->flags |= FMT_PLUS; break;
	case ' ' : s->flags |= FMT_SPACE; break;
	case '#' : s->flags |= FMT_ALT; break;
	case '0' : s->flags |= FMT_ZERO; break;
	case 'h' :
		if (s->flags & FMT_SHORT) {
			s->flags |= FMT_BYTE;
			s->flags &= ~FMT_SHORT;
		}
		else s->flags |= FMT_SHORT;
		break;
	case 'l' :
		if (s->flags & FMT_LONG) {
			s->flags |= FMT_VLONG;
			s->flags &= ~FMT_LONG;
		}
		else s->flags |= FMT_LONG;
		break;
	default : break;
	}
}

static int fmt_scan_rune(char *p, rune *r) {
	int n = chartorune(r, p);
	return n > 0 ? n : 1;
}

static fmt_spec fmt_scan(char *p) {
	fmt_spec s;
	memset(&s, 0, sizeof(s));
	s.start = p;
	p++;
	if (*p == '%') {
		s.verb = '%';
		s.end  = p + 1;
		return s;
	}
	while (*p && fmt_scan_flag(*p)) fmt_scan_flag_bit(&s, *p++);
	if (*p == '*') {
		s.has_width  = true;
		s.width_star = true;
		p++;
	}
	else if (fmt_scan_digit(*p)) {
		s.has_width = true;
		s.width     = fmt_scan_number(&p);
	}
	if (*p == '.') {
		p++;
		s.has_prec = true;
		if (*p == '*') s.prec_star = true, p++;
		else if (fmt_scan_digit(*p)) s.prec = fmt_scan_number(&p);
	}
	while (*p && fmt_scan_modifier(*p)) {
		if (*p == 'u') s.flags |= FMT_UNSIGN;
		else if (*p == ',') s.flags |= FMT_COMMA | FMT_FLOAT;
		else if (*p == ';') s.flags |= FMT_COMMA4 | FMT_ARRST;
		else fmt_scan_flag_bit(&s, *p);
		p++;
	}
	if (*p) p += fmt_scan_rune(p, &s.verb);
	else if (s.flags & FMT_UNSIGN) s.verb = 'd';
	s.end       = p;
	s.takes_arg = s.verb != 0 && s.verb != '%' && s.verb != 'r';
	return s;
}

static afmt_plan afmt_analyze(char *sf) {
	afmt_plan plan = {0, false};
	while (*sf) {
		if (*sf != '%') {
			sf++;
			continue;
		}
		fmt_spec s = fmt_scan(sf);
		if (s.takes_arg) {
			plan.nargs++;
			if (s.verb == 'a') plan.nested_array = true;
		}
		sf = s.end;
	}
	return plan;
}

static void afmt_put_int(int sd, int v) {
	char tmp [16];
	int  n = 0;
	long x = v;
	if (x < 0) {
		concatr(sd, '-');
		x = -x;
	}
	do {
		tmp [n++]  = ( char ) ('0' + x % 10);
		x         /= 10;
	}
	while (x > 0);
	while (n > 0) concatr(sd, ( rune ) ( uchar ) tmp [--n]);
}

static char *afmt_normalize(char *sf, fmt *fp) {
	if (!sf) {
		errmsg("fmt %a: bad subformat");
		return null;
	}
	int sd = mkstrand(0);
	if (sd < 0) return null;
	for (char *p = sf; *p;) {
		char *pct = strchr(p, '%');
		if (!pct) {
			concat(sd, p);
			break;
		}
		if (pct > p) concats(sd, mkslitr(p, ( uvlong ) (pct - p)));
		p       = pct;
		char *q = p + 1;
		concatr(sd, '%');
		if (*q == '%') {
			concatr(sd, '%');
			p = q + 1;
			continue;
		}
		while (*q && fmt_scan_flag(*q)) concatr(sd, ( rune ) ( uchar ) *q++);
		if (*q == '*') {
			int n = va_arg(fp->args, int);
			afmt_put_int(sd, n);
			q++;
		}
		else
			while (fmt_scan_digit(*q)) concatr(sd, ( rune ) ( uchar ) *q++);
		if (*q == '.') {
			concatr(sd, '.');
			q++;
			if (*q == '*') {
				int n = va_arg(fp->args, int);
				afmt_put_int(sd, n);
				q++;
			}
			else
				while (fmt_scan_digit(*q)) concatr(sd, ( rune ) ( uchar ) *q++);
		}
		while (*q && fmt_scan_modifier(*q)) concatr(sd, ( rune ) ( uchar ) *q++);
		if (*q) concatr(sd, ( rune ) ( uchar ) *q++);
		p = q;
	}
	slitr  s = obslitr(sd);
	uvlong outlen;
	if (!addok64(s.len, 1, &outlen)) {
		rmstrand(sd);
		errmsg("fmt %a: format too large");
		return null;
	}
	char *out = malloc(( size_t ) outlen);
	if (!out) {
		rmstrand(sd);
		errmsg("fmt %a: out of memory");
		return null;
	}
	if (s.len > 0) memcpy(out, s.s, s.len);
	out [s.len] = 0;
	rmstrand(sd);
	return out;
}

static int afmt_scalar_stride(uint flags) {
	if (flags & FMT_FLOAT) return (flags & FMT_LONG) ? sizeof(long double) : sizeof(float);
	if (flags & FMT_VLONG) return sizeof(vlong);
	if (flags & FMT_LONG) return sizeof(long);
	if (flags & FMT_SHORT) return sizeof(short);
	if (flags & FMT_BYTE) return 1;
	return sizeof(int);
}

static void afmt_release(afmt_state *st) {
	free(st->inner_subfmt);
	free(st->subfmt);
	st->inner_subfmt = null;
	st->subfmt       = null;
}

static bool afmt_fail(afmt_state *st, char *msg) {
	if (msg && !err()) errmsg(msg);
	afmt_release(st);
	return false;
}

static bool fmt_append_sd(fmt *fp, int sd) {
	if (sd < 0) return false;
	slitr s = obslitr(sd);
	if (s.len > 0) concats(fp->sd, s);
	rmstrand(sd);
	return true;
}

static void afmt_append_text(fmt *fp, char *start, char *end) {
	if (end > start) concats(fp->sd, mkslitr(start, ( uvlong ) (end - start)));
}

static char *afmt_spec_cstr(fmt_spec s) {
	uvlong len = ( uvlong ) (s.end - s.start);
	uvlong size;
	if (!addok64(len, 1, &size)) {
		errmsg("fmt %a: format too large");
		return null;
	}
	char *p = malloc(( size_t ) size);
	if (!p) {
		errmsg("fmt %a: out of memory");
		return null;
	}
	if (len > 0) memcpy(p, s.start, len);
	p [len] = 0;
	return p;
}

static void *afmt_bound_arg(afmt_binding *b, int index) { return b->base + ( uvlong ) index * ( uvlong ) b->stride; }

static void *afmt_element(afmt_state *st, int index) { return ( uchar * ) st->base + ( uvlong ) index * st->span; }

static int   afmt_format_bound(fmt_spec s, afmt_binding *binding, int index) {
	char *spec = afmt_spec_cstr(s);
	if (!spec) return -1;
	int   sd    = -1;
	void *elem  = afmt_bound_arg(binding, index);
	uint  flags = binding->flags;
	if (flags & FMT_FLOAT)
		if (flags & FMT_LONG) sd = fmts(0, spec, *( long double * ) elem);
		else sd = fmts(0, spec, ( double ) *( float * ) elem);
	else if (flags & FMT_VLONG)
		if (flags & FMT_UNSIGN) sd = fmts(0, spec, *( uvlong * ) elem);
		else sd = fmts(0, spec, *( vlong * ) elem);
	else if (flags & FMT_LONG)
		if (flags & FMT_UNSIGN) sd = fmts(0, spec, *( ulong * ) elem);
		else sd = fmts(0, spec, *( long * ) elem);
	else if (flags & FMT_SHORT)
		if (flags & FMT_UNSIGN) sd = fmts(0, spec, ( uint ) *( ushort * ) elem);
		else sd = fmts(0, spec, ( int ) *( short * ) elem);
	else if (flags & FMT_BYTE)
		if (flags & FMT_UNSIGN) sd = fmts(0, spec, ( uint ) *( uchar * ) elem);
		else sd = fmts(0, spec, ( int ) *( char * ) elem);
	else if (flags & FMT_UNSIGN) sd = fmts(0, spec, *( uint * ) elem);
	else sd = fmts(0, spec, *( int * ) elem);
	free(spec);
	return sd;
}

static bool afmt_emit_bound_spec(fmt *fp, fmt_spec s, afmt_binding *binding, int index) {
	return fmt_append_sd(fp, afmt_format_bound(s, binding, index));
}

static bool afmt_append_literal_spec(fmt *fp, fmt_spec s) {
	char *spec = afmt_spec_cstr(s);
	if (!spec) return false;
	int sd = fmts(0, spec);
	free(spec);
	return fmt_append_sd(fp, sd);
}

static bool afmt_format_spec(fmt *fp, fmt_spec s, afmt_binding *binding, int *index) {
	if (!s.takes_arg) return afmt_append_literal_spec(fp, s);
	return afmt_emit_bound_spec(fp, s, binding, (*index)++);
}

static bool afmt_format_scalar_element(fmt *fp, char *subfmt, afmt_binding binding) {
	char *p = subfmt;
	int   n = 0;
	while (*p) {
		char *pct = strchr(p, '%');
		if (!pct) {
			uvlong tail = ( uvlong ) strlen(p);
			if (tail > 0) concats(fp->sd, mkslitr(p, tail));
			return true;
		}
		afmt_append_text(fp, p, pct);
		fmt_spec s = fmt_scan(pct);
		if (!afmt_format_spec(fp, s, &binding, &n)) return false;
		p = s.end;
	}
	return true;
}

static bool afmt_format_compound_element(fmt *fp, char *subfmt, void *elem, char *inner_subfmt) {
	if (inner_subfmt) return fmt_append_sd(fp, fmts(0, subfmt, elem, inner_subfmt));
	return fmt_append_sd(fp, fmts(0, subfmt, elem));
}

static bool afmt_read_source(fmt *fp, afmt_state *st) {
	if (fp->flags & FMT_ARRST) {
		arrst a = va_arg(fp->args, arrst);
		if (a.len > ( uvlong ) INT_MAX) {
			errmsg("fmt %a: array too large");
			return false;
		}
		st->base  = a.x;
		st->count = ( int ) a.len;
		return true;
	}
	st->base = va_arg(fp->args, void *);
	return true;
}

static bool afmt_parse_count(fmt *fp, afmt_state *st) {
	if (!afmt_read_source(fp, st)) return false;
	if (fp->flags & FMT_PREC) st->count = fp->prec;
	else if (!(fp->flags & FMT_ARRST)) {
		errmsg("fmt %a: need element count");
		return false;
	}
	return true;
}

static bool afmt_parse_shape(fmt *fp, afmt_state *st) {
	st->compound = (fp->flags & FMT_ALT) != 0;
	st->stride   = afmt_scalar_stride(fp->flags);
	if (!st->compound) return true;
	st->stride = va_arg(fp->args, int);
	if (st->stride <= 0) {
		errmsg("fmt %a: invalid element stride");
		return false;
	}
	return true;
}

static bool afmt_set_span(afmt_state *st) {
	if (st->compound || st->plan.nargs == 0) {
		st->span = ( uvlong ) st->stride;
		return true;
	}
	if (!mulok64(( uvlong ) st->stride, ( uvlong ) st->plan.nargs, &st->span))
		return afmt_fail(st, "fmt %a: element span overflow");
	return true;
}

static bool afmt_parse_formats(fmt *fp, afmt_state *st, char *subfmt) {
	st->subfmt = afmt_normalize(subfmt, fp);
	if (!st->subfmt) return false;
	st->plan = afmt_analyze(st->subfmt);
	if (!afmt_set_span(st)) return false;
	if (st->compound && st->plan.nested_array) {
		char *inner = va_arg(fp->args, char *);
		if (!inner) return afmt_fail(st, "fmt %a: bad subformat");
		st->inner_subfmt = afmt_normalize(inner, fp);
		if (!st->inner_subfmt) return afmt_fail(st, null);
	}
	return true;
}

static bool afmt_init(fmt *fp, afmt_state *st) {
	memset(st, 0, sizeof(*st));
	if (!afmt_parse_count(fp, st)) return false;

	char *subfmt = va_arg(fp->args, char *);
	if (!subfmt) return afmt_fail(st, "fmt %a: bad subformat");
	if (!st->base && st->count > 0) return afmt_fail(st, "fmt %a: bad source");

	/* In %a only, # means compound element stride.  Other verbs keep
	   interpreting the same syntax as ordinary alternate form. */
	if (!afmt_parse_shape(fp, st)) return false;
	return afmt_parse_formats(fp, st, subfmt);
}

static bool fmt_verb_a(fmt *fp) {
	afmt_state st;
	int        dst = fp->sd;
	int        out = mkstrand(0);
	if (out < 0) return false;
	fp->sd = out;
	if (!afmt_init(fp, &st)) {
		fp->sd = dst;
		rmstrand(out);
		return false;
	}
	bool ok = true;

	for (int i = 0; i < st.count; i++) {
		void *elem = afmt_element(&st, i);
		if (st.compound) {
			if (!afmt_format_compound_element(fp, st.subfmt, elem, st.inner_subfmt)) {
				ok = false;
				break;
			}
		}
		else if (!afmt_format_scalar_element(fp, st.subfmt, (afmt_binding) {elem, st.stride, fp->flags})) {
			ok = false;
			break;
		}
	}
	afmt_release(&st);
	fp->sd = dst;
	if (!ok) {
		rmstrand(out);
		return false;
	}
	slitr s = obslitr(out);
	int   n = s.len > ( uvlong ) INT_MAX ? INT_MAX : ( int ) s.len;
	int   p = ((fp->flags & FMT_WIDTH) && fp->width > n) ? fp->width - n : 0;
	if (!(fp->flags & FMT_LEFT)) fmt_putfill(fp, ' ', p);
	if (s.len > 0) concats(fp->sd, s);
	if (fp->flags & FMT_LEFT) fmt_putfill(fp, ' ', p);
	rmstrand(out);
	return ok;
}

static bool fmt_verb_t(fmt *fp) {
	slitr sl = va_arg(fp->args, slitr);
	if (!sl.s && sl.len > 0) {
		errmsg("fmt %t: bad slitr");
		return false;
	}
	int slen = ( int ) sl.len;
	if ((fp->flags & FMT_PREC) && fp->prec < slen) slen = fp->prec;
	fmt_putfield(fp, mkslitr(sl.s, ( uvlong ) slen));
	return true;
}

/* ═══════════════════════════════════════════════════════
   Floating point formatting
   Decimal-first path modeled after Plan 9 lib9/fmt/fltfmt.c.
   ═══════════════════════════════════════════════════════ */

typedef struct fmt_dec fmt_dec;

struct fmt_dec {
	char digits [FMT_SIGNIF + 10];
	int  exp;
	int  sexp;
	int  ndigits;
};

static double const fmt_pows10 [] = {
  1e0,   1e1,   1e2,   1e3,   1e4,   1e5,   1e6,   1e7,   1e8,   1e9,   1e10,  1e11,  1e12,  1e13,  1e14,  1e15,
  1e16,  1e17,  1e18,  1e19,  1e20,  1e21,  1e22,  1e23,  1e24,  1e25,  1e26,  1e27,  1e28,  1e29,  1e30,  1e31,
  1e32,  1e33,  1e34,  1e35,  1e36,  1e37,  1e38,  1e39,  1e40,  1e41,  1e42,  1e43,  1e44,  1e45,  1e46,  1e47,
  1e48,  1e49,  1e50,  1e51,  1e52,  1e53,  1e54,  1e55,  1e56,  1e57,  1e58,  1e59,  1e60,  1e61,  1e62,  1e63,
  1e64,  1e65,  1e66,  1e67,  1e68,  1e69,  1e70,  1e71,  1e72,  1e73,  1e74,  1e75,  1e76,  1e77,  1e78,  1e79,
  1e80,  1e81,  1e82,  1e83,  1e84,  1e85,  1e86,  1e87,  1e88,  1e89,  1e90,  1e91,  1e92,  1e93,  1e94,  1e95,
  1e96,  1e97,  1e98,  1e99,  1e100, 1e101, 1e102, 1e103, 1e104, 1e105, 1e106, 1e107, 1e108, 1e109, 1e110, 1e111,
  1e112, 1e113, 1e114, 1e115, 1e116, 1e117, 1e118, 1e119, 1e120, 1e121, 1e122, 1e123, 1e124, 1e125, 1e126, 1e127,
  1e128, 1e129, 1e130, 1e131, 1e132, 1e133, 1e134, 1e135, 1e136, 1e137, 1e138, 1e139, 1e140, 1e141, 1e142, 1e143,
  1e144, 1e145, 1e146, 1e147, 1e148, 1e149, 1e150, 1e151, 1e152, 1e153, 1e154, 1e155, 1e156, 1e157, 1e158, 1e159,
};

static double fmt_pow10d(int n) {
	double d;
	int    neg;

	neg = 0;
	if (n < 0) {
		neg = 1;
		n   = -n;
	}
	int npows10 = ( int ) (sizeof(fmt_pows10) / sizeof(fmt_pows10 [0]));
	if (n < npows10) d = fmt_pows10 [n];
	else {
		d = fmt_pows10 [npows10 - 1];
		for (;;) {
			n -= npows10 - 1;
			if (n < npows10) {
				d *= fmt_pows10 [n];
				break;
			}
			d *= fmt_pows10 [npows10 - 1];
		}
	}
	return neg ? 1.0 / d : d;
}

static int fmt_xadd1(char *a, int n) {
	if (n < 0 || n > FMT_SIGNIF) return 0;
	for (char *b = a + n - 1; b >= a; b--) {
		int c = *b + 1;
		if (c <= '9') {
			*b = ( char ) c;
			return 0;
		}
		*b = '0';
	}
	a [0] = '1';
	return 1;
}

static int fmt_xsub1(char *a, int n) {
	if (n < 0 || n > FMT_SIGNIF) return 0;
	for (char *b = a + n - 1; b >= a; b--) {
		int c = *b - 1;
		if (c >= '0') {
			if (c == '0' && b == a) {
				*b = '9';
				return 1;
			}
			*b = ( char ) c;
			return 0;
		}
		*b = '9';
	}
	abort();
}

static void fmt_xfmtexp(char *p, int e, bool upper) {
	char se [9];
	int  i = 0;
	*p++   = upper ? 'E' : 'e';
	if (e < 0) {
		*p++ = '-';
		e    = -e;
	}
	else *p++ = '+';
	while (e) {
		se [i++]  = ( char ) (e % 10 + '0');
		e        /= 10;
	}
	while (i < 2) se [i++] = '0';
	while (i > 0) *p++ = se [--i];
	*p = 0;
}

static void fmt_dtoa(double f, fmt_dec *out) {
	int    e2, e, ee, ndigit, i;
	char   tmp [FMT_SIGNIF + 10];
	double g;
	int    oerrno = errno;

	memset(out, 0, sizeof(*out));
	if (f == 0) {
		out->digits [0] = '0';
		out->digits [1] = 0;
		out->ndigits    = 1;
		out->exp        = 0;
		out->sexp       = 0;
		return;
	}
	frexp(f, &e2);
	e = ( int ) (e2 * .301029995664);
	g = f * fmt_pow10d(-e);
	while (g < 1) {
		e--;
		g = f * fmt_pow10d(-e);
	}
	while (g >= 10) {
		e++;
		g = f * fmt_pow10d(-e);
	}
	for (i = 0; i < FMT_SIGNIF; i++) {
		int d           = ( int ) g;
		out->digits [i] = ( char ) (d + '0');
		g               = (g - d) * 10;
	}
	out->digits [i]  = 0;
	e               -= FMT_SIGNIF - 1;
	fmt_xfmtexp(out->digits + FMT_SIGNIF, e, false);
	for (i = 0; i < 10; i++) {
		g = strtod(out->digits, null);
		if (f > g) {
			if (fmt_xadd1(out->digits, FMT_SIGNIF)) {
				e--;
				fmt_xfmtexp(out->digits + FMT_SIGNIF, e, false);
			}
			continue;
		}
		if (f < g) {
			if (fmt_xsub1(out->digits, FMT_SIGNIF)) {
				e++;
				fmt_xfmtexp(out->digits + FMT_SIGNIF, e, false);
			}
			continue;
		}
		break;
	}
	for (i = FMT_SIGNIF - 1; i >= FMT_SIGNIF - 3; i--) {
		int c = out->digits [i];
		if (c != '9') {
			out->digits [i] = '9';
			g               = strtod(out->digits, null);
			if (g != f) {
				out->digits [i] = ( char ) c;
				break;
			}
		}
	}
	if (out->digits [FMT_SIGNIF - 1] == '9') {
		strcpy(tmp, out->digits);
		ee = e;
		if (fmt_xadd1(tmp, FMT_SIGNIF)) {
			ee--;
			fmt_xfmtexp(tmp + FMT_SIGNIF, ee, false);
		}
		g = strtod(tmp, null);
		if (g == f) {
			strcpy(out->digits, tmp);
			e = ee;
		}
	}
	for (i = FMT_SIGNIF - 1; i >= FMT_SIGNIF - 3; i--) {
		int c = out->digits [i];
		if (c != '0') {
			out->digits [i] = '0';
			g               = strtod(out->digits, null);
			if (g != f) {
				out->digits [i] = ( char ) c;
				break;
			}
		}
	}
	ndigit = FMT_SIGNIF;
	while (ndigit > 1 && out->digits [ndigit - 1] == '0') {
		e++;
		ndigit--;
	}
	out->digits [ndigit] = 0;
	out->ndigits         = ndigit;
	out->exp             = e;
	out->sexp            = e + ndigit - 1;
	errno                = oerrno;
}

static void fmt_float_pad_out(fmt *fp, int body, char sign, bool zero_pad) {
	slitr s    = obslitr(body);
	int   slen = ( int ) s.len + (sign != 0);
	int   pad  = ((fp->flags & FMT_WIDTH) && fp->width > slen) ? fp->width - slen : 0;
	bool  zpad = !(fp->flags & FMT_LEFT) && (fp->flags & FMT_ZERO) && zero_pad;
	if (!(fp->flags & FMT_LEFT) && !zpad)
		while (pad-- > 0) fmt_putc(fp, ' ');
	if (sign) fmt_putc(fp, sign);
	if (zpad)
		while (pad-- > 0) fmt_putc(fp, '0');
	if (s.len > 0) concats(fp->sd, s);
	if (fp->flags & FMT_LEFT)
		while (pad-- > 0) fmt_putc(fp, ' ');
}

static void fmt_emit_decimal(int sd, char *digits, int point, int ndigits, int z1, int z2, char *suf, bool alt) {
	if (z1 + ndigits + z2 == point) {
		if (!alt) point++;
	}
	while (z1 > 0 || ndigits > 0 || z2 > 0) {
		char c;
		if (z1 > 0) {
			z1--;
			c = '0';
		}
		else if (ndigits > 0) {
			ndigits--;
			c = *digits++;
		}
		else {
			z2--;
			c = '0';
		}
		sd_putc(sd, c);
		if (--point == 0) sd_putc(sd, '.');
	}
	if (*suf) sd_puts(sd, suf);
}

static void fmt_trim_g(char *digits, int *ndigits, int *z1, int *z2, int point) {
	if (*z1 + *ndigits + *z2 < point) return;
	if (*z1 + *ndigits < point) {
		*z2 = point - (*z1 + *ndigits);
		return;
	}
	*z2 = 0;
	while (*z1 + *ndigits > point && *ndigits > 1 && digits [*ndigits - 1] == '0') (*ndigits)--;
}

static bool fmt_float_emit(fmt *fp, char verb) {
	/* FMT_LONG reads long double to preserve the variadic ABI, then
	   formats through the double-precision Plan 9-style decimal path.
	   A true long-double dtoa should be a separate path if ever needed. */
	double v    = (fp->flags & FMT_LONG) ? ( double ) va_arg(fp->args, long double) : va_arg(fp->args, double);
	char   sign = 0;
	int    body = mkstrand(0);
	if (body < 0) return false;
	bool finite = true;
	if (isnan(v)) {
		sd_puts(body, "NaN");
		finite = false;
	}
	else if (isinf(v)) {
		sd_puts(body, v < 0 ? "-Inf" : "+Inf");
		finite = false;
	}
	else {
		if (v < 0) {
			sign = '-';
			v    = -v;
		}
		else if (fp->flags & FMT_PLUS) sign = '+';
		else if (fp->flags & FMT_SPACE) sign = ' ';

		fmt_dec dec;
		fmt_dtoa(v, &dec);
		int   prec     = (fp->flags & FMT_PREC) ? fp->prec : 6;
		char  suf [10] = {0};
		int   point, z1, z2, e, newndigits;
		bool  alt      = (fp->flags & FMT_ALT) != 0;
		char  realverb = verb | 0x20;
		bool  upper    = verb == 'E' || verb == 'G';
		char *digits   = dec.digits;
		int   ndigits  = dec.ndigits;
		int   exp      = dec.exp;
		int   sexp     = dec.sexp;
		if (realverb == 'g' && prec == 0) prec = 1;
		if (realverb == 'e' || realverb == 'g') {
			point = 1;
			z1    = 0;
			e     = sexp;
			if (realverb == 'g') {
				if (ndigits > prec) {
					if (digits [prec] >= '5' && fmt_xadd1(digits, prec)) exp++;
					exp     += ndigits - prec;
					ndigits  = prec;
					e        = exp + (ndigits - 1);
				}
				if (-4 <= e && e < prec) goto casef;
				prec--;
			}
			if (1 + prec >= ndigits) z2 = 1 + prec - ndigits;
			else {
				newndigits = 1 + prec;
				if (digits [newndigits] >= '5' && fmt_xadd1(digits, newndigits)) e++;
				ndigits = newndigits;
				z2      = 0;
			}
			if (realverb == 'g' && !alt) fmt_trim_g(digits, &ndigits, &z1, &z2, point);
			fmt_xfmtexp(suf, e, upper);
			fmt_emit_decimal(body, digits, point, ndigits, z1, z2, suf, alt);
		}
		else {
casef:
			if (ndigits + exp > 0) {
				point = ndigits + exp;
				z1    = 0;
			}
			else {
				point = 1;
				z1    = 1 + -(ndigits + exp);
			}
			if (realverb == 'g') prec += z1 - point;
			if (point + prec >= z1 + ndigits) z2 = point + prec - (z1 + ndigits);
			else {
				newndigits = point + prec - z1;
				if (newndigits < 0) {
					z1         += newndigits;
					newndigits  = 0;
				}
				else if (newndigits == 0) {
					if (digits [0] >= '5') {
						digits [0] = '1';
						newndigits = 1;
						if (z1) z1--;
						else point++;
					}
				}
				else if (digits [newndigits] >= '5' && fmt_xadd1(digits, newndigits)) {
					digits [newndigits++] = '0';
					if (z1) z1--;
					else point++;
				}
				z2      = 0;
				ndigits = newndigits;
			}
			if (realverb == 'g' && !alt) fmt_trim_g(digits, &ndigits, &z1, &z2, point);
			fmt_emit_decimal(body, digits, point, ndigits, z1, z2, "", alt);
		}
	}
	fmt_float_pad_out(fp, body, sign, finite);
	rmstrand(body);
	return true;
}

/* ── Format string parser ───────────────────────────── */

static void fmt_apply_width(fmt *fp, fmt_spec *spec) {
	if (!spec->has_width) return;
	fp->flags |= FMT_WIDTH;
	fp->width  = spec->width_star ? va_arg(fp->args, int) : spec->width;
	if (fp->width < 0) {
		fp->flags |= FMT_LEFT;
		fp->width  = -fp->width;
	}
}

static void fmt_apply_prec(fmt *fp, fmt_spec *spec) {
	if (!spec->has_prec) return;
	fp->flags |= FMT_PREC;
	fp->prec   = spec->prec_star ? va_arg(fp->args, int) : spec->prec;
	if (fp->prec < 0) {
		fp->flags &= ~FMT_PREC;
		fp->prec   = 0;
	}
}

static char *parse_fmt(char *fm, fmt *fp) {
	fmt_spec spec = fmt_scan(fm - 1);
	fp->flags     = spec.flags;
	fp->verb      = spec.verb;
	fmt_apply_width(fp, &spec);
	fmt_apply_prec(fp, &spec);
	return spec.end;
}

/* ── Main formatting functions ──────────────────────── */

bool dofmt(fmt *fp) {
	if (!fp->verb) return false;

	if (fp->verb != 'a') fp->flags &= ~(( uint ) FMT_FLOAT | ( uint ) FMT_ARRST);

	/* check custom verbs first */
	for (int i = 0; i < nverbs; i++)
		if (verb_registry [i].c == fp->verb) return verb_registry [i].fn(fp);

	/* built-in verbs */
	switch (fp->verb) {
	case 'd' :
	case 'i' : return fmt_verb_d(fp);
	case 'x' : fmt_int(fp, fmt_arg_unsigned(fp), 16, false, "0123456789abcdef"); return true;
	case 'X' : fmt_int(fp, fmt_arg_unsigned(fp), 16, false, "0123456789ABCDEF"); return true;
	case 'o' : fmt_int(fp, fmt_arg_unsigned(fp), 8, false, "01234567"); return true;
	case 'b' : fmt_int(fp, fmt_arg_unsigned(fp), 2, false, "01"); return true;
	case 's' : return fmt_verb_s(fp);
	case 't' : return fmt_verb_t(fp);
	case 'S' : return fmt_verb_S(fp);
	case 'c' : return fmt_verb_c(fp);
	case 'C' : return fmt_verb_C(fp);
	case 'p' : return fmt_verb_p(fp);
	case 'r' : return fmt_verb_r(fp);
	case 'a' : return fmt_verb_a(fp);
	case 'f' :
	case 'e' :
	case 'E' :
	case 'g' :
	case 'G' : return fmt_float_emit(fp, ( char ) fp->verb);
	case '%' : fmt_putc(fp, '%'); return true;
	default  : return false;
	}
}

static char *fmt_emit_literal_run(int sd, char *p) {
	char *start = p;
	while (*p && *p != '%') p++;
	if (p > start) concats(sd, mkslitr(start, ( uvlong ) (p - start)));
	return p;
}

static bool (*fmt_custom_fn(rune c))(fmt *) {
	for (int i = 0; i < nverbs; i++)
		if (verb_registry [i].c == c) return verb_registry [i].fn;
	return null;
}

int vfmts(int arena, char *fm, va_list args) {
	if (!fm) {
		errmsg("vfmts: bad format");
		return -1;
	}

	int sd = mkstrand(arena);
	if (sd < 0) return -1;

	fmt fp;
	memset(&fp, 0, sizeof(fp));
	fp.sd = sd;

	/* single va_copy: verbs advance it sequentially */
	va_list va;
	va_copy(va, args);

	char *p = fm;
	while (*p) {
		p = fmt_emit_literal_run(fp.sd, p);
		if (!*p) break;
		p++; /* skip '%' */
		if (*p == '\0') break;

		/* parse format spec, using the shared va */
		memset(&fp, 0, sizeof(fp));
		fp.sd = sd;
		va_copy(fp.args, va);

		uint custom_flags = 0;
		bool done         = false;
		for (;;) {
			p         = parse_fmt(p, &fp);
			fp.flags |= custom_flags;
			if (!fp.verb) break;
			bool (*custom)(fmt *) = fmt_custom_fn(fp.verb);
			if (!custom) break;
			done = custom(&fp);
			if (done) break;
			custom_flags = fp.flags;
			memset(&fp, 0, sizeof(fp));
			fp.sd = sd;
			va_end(fp.args);
			va_copy(fp.args, va);
		}

		if (fp.verb && !done) dofmt(&fp);
		va_end(va);
		va_copy(va, fp.args);
		va_end(fp.args);
	}

	va_end(va);
	return sd;
}

int fmts(int arena, char *fm, ...) {
	va_list args;
	va_start(args, fm);
	int sd = vfmts(arena, fm, args);
	va_end(args);
	return sd;
}
