#include "regex9.h"
#include "arena.h"
#include "err.h"
#include "text.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Plan 9-shaped regex compiler, rebuilt for Coetua.
 * This first landing keeps the VM spine direct: infix parser -> Reinst graph.
 * Counted @ opcodes are parsed as real instructions, not cloned subgraphs;
 * regexec will teach them their repetition semantics next.
 */

typedef struct Node       Node;
typedef struct Parser     Parser;
typedef struct Branchmark Branchmark;

typedef struct Node {
	Reinst *first;
	Reinst *last;
} Node;

typedef struct Branchmark {
	uint base;
	uint max;
} Branchmark;

typedef struct Parser {
	char       *p;
	int         arena;
	Reprog     *prog;
	Reinst     *inst;
	Reinst     *iend;
	Reclass    *class;
	Reclass    *cend;
	Node       *andstk;
	uvlong      nand;
	uvlong      cand;
	int        *opstk;
	uint       *capstk;
	Requant    *qstk;
	uvlong      nop;
	uvlong      cop;
	uvlong      ccap;
	uvlong      cq;
	Branchmark *brstk;
	uvlong      nbr;
	uvlong      cbr;
	uint        curcap;
	uint        maxcap;
	uint        nbra;
	uint        nquant;
	bool        lastand;
	bool        done;
	rune        yyrune;
	Reclass    *yyclass;
	Requant     yyquant;
} Parser;

static bool grow(void **p, uvlong *cap, uvlong need, uvlong sz) {
	if (*cap >= need) return true;
	uvlong ncap = *cap ? *cap * 2 : 16;
	while (ncap < need) {
		uvlong next = ncap * 2;
		if (next <= ncap) return errmsg("regex allocation failed"), false;
		ncap = next;
	}
	if (sz && ncap > ( uvlong ) (SIZE_MAX / sz)) return errmsg("regex allocation failed"), false;
	void *np = realloc(*p, ( size_t ) (ncap * sz));
	if (!np) return errmsg("regex allocation failed"), false;
	*p   = np;
	*cap = ncap;
	return true;
}

static Reinst *newinst(Parser *p, int type) {
	if (p->inst == p->iend) return errmsg("regex program too large"), null;
	Reinst *i = p->inst++;
	memset(i, 0, sizeof(*i));
	i->type = type;
	return i;
}

static bool pushand(Parser *p, Reinst *first, Reinst *last) {
	if (!grow(( void ** ) &p->andstk, &p->cand, p->nand + 1, sizeof(Node))) return false;
	p->andstk [p->nand++] = (Node) {first, last};
	return true;
}

static Node popand(Parser *p, int op) {
	if (p->nand == 0) {
		char msg [64];
		snprintf(msg, sizeof(msg), "missing operand for %c", op ? op : '?');
		errmsg(msg);
		Reinst *i = newinst(p, NOP);
		return (Node) {i, i};
	}
	return p->andstk [--p->nand];
}

static bool pushop(Parser *p, int op) {
	if (!grow(( void ** ) &p->opstk, &p->cop, p->nop + 1, sizeof(int))) return false;
	if (!grow(( void ** ) &p->capstk, &p->ccap, p->nop + 1, sizeof(uint))) return false;
	if (!grow(( void ** ) &p->qstk, &p->cq, p->nop + 1, sizeof(Requant))) return false;
	p->opstk [p->nop]  = op;
	p->capstk [p->nop] = p->curcap;
	p->qstk [p->nop]   = p->yyquant;
	if (op >= QEXACT && op <= CQOUTSIDE) p->qstk [p->nop].qid = p->nquant++;
	p->nop++;
	return true;
}

static int popop(Parser *p, uint *cap, Requant *q) {
	if (p->nop == 0) return errmsg("operator stack underflow"), START;
	p->nop--;
	*cap = p->capstk [p->nop];
	*q   = p->qstk [p->nop];
	return p->opstk [p->nop];
}

