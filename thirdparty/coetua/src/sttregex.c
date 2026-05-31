#include "sttregex.h"
#include "regex9.h"
#include "arena.h"
#include "err.h"
#include "text.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ── linear command stream, no tail-command ownership ── */

typedef enum
{
	CT_X,
	CT_Y,
	CT_G,
	CT_V,
	CT_S,
	CT_P,
	CT_D,
	CT_C,
	CT_A,
	CT_I,
} CmdType;

typedef struct {
	CmdType type;
	Reprog *re;
	char   *txt;
	uvlong  tlen;
} Cmd;

/* ── parser ─────────────────────────────────────────── */

static char *read_delimited(char **sp, int arena, bool *fatal) {
	char  *s   = *sp;
	uvlong cap = 64, len = 0;
	char  *buf = aden(arena, cap);
	if (!buf) {
		*fatal = true;
		return null;
	}
	while (*s && *s != '/') {
		if (*s == '\\' && s [1] == '/') s++;
		if (len + 2 > cap) {
			uvlong ncap;
			if (!mulok64(cap, 2, &ncap)) {
				errmsg("sttregex: field too large");
				*fatal = true;
				return null;
			}
			char *nb = aden(arena, ncap);
			if (!nb) {
				*fatal = true;
				return null;
			}
			memcpy(nb, buf, len);
			buf = nb;
			cap = ncap;
		}
		buf [len++] = *s++;
	}
	if (*s != '/') {
		errmsg("unterminated sttregex field");
		return null;
	}
	s++;
	buf [len] = 0;
	*sp       = s;
	return buf;
}

static Cmd *parse(int arena, char *se, int *nout, bool *fatal) {
	uvlong cc = 8;
	Cmd   *ca = calloc(( size_t ) cc, sizeof(Cmd));
	int    nc = 0;
	if (!ca) {
		errmsg("sttregex: out of memory");
		*fatal = true;
		return null;
	}

	while (*se) {
		if (*se == ' ') {
			se++;
			continue;
		}

		if (nc >= ( int ) cc) {
			uvlong ncc;
			if (!mulok64(cc, 2, &ncc) || ncc > ( uvlong ) (SIZE_MAX / sizeof(Cmd))) {
				errmsg("sttregex: command overflow");
				*fatal = true;
				goto fail;
			}
			cc       = ncc;
			Cmd *nca = realloc(ca, ( size_t ) (cc * sizeof(Cmd)));
			if (!nca) {
				errmsg("sttregex: out of memory");
				*fatal = true;
				free(ca);
				return null;
			}
			ca = nca;
		}
		Cmd *c = &ca [nc];
		memset(c, 0, sizeof(*c));

		switch (*se) {
		case 'p' :
			c->type = CT_P;
			se++;
			break;
		case 'd' :
			c->type = CT_D;
			se++;
			break;
		case 'c' : c->type = CT_C; goto onefield;
		case 'a' : c->type = CT_A; goto onefield;
		case 'i' : c->type = CT_I; goto onefield;
		case 'x' : c->type = CT_X; goto onefield;
		case 'y' : c->type = CT_Y; goto onefield;
		case 'g' : c->type = CT_G; goto onefield;
		case 'v' : c->type = CT_V; goto onefield;
		case 's' :
			c->type = CT_S;
			se++;
			if (*se != '/') {
				errmsg("sttregex: s needs /regex/");
				goto fail;
			}
			se++;
			char *field = read_delimited(&se, arena, fatal);
			if (!field) goto fail;
			c->re = regcomp9(arena, field);
			if (!c->re) goto fail;
			c->txt = read_delimited(&se, arena, fatal);
			if (!c->txt) goto fail;
			c->tlen = strlen(c->txt);
			nc++;
			continue;
		default : errmsg("unknown sttregex command"); goto fail;
		}
		nc++;
		continue;

onefield:
		se++;
		if (*se != '/') {
			errmsg("sttregex: command needs /field/");
			goto fail;
		}
		se++;
		if (c->type == CT_X || c->type == CT_Y || c->type == CT_G || c->type == CT_V) {
			char *field = read_delimited(&se, arena, fatal);
			if (!field) goto fail;
			c->re = regcomp9(arena, field);
			if (!c->re) goto fail;
		}
		else {
			c->txt = read_delimited(&se, arena, fatal);
			if (!c->txt) goto fail;
			c->tlen = strlen(c->txt);
		}
		nc++;
	}
	*nout = nc;
	return ca;
fail:
	free(ca);
	return null;
}

