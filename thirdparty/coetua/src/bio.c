#include "bio.h"
#include "arena.h"
#include "config.h"
#include "err.h"
#include "fmt.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>

typedef struct bio bio;

struct bio {
	int    fd;
	int    arena;
	omode  mode;
	uchar *buf;
	uvlong pos;
	uvlong len;
	uchar  last [5];
	int    lastn;
	uchar  unget [5];
	int    ungetn;
	bool   live;
	bool   owned;
	bool   writing;
};

/* Thread-local: a bio is opened, used, and closed within one thread, so the
   descriptor table is per-thread. Lock-free, matching the arena/strand/fd tables. */
static _Thread_local bio *bios;
static _Thread_local int  biocap;

static bool bio_flush_write(bio *b);
static bool bio_fill(bio *b);

static void bio_forget_get(bio *b) {
	b->lastn  = 0;
	b->ungetn = 0;
}

static void bio_remember(bio *b, uchar *s, int n) {
	if (!b || n <= 0 || n > 5) return;
	memcpy(b->last, s, ( size_t ) n);
	b->lastn = n;
}

static void bio_pushback(bio *b, uchar *s, int n) {
	if (!b || n <= 0 || n > 5 || b->ungetn + n > 5) return;
	for (int i = n - 1; i >= 0; i--) b->unget [b->ungetn++] = s [i];
}

static void bio_merge_pushback(bio *b) {
	if (!b || b->ungetn <= 0) return;
	int    pushed = b->ungetn;
	uvlong unread = b->len > b->pos ? b->len - b->pos : 0;
	if (unread > 0) memmove(b->buf + pushed, b->buf + b->pos, unread);
	for (int i = 0; i < pushed; i++) b->buf [i] = b->unget [pushed - 1 - i];
	b->ungetn = 0;
	b->pos    = 0;
	b->len    = unread + ( uvlong ) pushed;
}

static bool bio_table_init(void) {
	if (bios) return true;
	biocap = COETUA_BIO_TABLE_SEED > 0 ? COETUA_BIO_TABLE_SEED : 1;
	bios   = ( bio * ) calloc(( size_t ) biocap, sizeof(bio));
	if (!bios) {
		errmsg("bio: out of memory");
		biocap = 0;
		return false;
	}
	return true;
}

static bool bio_table_grow(void) {
	int newcap = biocap ? biocap * 2 : COETUA_BIO_TABLE_SEED;
	if (newcap < 1) newcap = 1;
	if (newcap <= biocap) {
		errmsg("bio: descriptor overflow");
		return false;
	}
	bio *p = ( bio * ) realloc(bios, ( size_t ) newcap * sizeof(bio));
	if (!p) {
		errmsg("bio: out of memory");
		return false;
	}
	memset(p + biocap, 0, ( size_t ) (newcap - biocap) * sizeof(bio));
	bios   = p;
	biocap = newcap;
	return true;
}

static bio *bio_get(int bd) {
	if (!bio_table_init() || bd < 0 || bd >= biocap || !bios [bd].live) return null;
	return &bios [bd];
}

static bio *bio_get_err(int bd, char *who) {
	bio *b = bio_get(bd);
	if (!b && !err()) errmsg(who);
	return b;
}

static void bio_reset_slot(bio *b, int arena) { *b = (bio) {.fd = -1, .arena = arena, .live = true}; }

static bool bio_ensure_buf(bio *b) {
	if (b->buf) return true;
	b->buf = ( uchar * ) aden(b->arena, COETUA_BIO_BUFSZ);
	if (!b->buf) return false;
	return true;
}

int mkbio(int arena) {
	if (!bio_table_init()) return -1;
	for (;;) {
		for (int i = 0; i < biocap; i++) {
			if (!bios [i].live) {
				bio_reset_slot(&bios [i], arena);
				if (!bio_ensure_buf(&bios [i])) {
					bios [i].live = false;
					return -1;
				}
				return i;
			}
		}
		if (!bio_table_grow()) return -1;
	}
}

void binit(int bd, int fd, omode mod) {
	bio *b = bio_get_err(bd, "binit: bad buffer");
	if (!b) return;
	if (b->fd >= 0) {
		bflush(bd);
		if (b->owned) dclose(b->fd);
	}
	b->fd      = fd;
	b->mode    = mod;
	b->pos     = 0;
	b->len     = 0;
	b->lastn   = 0;
	b->ungetn  = 0;
	b->owned   = false;
	b->writing = false;
}