static bool pushbranch(Parser *p, uint base) {
	if (!grow(( void ** ) &p->brstk, &p->cbr, p->nbr + 1, sizeof(Branchmark))) return false;
	p->brstk [p->nbr++] = (Branchmark) {base, base};
	return true;
}

static void branch_seen(Parser *p) {
	if (p->nbr == 0) return;
	Branchmark *b = &p->brstk [p->nbr - 1];
	if (p->curcap > b->max) b->max = p->curcap;
	p->curcap = b->base;
}

static void finish_branch(Parser *p) {
	if (p->nbr == 0) return;
	Branchmark b = p->brstk [--p->nbr];
	if (p->curcap > b.max) b.max = p->curcap;
	p->curcap = b.max;
	if (p->maxcap < p->curcap) p->maxcap = p->curcap;
}

static bool evaluntil(Parser *p, int pri) {
	while (p->nop && (pri == RBRA || p->opstk [p->nop - 1] >= pri)) {
		uint    cap = 0;
		Requant q   = {0};
		int     op  = popop(p, &cap, &q);
		if (op >= QEXACT && op <= CQOUTSIDE) {
			Node    a = popand(p, '@');
			Reinst *i = newinst(p, op);
			if (!i) return false;
			i->q            = q;
			i->u1.right     = a.first;
			a.last->u2.next = i;
			if (!pushand(p, i, i)) return false;
			continue;
		}
		switch (op) {
		case START : return true;
		case LBRA  : {
			Node    expr = popand(p, '(');
			Reinst *r    = newinst(p, RBRA);
			Reinst *l    = newinst(p, LBRA);
			if (!r || !l) return false;
			r->u1.subid        = ( int ) cap;
			l->u1.subid        = ( int ) cap;
			expr.last->u2.next = r;
			l->u2.next         = expr.first;
			if (!pushand(p, l, r)) return false;
			return true;
		}
		case OR : {
			Node    right = popand(p, '|');
			Node    left  = popand(p, '|');
			Reinst *join  = newinst(p, NOP);
			Reinst *split = newinst(p, OR);
			if (!join || !split) return false;
			left.last->u2.next  = join;
			right.last->u2.next = join;
			split->u1.right     = left.first;
			split->u2.left      = right.first;
			if (!pushand(p, split, join)) return false;
			break;
		}
		case CAT : {
			Node b          = popand(p, 0);
			Node a          = popand(p, 0);
			a.last->u2.next = b.first;
			if (!pushand(p, a.first, b.last)) return false;
			break;
		}
		case STAR : {
			Node    a     = popand(p, '*');
			Reinst *split = newinst(p, OR);
			if (!split) return false;
			a.last->u2.next = split;
			split->u1.right = a.first;
			if (!pushand(p, split, split)) return false;
			break;
		}
		case PLUS : {
			Node    a     = popand(p, '+');
			Reinst *split = newinst(p, OR);
			if (!split) return false;
			a.last->u2.next = split;
			split->u1.right = a.first;
			if (!pushand(p, a.first, split)) return false;
			break;
		}
		case QUEST : {
			Node    a     = popand(p, '?');
			Reinst *split = newinst(p, OR);
			Reinst *join  = newinst(p, NOP);
			if (!split || !join) return false;
			split->u2.left  = join;
			split->u1.right = a.first;
			a.last->u2.next = join;
			if (!pushand(p, split, join)) return false;
			break;
		}
		default : return errmsg("unknown regex operator"), false;
		}
	}
	return true;
}

