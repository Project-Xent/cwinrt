#pragma once
#include "../arena.h"
#include "../atom.h"
#include "../err.h"

#define BT_TAG_MASK (( uvlong ) 7)
#define BT_RED      (( uvlong ) 1)
#define BT_DEAD     (( uvlong ) 2)

typedef struct knod {
	uvlong par;
	uvlong ref;
	uvlong kids [2];
} knod;

static knod inline *bt_raw(uvlong k) { return ( knod * ) (k & ~BT_TAG_MASK); }

static uvlong inline bt_id(knod *k) { return ( uvlong ) k; }

static uvlong inline bt_link_raw(uvlong link) { return link & ~BT_TAG_MASK; }

static uvlong inline bt_link_tag(uvlong link) { return link & BT_TAG_MASK; }

static uvlong inline bt_par(uvlong k) { return bt_link_raw(bt_raw(k)->par); }

static uvlong inline bt_kid(uvlong k, int side) { return bt_link_raw(bt_raw(k)->kids [side]); }

static uvlong inline bt_par_tag(uvlong k) { return bt_link_tag(bt_raw(k)->par); }

static void inline bt_set_par(uvlong k, uvlong par) {
	if (!k) return;
	bt_raw(k)->par = bt_link_raw(par) | bt_link_tag(bt_raw(k)->par);
}

static void inline bt_set_par_tag(uvlong k, uvlong tag) {
	if (!k) return;
	bt_raw(k)->par = bt_link_raw(bt_raw(k)->par) | (tag & BT_TAG_MASK);
}

static void inline bt_add_par_tag(uvlong k, uvlong tag) {
	if (!k) return;
	bt_raw(k)->par |= tag & BT_TAG_MASK;
}

static void inline bt_clear_par_tag(uvlong k, uvlong tag) {
	if (!k) return;
	bt_raw(k)->par &= ~(tag & BT_TAG_MASK);
}

static void inline bt_set_kid(uvlong k, int side, uvlong kid) {
	if (!k) return;
	bt_raw(k)->kids [side] = bt_link_raw(kid) | bt_link_tag(bt_raw(k)->kids [side]);
}

static void inline bt_set_kid_tag(uvlong k, int side, uvlong tag) {
	if (!k) return;
	bt_raw(k)->kids [side] = bt_link_raw(bt_raw(k)->kids [side]) | (tag & BT_TAG_MASK);
}

static bool inline bt_redp(uvlong k) { return k && (bt_par_tag(k) & BT_RED); }

static bool inline bt_deadp(uvlong k) { return k && (bt_par_tag(k) & BT_DEAD); }

static void inline bt_mark_red(uvlong k, bool red) {
	if (!k) return;
	if (red) bt_add_par_tag(k, BT_RED);
	else bt_clear_par_tag(k, BT_RED);
}

static void inline bt_mark_dead(uvlong k) { bt_add_par_tag(k, BT_DEAD); }

static knod inline *bt_new_knod(int arena, uvlong ref, char *who) {
	knod *k = ( knod * ) acarrel(arena, ( int ) sizeof(uvlong), sizeof(knod));
	if (!k) {
		if (!err()) errmsg(who);
		return null;
	}
	k->par      = 0;
	k->ref      = ref;
	k->kids [0] = 0;
	k->kids [1] = 0;
	return k;
}
