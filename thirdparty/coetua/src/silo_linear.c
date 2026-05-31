#include "silo_priv.h"
#include "err.h"
#include <string.h>

static silo_t *linear_get(int desc, int type) {
	silo_t *s = silo_get(desc);
	if (!s || s->type != type) return null;
	return s;
}

static silo_t *linear_need(int desc, int type, char *who) {
	silo_t *s = linear_get(desc, type);
	if (!s) errmsg(who);
	return s;
}

static silo_t *seq_get(int seq, char *who) {
	silo_t *s = linear_get(seq, silo_seq);
	if (!s) errmsg(who);
	return s;
}

static bool norm_index(silo_t *s, vlong *pos, bool allow_end) {
	if (*pos < 0) *pos += ( vlong ) s->len;
	if (*pos < 0) return false;
	if (allow_end) return ( uvlong ) *pos <= s->len;
	return ( uvlong ) *pos < s->len;
}

static void rmlinear(silo_t *s) {
	s->data = null;
	s->cap  = 0;
	s->len  = 0;
	s->head = 0;
	silo_touch(s);
	s->type = 0;
}

static bool append_linear(silo_t *s, uvlong data) {
	uvlong needed;
	if (!addok64(s->len, 1, &needed)) {
		errmsg("silo: size overflow");
		return false;
	}
	if (!silo_grow(s, needed)) return false;
	s->data [s->len++] = data;
	silo_touch(s);
	return true;
}

static void seq_shift_right(silo_t *s, uvlong pos) {
	memmove(&s->data [pos + 1], &s->data [pos], (s->len - pos) * sizeof(uvlong));
}

static void seq_shift_left(silo_t *s, uvlong pos) {
	memmove(&s->data [pos], &s->data [pos + 1], (s->len - pos - 1) * sizeof(uvlong));
}

/* ── Stack ───────────────────────────────────────────── */

int  mkstack(int arena) { return silo_new(silo_stack, arena); }

void push(int stack, uvlong data) {
	silo_t *s = linear_need(stack, silo_stack, "push: bad stack");
	if (!s) return;
	append_linear(s, data);
}

uvlong pop(int stack) {
	silo_t *s = linear_need(stack, silo_stack, "pop: bad stack");
	if (!s) return 0;
	if (s->len == 0) {
		errmsg("pop: empty stack");
		return 0;
	}
	uvlong v = s->data [--s->len];
	silo_touch(s);
	return v;
}

uvlong peep(int stack) {
	silo_t *s = linear_need(stack, silo_stack, "peep: bad stack");
	if (!s) return 0;
	if (s->len == 0) {
		errmsg("peep: empty stack");
		return 0;
	}
	return s->data [s->len - 1];
}

void rmstack(int stack) {
	silo_t *s = linear_get(stack, silo_stack);
	if (s) rmlinear(s);
}

/* ── Queue (circular buffer) ─────────────────────────── */

int  mkqueue(int arena) { return silo_new(silo_queue, arena); }

void enqueue(int queue, uvlong data) {
	silo_t *s = linear_need(queue, silo_queue, "enqueue: bad queue");
	if (!s) return;
	uvlong needed;
	if (!addok64(s->len, 1, &needed)) {
		errmsg("enqueue: size overflow");
		return;
	}
	if (!silo_grow(s, needed)) return;
	uvlong idx    = (s->head + s->len) % s->cap;
	s->data [idx] = data;
	s->len++;
	silo_touch(s);
}

uvlong dequeue(int queue) {
	silo_t *s = linear_need(queue, silo_queue, "dequeue: bad queue");
	if (!s) return 0;
	if (s->len == 0) {
		errmsg("dequeue: empty queue");
		return 0;
	}
	uvlong val = s->data [s->head];
	s->head    = (s->head + 1) % s->cap;
	s->len--;
	silo_touch(s);
	return val;
}

uvlong peekq(int queue) {
	silo_t *s = linear_need(queue, silo_queue, "peekq: bad queue");
	if (!s) return 0;
	if (s->len == 0) {
		errmsg("peekq: empty queue");
		return 0;
	}
	return s->data [s->head];
}

void rmqueue(int queue) {
	silo_t *s = linear_get(queue, silo_queue);
	if (s) rmlinear(s);
}

/* ── Sequence ────────────────────────────────────────── */

int  mkseq(int arena) { return silo_new(silo_seq, arena); }

void atch(int seq, uvlong data) {
	silo_t *s = seq_get(seq, "atch: bad sequence");
	if (!s) return;
	append_linear(s, data);
}

void place(int seq, vlong pos, uvlong data) {
	silo_t *s = seq_get(seq, "place: bad sequence");
	if (!s) return;
	if (!norm_index(s, &pos, true)) {
		errmsg("place: index out of bounds");
		return;
	}
	uvlong needed;
	if (!addok64(s->len, 1, &needed)) {
		errmsg("place: size overflow");
		return;
	}
	if (!silo_grow(s, needed)) return;
	seq_shift_right(s, ( uvlong ) pos);
	s->data [pos] = data;
	s->len++;
	silo_touch(s);
}

uvlong drop(int seq, vlong pos) {
	silo_t *s = seq_get(seq, "drop: bad sequence");
	if (!s) return 0;
	if (!norm_index(s, &pos, false)) {
		errmsg("drop: index out of bounds");
		return 0;
	}
	uvlong val = s->data [pos];
	if (( uvlong ) pos < s->len - 1) seq_shift_left(s, ( uvlong ) pos);
	s->len--;
	silo_touch(s);
	return val;
}

void swap(int seq, vlong pa, vlong pb) {
	silo_t *s = seq_get(seq, "swap: bad sequence");
	if (!s) return;
	if (!norm_index(s, &pa, false) || !norm_index(s, &pb, false)) {
		errmsg("swap: index out of bounds");
		return;
	}
	uvlong tmp   = s->data [pa];
	s->data [pa] = s->data [pb];
	s->data [pb] = tmp;
	silo_touch(s);
}

uvlong pick(int seq, vlong pos) {
	silo_t *s = seq_get(seq, "pick: bad sequence");
	if (!s) return 0;
	if (!norm_index(s, &pos, false)) {
		errmsg("pick: index out of bounds");
		return 0;
	}
	return s->data [pos];
}

uvlong slen(int seq) {
	silo_t *s = seq_get(seq, "slen: bad sequence");
	if (!s) return 0;
	return s->len;
}

uvlong swath(int seq, vlong start, vlong end, uvlong **data) {
	if (data) *data = null;
	silo_t *s = seq_get(seq, "swath: bad sequence");
	if (!s) return 0;
	if (!data) {
		errmsg("swath: bad output");
		return 0;
	}
	if (!norm_index(s, &start, false) || !norm_index(s, &end, false)) {
		errmsg("swath: index out of bounds");
		return 0;
	}
	if (end < start) {
		errmsg("swath: reversed range");
		return 0;
	}
	*data = s->data + start;
	return ( uvlong ) (end - start + 1);
}

void rmseq(int seq) {
	silo_t *s = linear_get(seq, silo_seq);
	if (s) rmlinear(s);
}