static bool operator(Parser *p, int op) {
	if (op == RBRA) {
		if (p->nbra == 0) return errmsg("unmatched right paren"), false;
		p->nbra--;
		if (!evaluntil(p, RBRA)) return false;
		finish_branch(p);
	}
	else if (op == LBRA) {
		if (p->lastand && !operator(p, CAT)) return false;
		uint id = ++p->curcap;
		if (p->maxcap < id) p->maxcap = id;
		p->nbra++;
		if (!pushop(p, LBRA)) return false;
		p->capstk [p->nop - 1] = id;
		if (!pushbranch(p, p->curcap)) return false;
	}
	else if (op == OR) {
		if (!evaluntil(p, OR)) return false;
		branch_seen(p);
		if (!pushop(p, OR)) return false;
	}
	else {
		if (!evaluntil(p, op)) return false;
		if (!pushop(p, op)) return false;
	}
	p->lastand = op == STAR || op == PLUS || op == QUEST || op == RBRA || (op >= QEXACT && op <= CQOUTSIDE);
	return true;
}

static bool operand(Parser *p, int type) {
	if (p->lastand && !operator(p, CAT)) return false;
	Reinst *i = newinst(p, type);
	if (!i) return false;
	if (type == RUNE) i->u1.r = p->yyrune;
	else if (type == CCLASS || type == NCCLASS) i->u1.cp = p->yyclass;
	if (!pushand(p, i, i)) return false;
	p->lastand = true;
	return true;
}

static int esc(char **sp, rune *rp) {
	char *s = *sp;
	if (*s == 0) return *rp = 0, 0;
	if (*s != '\\') {
		int n = chartorune(rp, s);
		if (n <= 0) return *rp = ( uchar ) *s, *sp = s + 1, 0;
		*sp = s + n;
		return 0;
	}
	s++;
	switch (*s) {
	case 'n' :
		*rp = '\n';
		s++;
		break;
	case 't' :
		*rp = '\t';
		s++;
		break;
	case 'r' :
		*rp = '\r';
		s++;
		break;
	case 0  : *rp = 0; break;
	default : *rp = ( uchar ) *s++; break;
	}
	*sp = s;
	return 1;
}

static bool addspan(Reclass *c, rune lo, rune hi) {
	uvlong n = ( uvlong ) (c->end - c->spans);
	if (n + 2 > c->cap) {
		uvlong cap = c->cap ? c->cap * 2 : 16;
		rune  *sp  = realloc(c->spans, ( size_t ) (cap * sizeof(rune)));
		if (!sp) return errmsg("regex class allocation failed"), false;
		c->spans = sp;
		c->end   = sp + n;
		c->cap   = cap;
	}
	*c->end++ = lo;
	*c->end++ = hi;
	return true;
}

static bool addshorthand(Reclass *c, int ch, bool neg) {
	Reclass tmp = {0};
#define SP(a, b)                                    \
	do {                                            \
		if (!addspan(&tmp, (a), (b))) return false; \
	}                                               \
	while (0)
	switch (ch) {
	case 'd' : SP('0', '9'); break;
	case 'a' :
		SP('A', 'Z');
		SP('a', 'z');
		break;
	case 'w' :
		SP('0', '9');
		SP('A', 'Z');
		SP('_', '_');
		SP('a', 'z');
		break;
	case 's' :
		SP('\t', '\n');
		SP('\r', '\r');
		SP(' ', ' ');
		break;
	case 'x' :
		SP('0', '9');
		SP('A', 'F');
		SP('a', 'f');
		break;
	case 'l' : SP('a', 'z'); break;
	case 'u' : SP('A', 'Z'); break;
	case 'c' :
		SP(0, 0x1f);
		SP(0x7f, 0x9f);
		break;
	case 'g' : SP('!', '~'); break;
	case 'p' :
		SP('!', '/');
		SP(':', '@');
		SP('[', '`');
		SP('{', '~');
		break;
	default : free(tmp.spans); return false;
	}
#undef SP
	if (!neg) {
		for (rune *r = tmp.spans; r < tmp.end; r += 2)
			if (!addspan(c, r [0], r [1])) {
				free(tmp.spans);
				return false;
			}
	}
	else {
		rune start = 0;
		for (rune *r = tmp.spans; r < tmp.end; r += 2) {
			if (start < r [0] && !addspan(c, start, r [0] - 1)) {
				free(tmp.spans);
				return false;
			}
			start = r [1] + 1;
		}
		if (start <= 0x10ffff && !addspan(c, start, 0x10ffff)) {
			free(tmp.spans);
			return false;
		}
	}
	free(tmp.spans);
	return true;
}