static bool bio_writes(omode mod) { return mod.w || mod.x || mod.t; }

int         bopen(int arena, char *file, omode mod) {
	if (!file) {
		errmsg("bopen: bad path");
		return -1;
	}
	int bd = mkbio(arena);
	if (bd < 0) return -1;
	int fd = bio_writes(mod) ? dcreate(file, mod, (perm) {.bits = 0666}) : dopen(file, mod);
	if (fd < 0) {
		rmbio(bd);
		return -1;
	}
	binit(bd, fd, mod);
	bios [bd].owned = true;
	return bd;
}

int bfildes(int bd) {
	bio *b = bio_get_err(bd, "bfildes: bad buffer");
	return b ? b->fd : -1;
}

static bool bio_can_read(bio *b, char *who) {
	if (!b || b->fd < 0) {
		errmsg(who);
		return false;
	}
	if (!b->mode.r) {
		errmsg("bio: buffer is not open for reading");
		return false;
	}
	if (b->writing) {
		if (!bio_flush_write(b)) return false;
		b->writing = false;
	}
	return true;
}

static bool bio_read_byte(bio *b, uchar *c) {
	if (b->ungetn > 0) {
		*c = b->unget [--b->ungetn];
		return true;
	}
	if (!bio_fill(b)) return false;
	*c = b->buf [b->pos++];
	return true;
}

static bool bio_fill(bio *b) {
	if (b->pos < b->len) return true;
	vlong n = dread(b->fd, b->buf, COETUA_BIO_BUFSZ);
	if (n < 0) return false;
	b->pos = 0;
	b->len = ( uvlong ) n;
	return n > 0;
}

static uvlong find_delim_byte(uchar *s, uvlong n, uchar delim, bool *found) {
	for (uvlong i = 0; i < n; i++) {
		if (s [i] == delim) {
			*found = true;
			return i + 1;
		}
	}
	*found = false;
	return n;
}

arrst brdline(int bd, rune delim) {
	bio *b = bio_get(bd);
	if (!bio_can_read(b, "brdline: bad buffer")) return mkarrst(0, null);
	b->lastn = 0;
	bio_merge_pushback(b);
	if (!bio_fill(b)) return mkarrst(0, null);
	if (delim > 0xff) delim = '\n';

	uchar *start = b->buf + b->pos;
	uvlong avail = b->len - b->pos;
	bool   found;
	uvlong take = find_delim_byte(start, avail, ( uchar ) delim, &found);
	( void ) found;
	b->pos += take;
	return mkarrst(take, start);
}

int brdstr(int bd, int arena, rune delim, bool nulldelim) {
	int sd = mkstrand(arena);
	if (sd < 0) return -1;
	for (;;) {
		rune r = bgetrune(bd);
		if (r == ( rune ) -1) break;
		if (r == delim) {
			if (!nulldelim) concatr(sd, r);
			break;
		}
		concatr(sd, r);
	}
	if (err()) {
		rmstrand(sd);
		return -1;
	}
	return sd;
}

static bool write_all(int fd, uchar *buf, uvlong len) {
	uvlong off = 0;
	while (off < len) {
		vlong n = dwrite(fd, buf + off, len - off);
		if (n <= 0) return false;
		off += ( uvlong ) n;
	}
	return true;
}

static bool bio_flush_write(bio *b) {
	if (!write_all(b->fd, b->buf, b->pos)) return false;
	b->pos = 0;
	b->len = 0;
	bio_forget_get(b);
	return true;
}

vlong bread(int bd, void *buf, uvlong len) {
	bio *b = bio_get(bd);
	if (!b || b->fd < 0 || (!buf && len)) {
		errmsg("bread: bad buffer");
		return -1;
	}
	if (!b->mode.r) {
		errmsg("bread: buffer is not open for reading");
		return -1;
	}
	if (b->writing) {
		if (!bio_flush_write(b)) return -1;
		b->writing = false;
	}
	b->lastn   = 0;
	uchar *out = ( uchar * ) buf;
	uvlong got = 0;
	while (got < len && b->ungetn > 0) out [got++] = b->unget [--b->ungetn];
	while (got < len) {
		if (b->pos == b->len) {
			vlong n = dread(b->fd, b->buf, COETUA_BIO_BUFSZ);
			if (n < 0) return got ? ( vlong ) got : -1;
			if (n == 0) break;
			b->pos = 0;
			b->len = ( uvlong ) n;
		}
		uvlong avail = b->len - b->pos;
		uvlong take  = len - got < avail ? len - got : avail;
		memcpy(out + got, b->buf + b->pos, take);
		b->pos += take;
		got    += take;
	}
	b->lastn = 0;
	return ( vlong ) got;
}

