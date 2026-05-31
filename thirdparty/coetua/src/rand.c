#include "rand.h"
#include <string.h>

static _Thread_local uvlong shared_seed = 0x1234567890abcdefull;

/* ── ChaCha8 state ───────────────────────────────────── */
static _Thread_local uint   chacha_state [16]
  = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static _Thread_local uvlong chacha_buf [8];
static _Thread_local int    chacha_idx = 8;

void                        seedfeed(void *data, uvlong len) {
	if (len >= sizeof(uvlong)) memcpy(&shared_seed, data, sizeof(uvlong));
	else if (len > 0) memcpy(&shared_seed, data, len);

	for (int i = 0; i < 8; i++)
		chacha_state [4 + i] = ( uint ) (shared_seed >> (i % 2 == 0 ? 0 : 32)) ^ ( uint ) (i * 0x9e3779b9u);
	chacha_idx = 8;
}

/* ── ChaCha8 core ────────────────────────────────────── */
static void chacha8_block(void) {
	uint x [16];
	memcpy(x, chacha_state, sizeof(x));

#define QR(a, b, c, d)          \
	x [a] += x [b];             \
	x [d] ^= x [a];             \
	x [d]  = rotl32(x [d], 16); \
	x [c] += x [d];             \
	x [b] ^= x [c];             \
	x [b]  = rotl32(x [b], 12); \
	x [a] += x [b];             \
	x [d] ^= x [a];             \
	x [d]  = rotl32(x [d], 8);  \
	x [c] += x [d];             \
	x [b] ^= x [c];             \
	x [b]  = rotl32(x [b], 7)

	for (int i = 0; i < 4; i++) {
		QR(0, 4, 8, 12);
		QR(1, 5, 9, 13);
		QR(2, 6, 10, 14);
		QR(3, 7, 11, 15);
		QR(0, 5, 10, 15);
		QR(1, 6, 11, 12);
		QR(2, 7, 8, 13);
		QR(3, 4, 9, 14);
	}
#undef QR

	for (int i = 0; i < 16; i++) (( uint * ) chacha_buf) [i] = x [i] + chacha_state [i];
	chacha_state [12]++;
}

uvlong rand64(void) {
	if (chacha_idx >= 8) {
		chacha8_block();
		chacha_idx = 0;
	}
	return chacha_buf [chacha_idx++];
}

/* ── Fast PRNG (wyrand) ──────────────────────────────── */
uvlong qrand64(void) {
	shared_seed += 0xa0761d6478bd642full;
	uvlong see   = shared_seed ^ 0xe7037ed1a0b428dbull;
	u128   p     = wmul64(shared_seed, see);
	return p.lo ^ p.hi;
}