static Reclass *newclass(Parser *p) {
	if (p->class == p->cend) return errmsg("too many regex classes"), null;
	Reclass *c = p->class++;
	memset(c, 0, sizeof(*c));
	return c;
}

static int bldclass(Parser *p) {
	Reclass *c = newclass(p);
	if (!c) return END;
	p->yyclass = c;
	int type   = CCLASS;
	if (*p->p == '^') {
		type = NCCLASS;
		p->p++;
		if (!addspan(c, '\n', '\n')) return END;
	}
	while (*p->p && *p->p != ']') {
		rune lo, hi;
		if (*p->p == '%' && p->p [1]) {
			p->p++;
			int  ch  = *p->p++;
			bool neg = ch >= 'A' && ch <= 'Z';
			if (neg) ch += 'a' - 'A';
			if (!addshorthand(c, ch, neg)) return errmsg("bad regex shorthand"), END;
			continue;
		}
		esc(&p->p, &lo);
		hi = lo;
		if (*p->p == '-' && p->p [1] && p->p [1] != ']') {
			p->p++;
			esc(&p->p, &hi);
		}
		if (lo > hi) {
			rune t = lo;
			lo     = hi;
			hi     = t;
		}
		if (!addspan(c, lo, hi)) return END;
	}
	if (*p->p != ']') return errmsg("malformed regex class"), END;
	p->p++;
	return type;
}

static bool number(char **sp, uvlong *v) {
	char *s = *sp;
	if (*s < '0' || *s > '9') return false;
	uvlong n = 0;
	while (*s >= '0' && *s <= '9') n = n * 10 + ( uvlong ) (*s++ - '0');
	*sp = s;
	*v  = n;
	return true;
}

static int lexat(Parser *p) {
	char *s = p->p;
	memset(&p->yyquant, 0, sizeof(p->yyquant));
	if (*s != '@') return 0;
	s++;
	bool cap = false;
	if (*s == '(') {
		cap = true;
		s++;
	}
	uvlong n = 0, m = 0;
	int    type = 0;
	if (cap && *s == ')') {
		s++;
		if (*s != '\'') return errmsg("bad captured @ quantifier"), END;
		s++;
		type = CQANY;
	}
	else {
		if (!number(&s, &n)) return errmsg("bad @ quantifier"), END;
		if (*s == '~' || *s == '^') {
			int sep = *s++;
			if (!number(&s, &m)) return errmsg("bad @ range"), END;
			if (cap) {
				if (*s++ != ')') return errmsg("bad captured @ range"), END;
			}
			if (sep == '~') {
				if (*s++ != '\'') return errmsg("bad @ range terminator"), END;
				type = cap ? CQRANGE : QRANGE;
			}
			else {
				if (*s++ != ';') return errmsg("bad @ outside terminator"), END;
				type = cap ? CQOUTSIDE : QOUTSIDE;
			}
		}
		else {
			if (cap) {
				if (*s++ != ')') return errmsg("bad captured @ quantifier"), END;
			}
			if (*s == '\'') {
				s++;
				type = cap ? 0 : QEXACT;
			}
			else if (*s == '+') {
				s++;
				type = cap ? CQATLEAST : QATLEAST;
			}
			else if (*s == '-') {
				s++;
				type = cap ? CQATMOST : QATMOST;
			}
			else return errmsg("bad @ quantifier terminator"), END;
			m = n;
		}
	}
	if (cap) {
		if (type == 0) return errmsg("captured exact @ is redundant"), END;
		p->yyquant.subid = ++p->curcap;
		if (p->maxcap < p->curcap) p->maxcap = p->curcap;
	}
	p->yyquant.n = n;
	p->yyquant.m = m;
	p->p         = s;
	return type;
}