/* ── replacement expansion ──────────────────────────── */

static void expand(int sd, char *txt, uvlong tlen, slitr work, Resub *caps, int nc) {
	char *p   = txt;
	char *end = txt + tlen;
	while (p < end) {
		if (*p == '&') {
			p++;
			concats(sd, work);
			continue;
		}
		if (*p == '\\' && p + 1 < end) {
			char nx = p [1];
			if (nx == '&') {
				p += 2;
				concat(sd, "&");
				continue;
			}
			if (nx >= '1' && nx <= '9') {
				int id  = nx - '0';
				p      += 2;
				if (id < nc && caps [id].kind == RSUB_TEXT && caps [id].s.sp) {
					slitr c = mkslitr(caps [id].s.sp, ( uvlong ) (caps [id].e.ep - caps [id].s.sp));
					concats(sd, c);
				}
				else if (id < nc && caps [id].kind == RSUB_QUANTITY) {
					char buf [32];
					int  n = snprintf(buf, sizeof(buf), "%llu", caps [id].s.q);
					concats(sd, mkslitr(buf, ( uvlong ) n));
				}
				continue;
			}
			concats(sd, mkslitr(p, 2));
			p += 2;
			continue;
		}
		concatr(sd, ( uchar ) *p++);
	}
}

/* ── executor ───────────────────────────────────────── */

static bool subchain_pick(Cmd *ca, int nc, int idx) {
	while (idx < nc) {
		switch (ca [idx].type) {
		case CT_P : return true;
		case CT_D :
		case CT_C :
		case CT_A :
		case CT_I :
		case CT_S : return false;
		case CT_G :
		case CT_V : idx++; break;
		case CT_X :
		case CT_Y : return subchain_pick(ca, nc, idx + 1);
		default   : return false;
		}
	}
	return false;
}

static Resub *merge_caps(Resub *outer, int nouter, Resub *inner, int ninner, int *nout) {
	int    base  = nouter > 0 ? nouter : 1;
	int    total = base + (ninner > 0 ? ninner - 1 : 0);
	Resub *m     = calloc(( size_t ) total, sizeof(Resub));
	if (!m) {
		errmsg("sttregex: capture allocation failed");
		return null;
	}
	if (outer && nouter > 0) memcpy(m, outer, ( size_t ) nouter * sizeof(Resub));
	for (int i = 1; i < ninner; i++) m [base + i - 1] = inner [i];
	*nout = total;
	return m;
}

static int  step(Cmd *ca, int nc, int idx, slitr in, int out, Resub *caps, int ncap, int arena, Resub **ocaps,
                 int *oncap);
static int  exec_from(Cmd *ca, int nc, int start, slitr in, int out, Resub *caps, int ncap, int arena);

static void handoff_caps(Resub **ocaps, int *oncap, Resub *caps, int ncap) {
	if (!ocaps || !oncap || !caps || ncap <= 0) return;
	free(*ocaps);
	*ocaps = caps;
	*oncap = ncap;
}