vlong bwrite(int bd, void *buf, uvlong len) {
	bio *b = bio_get(bd);
	if (!b || b->fd < 0 || (!buf && len)) {
		errmsg("bwrite: bad buffer");
		return -1;
	}
	if (!b->mode.w && !b->mode.a) {
		errmsg("bwrite: buffer is not open for writing");
		return -1;
	}
	if (!b->writing) {
		uvlong unread = (b->len > b->pos ? b->len - b->pos : 0) + ( uvlong ) b->ungetn;
		if (!b->mode.a && unread && dseek(b->fd, -( vlong ) unread, 1) < 0) return -1;
		b->pos     = 0;
		b->len     = 0;
		b->writing = true;
	}
	bio_forget_get(b);
	uchar *in  = ( uchar * ) buf;
	uvlong put = 0;
	while (put < len) {
		uvlong avail = COETUA_BIO_BUFSZ - b->pos;
		if (avail == 0) {
			if (!bio_flush_write(b)) return put ? ( vlong ) put : -1;
			avail = COETUA_BIO_BUFSZ;
		}
		uvlong take = len - put < avail ? len - put : avail;
		memcpy(b->buf + b->pos, in + put, take);
		b->pos += take;
		put    += take;
	}
	return ( vlong ) put;
}

uvlong bbuffered(int bd) {
	bio *b = bio_get_err(bd, "bbuffered: bad buffer");
	if (!b || b->fd < 0) return 0;
	if (b->writing) return b->pos;
	return (b->len > b->pos ? b->len - b->pos : 0) + ( uvlong ) b->ungetn;
}

vlong boffset(int bd) {
	bio *b = bio_get(bd);
	if (!b || b->fd < 0) {
		errmsg("boffset: bad buffer");
		return -1;
	}
	vlong off = dseek(b->fd, 0, 1);
	if (off < 0) return -1;
	if (b->writing) return off + ( vlong ) b->pos;
	return off - ( vlong ) bbuffered(bd);
}

vlong bseek(int bd, vlong amount, int from) {
	bio *b = bio_get(bd);
	if (!b || b->fd < 0) {
		errmsg("bseek: bad buffer");
		return -1;
	}
	vlong base;
	switch (from) {
	case 0 : base = 0; break;
	case 1 :
		base = boffset(bd);
		if (base < 0) return -1;
		break;
	case 2 :
		if (b->writing && !bio_flush_write(b)) return -1;
		b->writing = false;
		base       = dseek(b->fd, 0, 2);
		if (base < 0) return -1;
		break;
	default : errmsg("bseek: bad whence"); return -1;
	}
	vlong target = base + amount;
	if ((amount > 0 && target < base) || (amount < 0 && target > base)) {
		errmsg("bseek: offset overflow");
		return -1;
	}
	if (b->writing && !bio_flush_write(b)) return -1;
	vlong off = dseek(b->fd, target, 0);
	if (off < 0) return -1;
	b->pos     = 0;
	b->len     = 0;
	b->writing = false;
	bio_forget_get(b);
	return off;
}

int bgetc(int bd) {
	uchar c;
	bio  *b = bio_get(bd);
	if (!bio_can_read(b, "bgetc: bad buffer")) return -1;
	if (!bio_read_byte(b, &c)) return -1;
	bio_remember(b, &c, 1);
	return ( int ) c;
}

void bungetc(int bd) {
	bio *b = bio_get(bd);
	if (!b || b->writing || b->lastn != 1) return;
	bio_pushback(b, b->last, 1);
	b->lastn = 0;
}

int bputc(int bd, char c) {
	vlong n = bwrite(bd, &c, 1);
	return n == 1 ? 1 : -1;
}

rune bgetrune(int bd) {
	bio *b = bio_get(bd);
	if (!bio_can_read(b, "bgetrune: bad buffer")) return ( rune ) -1;
	uchar seq [5];
	char  buf [5];
	uchar c;
	if (!bio_read_byte(b, &c)) return ( rune ) -1;
	seq [0] = c;
	buf [0] = ( char ) c;
	if ((( uchar ) buf [0]) < 0x80) {
		bio_remember(b, seq, 1);
		return ( rune ) ( uchar ) buf [0];
	}

	for (int i = 1; i < 4; i++) {
		if (fullrune(buf, i)) {
			rune r;
			int  n = chartorune(&r, buf);
			bio_remember(b, seq, n);
			return r;
		}
		if (!bio_read_byte(b, &c)) {
			b->lastn = 0;
			errmsg("bgetrune: incomplete UTF");
			return ( rune ) -1;
		}
		seq [i] = c;
		buf [i] = ( char ) c;
	}

	rune r;
	int  n = chartorune(&r, buf);
	bio_remember(b, seq, n);
	return r;
}