static int lex(Parser *p) {
	if (p->done) return END;
	if (*p->p == 0) return p->done = true, END;
	if (*p->p == '@') return lexat(p);
	if (*p->p == '%') {
		p->p++;
		int  ch  = *p->p++;
		bool neg = ch >= 'A' && ch <= 'Z';
		if (neg) ch += 'a' - 'A';
		Reclass *c = newclass(p);
		if (!c) return END;
		p->yyclass = c;
		if (!addshorthand(c, ch, neg)) return errmsg("bad regex shorthand"), END;
		return CCLASS;
	}
	if (esc(&p->p, &p->yyrune)) return p->yyrune ? RUNE : END;
	switch (p->yyrune) {
	case '*' : return STAR;
	case '+' : return PLUS;
	case '?' : return QUEST;
	case '|' : return OR;
	case '.' : return ANY;
	case '^' : return BOL;
	case '$' : return EOL;
	case '(' : return LBRA;
	case ')' : return RBRA;
	case '[' : return bldclass(p);
	default  : return RUNE;
	}
}

static void patch_nops(Reprog *prog, Reinst *end) {
	for (Reinst *i = prog->startinst; i < end; i++) {
		Reinst *n = i->u2.next;
		while (n && n->type == NOP) n = n->u2.next;
		if (n) i->u2.next = n;
	}
}

Reprog *regcomp9(int arena, char *pattern) {
	if (!pattern) return errmsg("null regex pattern"), null;
	errmsg(null);
	uvlong plen = strlen(pattern);
	uvlong maxinst, maxclass, instbytes, classbytes, tailbytes, bytes;
	if (!mulok64(plen, 8, &maxinst)
	    || !addok64(maxinst, 16, &maxinst)
	    || !addok64(plen, 1, &maxclass)
	    || !mulok64(maxinst, sizeof(Reinst), &instbytes)
	    || !mulok64(maxclass, sizeof(Reclass), &classbytes)
	    || !addok64(instbytes, classbytes, &tailbytes)
	    || !addok64(sizeof(Reprog), tailbytes, &bytes))
	{
		errmsg("regex program too large");
		return null;
	}
	Reprog *prog = aden(arena, bytes);
	if (!prog) {
		if (!err()) errmsg("regex allocation failed");
		return null;
	}
	memset(prog, 0, ( size_t ) bytes);
	Parser p = {0};
	p.p      = pattern;
	p.arena  = arena;
	p.prog   = prog;
	p.inst   = ( Reinst * ) (prog + 1);
	p.iend   = p.inst + maxinst;
	p.class  = ( Reclass * ) (p.iend);
	p.cend   = p.class + maxclass;
	if (!pushop(&p, START - 1)) goto fail;
	for (;;) {
		int t = lex(&p);
		if (err()) goto fail;
		if (t == END) break;
		if ((t & 0300) == OPERATOR) {
			if (!operator(&p, t)) goto fail;
		}
		else {
			if (!operand(&p, t)) goto fail;
		}
	}
	if (!evaluntil(&p, START)) goto fail;
	if (p.nbra) {
		errmsg("unmatched left paren");
		goto fail;
	}
	if (!operand(&p, END)) goto fail;
	if (!evaluntil(&p, START)) goto fail;
	if (p.nand != 1) {
		errmsg("malformed regex expression");
		goto fail;
	}
	prog->startinst = p.andstk [0].first;
	prog->ninst     = ( uvlong ) (p.inst - ( Reinst * ) (prog + 1));
	prog->class     = ( Reclass * ) p.iend;
	prog->nclass    = ( uvlong ) (p.class - prog->class);
	prog->nsub      = p.maxcap + 1;
	prog->nquant    = p.nquant;
	patch_nops(prog, p.inst);
	free(p.andstk);
	free(p.opstk);
	free(p.capstk);
	free(p.qstk);
	free(p.brstk);
	return prog;
fail:
	free(p.andstk);
	free(p.opstk);
	free(p.capstk);
	free(p.qstk);
	free(p.brstk);
	return null;
}