static int step(Cmd *ca, int nc, int idx, slitr in, int out, Resub *caps, int ncap, int arena, Resub **ocaps,
                int *oncap) {
	if (idx >= nc) {
		concats(out, in);
		return nc;
	}
	Cmd *c = &ca [idx];

	switch (c->type) {
	case CT_P : concats(out, in); return idx + 1;
	case CT_D : return idx + 1;
	case CT_C : expand(out, c->txt, c->tlen, in, caps, ncap); return idx + 1;
	case CT_A :
		concats(out, in);
		expand(out, c->txt, c->tlen, in, caps, ncap);
		return idx + 1;
	case CT_I :
		expand(out, c->txt, c->tlen, in, caps, ncap);
		concats(out, in);
		return idx + 1;

	case CT_G :
	case CT_V : {
		Resub m [64];
		memset(m, 0, sizeof(m));
		int ok = regexec9(c->re, in.s, in.len, m, 64);
		if (ok < 0) return -1;
		bool pass = (c->type == CT_G) ? (ok > 0) : (ok <= 0);
		if (!pass) {
			if (!subchain_pick(ca, nc, idx + 1)) concats(out, in);
			return -1;
		}
		return step(ca, nc, idx + 1, in, out, caps, ncap, arena, ocaps, oncap);
	}

	case CT_S : {
		int tmp = mkstrand(arena);
		if (tmp < 0) return -1;
		char *p   = in.s;
		char *eos = in.s + in.len;
		while (p < eos) {
			Resub m [64];
			memset(m, 0, sizeof(m));
			uvlong remain = ( uvlong ) (eos - p);
			int    ok     = regexec9(c->re, p, remain, m, 64);
			if (ok < 0) {
				rmstrand(tmp);
				return -1;
			}
			if (ok == 0) {
				concats(tmp, mkslitr(p, ( uvlong ) (eos - p)));
				break;
			}
			if (m [0].s.sp == m [0].e.ep) {
				int step = chartorune(&(rune) {0}, p);
				if (step <= 0) step = 1;
				concats(tmp, mkslitr(p, ( uvlong ) step));
				p += step;
				continue;
			}
			concats(tmp, mkslitr(p, ( uvlong ) (m [0].s.sp - p)));
			int    isn = ( int ) c->re->nsub;
			int    nmerged;
			Resub *merged = merge_caps(caps, ncap, m, isn, &nmerged);
			if (!merged) {
				rmstrand(tmp);
				return -1;
			}
			expand(tmp, c->txt, c->tlen, mkslitr(m [0].s.sp, ( uvlong ) (m [0].e.ep - m [0].s.sp)), merged, nmerged);
			free(merged);
			p = m [0].e.ep;
		}
		slitr subbed = obslitr(tmp);
		int   ret    = step(ca, nc, idx + 1, subbed, out, caps, ncap, arena, ocaps, oncap);
		rmstrand(tmp);
		return ret;
	}

	case CT_X :
	case CT_Y : {
		bool  pick = subchain_pick(ca, nc, idx + 1);
		int   subn = 0;
		char *p    = in.s;
		char *eos  = in.s + in.len;
		while (p < eos) {
			Resub m [64];
			memset(m, 0, sizeof(m));
			uvlong remain = ( uvlong ) (eos - p);
			int    ok     = regexec9(c->re, p, remain, m, 64);
			if (ok < 0) return -1;
			if (ok == 0) break;
			if (m [0].s.sp == m [0].e.ep) {
				int step = chartorune(&(rune) {0}, p);
				if (step <= 0) step = 1;
				if (!pick) concats(out, mkslitr(p, ( uvlong ) step));
				p += step;
				continue;
			}
			/* gap / pre-match: X keeps in edit, discards in pick; Y runs sub-chain on it */
			if (c->type == CT_X) {
				if (!pick) concats(out, mkslitr(p, ( uvlong ) (m [0].s.sp - p)));

				int    isn = ( int ) c->re->nsub;
				int    nmerged;
				Resub *merged = merge_caps(caps, ncap, m, isn, &nmerged);
				if (!merged) return -1;

				slitr mtx = mkslitr(m [0].s.sp, ( uvlong ) (m [0].e.ep - m [0].s.sp));
				int   sub = mkstrand(arena);
				if (sub < 0) {
					free(merged);
					return -1;
				}
				Resub *child_caps = null;
				int    child_ncap = 0;
				int    ret        = step(ca, nc, idx + 1, mtx, sub, merged, nmerged, arena, &child_caps, &child_ncap);
				if (ret >= 0) {
					concats(out, obslitr(sub));
					subn = ret;
					if (child_caps) {
						handoff_caps(ocaps, oncap, child_caps, child_ncap);
						free(merged);
						merged = null;
					}
					else {
						handoff_caps(ocaps, oncap, merged, nmerged);
						merged = null;
					}
				}
				else if (!pick) { concats(out, mtx); }
				rmstrand(sub);
				free(merged);
			}
			else {
				/* Y: run sub-chain on the gap, keep match text in edit mode */
				slitr gap = mkslitr(p, ( uvlong ) (m [0].s.sp - p));
				if (gap.len > 0) {
					int sub = mkstrand(arena);
					if (sub < 0) return -1;
					Resub *child_caps = null;
					int    child_ncap = 0;
					int    ret        = step(ca, nc, idx + 1, gap, sub, caps, ncap, arena, &child_caps, &child_ncap);
					if (ret >= 0) {
						concats(out, obslitr(sub));
						subn = ret;
						handoff_caps(ocaps, oncap, child_caps, child_ncap);
					}
					else if (!pick) { concats(out, gap); }
					rmstrand(sub);
				}
				if (!pick) concats(out, mkslitr(m [0].s.sp, ( uvlong ) (m [0].e.ep - m [0].s.sp)));
			}
			p = m [0].e.ep;
		}
		/* trailing text: for Y, run sub-chain on it; for X, keep in edit mode */
		if (c->type == CT_Y && p < eos) {
			slitr trail = mkslitr(p, ( uvlong ) (eos - p));
			int   sub   = mkstrand(arena);
			if (sub < 0) return -1;
			Resub *child_caps = null;
			int    child_ncap = 0;
			int    ret        = step(ca, nc, idx + 1, trail, sub, caps, ncap, arena, &child_caps, &child_ncap);
			if (ret >= 0) {
				concats(out, obslitr(sub));
				if (!subn) subn = ret;
				handoff_caps(ocaps, oncap, child_caps, child_ncap);
			}
			else if (!pick) { concats(out, trail); }
			rmstrand(sub);
		}
		if (c->type == CT_X && !pick && p < eos) concats(out, mkslitr(p, ( uvlong ) (eos - p)));
		if (ncap > 0 && subn > 0 && subn < nc) {
			slitr cur  = obslitr(out);
			int   cont = mkstrand(arena);
			if (cont < 0) return -1;
			Resub *cc = (ocaps && *ocaps) ? *ocaps : caps;
			int    cn = (ocaps && *ocaps) ? *oncap : ncap;
			if (exec_from(ca, nc, subn, cur, cont, cc, cn, arena) < 0) {
				rmstrand(cont);
				return -1;
			}
			dropstr(out, 0, ( vlong ) cur.len);
			concats(out, obslitr(cont));
			rmstrand(cont);
			return nc;
		}
		return (subn > 0) ? subn : nc;
	}

	default : concats(out, in); return nc;
	}
}