void bungetrune(int bd) {
	bio *b = bio_get(bd);
	if (!b || b->writing || b->lastn <= 0) return;
	bio_pushback(b, b->last, b->lastn);
	b->lastn = 0;
}

int bputrune(int bd, rune r) {
	char  buf [4];
	int   n   = runetochar(buf, &r);
	vlong put = bwrite(bd, buf, ( uvlong ) n);
	return put == n ? n : -1;
}

int bvprint(int bd, char *fm, va_list args) {
	int sd = vfmts(0, fm, args);
	if (sd < 0) return -1;
	slitr s = obslitr(sd);
	vlong n = s.len > 0 ? bwrite(bd, s.s, s.len) : 0;
	rmstrand(sd);
	return n < 0 ? -1 : ( int ) n;
}

int bprint(int bd, char *fm, ...) {
	va_list args;
	va_start(args, fm);
	int n = bvprint(bd, fm, args);
	va_end(args);
	return n;
}

static double bio_pow10(int n) {
	double d = 1.0;
	while (n-- > 0) d *= 10.0;
	return d;
}

double bgetd(int bd) {
	bio   *b    = bio_get(bd);
	double num  = 0.0;
	int    neg  = 0;
	int    dig  = 0;
	int    exp  = 0;
	int    eneg = 0;
	bool   got  = false;

	int    c    = bgetc(bd);
	while (c == ' ' || c == '\t') c = bgetc(bd);
	if (c == '-' || c == '+') {
		neg = c == '-';
		c   = bgetc(bd);
	}
	while (c >= '0' && c <= '9') {
		num = num * 10.0 + ( double ) (c - '0');
		got = true;
		c   = bgetc(bd);
	}
	if (c == '.') c = bgetc(bd);
	while (c >= '0' && c <= '9') {
		num = num * 10.0 + ( double ) (c - '0');
		dig++;
		got = true;
		c   = bgetc(bd);
	}
	if (!got) {
		if (c >= 0) bungetc(bd);
		if (!err()) errmsg("bgetd: no number");
		return 0.0;
	}
	if (c == 'e' || c == 'E') {
		uchar back [3];
		int   backn    = 0;
		int   expdig   = 0;
		int   expsign  = 1;
		int   expvalue = 0;
		back [backn++] = ( uchar ) c;
		c              = bgetc(bd);
		if (c == '-' || c == '+') {
			back [backn++] = ( uchar ) c;
			if (c == '-') expsign = -1;
			c = bgetc(bd);
		}
		while (c >= '0' && c <= '9') {
			expdig++;
			expvalue = expvalue * 10 + c - '0';
			c        = bgetc(bd);
		}
		if (expdig > 0) {
			if (expsign < 0) {
				dig  = -dig;
				eneg = 1;
			}
			exp = expvalue;
		}
		else {
			if (c >= 0) back [backn++] = ( uchar ) c;
			bio_pushback(b, back, backn);
			if (b) b->lastn = 0;
			c = -1;
		}
	}
	if (c >= 0) bungetc(bd);

	exp -= dig;
	if (exp < 0) {
		exp  = -exp;
		eneg = !eneg;
	}
	double scale = bio_pow10(exp);
	if (eneg) num /= scale;
	else num *= scale;
	return neg ? -num : num;
}

void bflush(int bd) {
	bio *b = bio_get_err(bd, "bflush: bad buffer");
	if (!b) return;
	if (b->writing && !bio_flush_write(b) && !err()) errmsg("bflush: write failed");
	b->writing = false;
}

void bclose(int bd) {
	bio *b = bio_get(bd);
	if (!b) return;
	bflush(bd);
	if (b->fd >= 0 && b->owned) dclose(b->fd);
	b->fd    = -1;
	b->owned = b->writing = false;
	b->pos = b->len = 0;
	bio_forget_get(b);
}

void rmbio(int bd) {
	bio *b = bio_get(bd);
	if (!b) return;
	bclose(bd);
	b->buf   = null;
	b->live  = false;
	b->arena = 0;
}