static int exec_from(Cmd *ca, int nc, int start, slitr in, int out, Resub *caps, int ncap, int arena) {
	int    idx      = start;
	int    cur_sd   = mkstrand(arena);
	Resub *cur_caps = null;
	int    cur_ncap = 0;
	if (cur_sd < 0) return -1;
	concats(cur_sd, in);
	if (caps && ncap > 0) {
		cur_caps = calloc(( size_t ) ncap, sizeof(Resub));
		if (!cur_caps) {
			rmstrand(cur_sd);
			errmsg("sttregex: capture allocation failed");
			return -1;
		}
		memcpy(cur_caps, caps, ( size_t ) ncap * sizeof(Resub));
		cur_ncap = ncap;
	}

	while (idx >= 0 && idx < nc) {
		int nxt_sd = mkstrand(arena);
		if (nxt_sd < 0) {
			rmstrand(cur_sd);
			free(cur_caps);
			return -1;
		}
		slitr  cur       = obslitr(cur_sd);
		Resub *next_caps = null;
		int    next_ncap = 0;
		int    next      = step(ca, nc, idx, cur, nxt_sd, cur_caps, cur_ncap, arena, &next_caps, &next_ncap);
		if (next < 0) {
			rmstrand(cur_sd);
			cur_sd = nxt_sd;
			free(cur_caps);
			cur_caps = next_caps;
			cur_ncap = next_ncap;
			break;
		}
		rmstrand(cur_sd);
		cur_sd = nxt_sd;
		idx    = next;
		if (next_caps) {
			free(cur_caps);
			cur_caps = next_caps;
			cur_ncap = next_ncap;
		}
	}
	concats(out, obslitr(cur_sd));
	rmstrand(cur_sd);
	free(cur_caps);
	return 0;
}

static int exec_all(Cmd *ca, int nc, slitr in, int out, Resub *caps, int ncap, int arena) {
	return exec_from(ca, nc, 0, in, out, caps, ncap, arena);
}

/* ── top-level entry ────────────────────────────────── */

int sttregex(int arena, char *str, char *se) {
	if (!str || !se) return errmsg("null sttregex argument"), -1;
	errmsg(null);

	int  nc    = 0;
	bool fatal = false;
	Cmd *ca    = parse(arena, se, &nc, &fatal);
	if (!ca) {
		if (fatal) return -1;
		int sd = mkstrand(arena);
		if (sd >= 0) concat(sd, str);
		return sd;
	}

	int out = mkstrand(arena);
	if (out < 0) {
		free(ca);
		return -1;
	}

	slitr in  = slitr_of(str);
	int   ret = exec_all(ca, nc, in, out, null, 0, arena);
	free(ca);

	if (ret < 0 && !err()) errmsg("sttregex chain failed");
	return out;
}
