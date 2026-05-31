#include "coetua.h"
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#endif

static int failures = 0;
#define CHECK(cond, msg)                    \
	do {                                    \
		if (!(cond)) {                      \
			printf("FAIL: %s\n", msg);      \
			failures++;                     \
		}                                   \
		else { printf("  ok: %s\n", msg); } \
	}                                       \
	while (0)

static char *path    = "test_bio_temp.txt";
static char *delpath = "test_bio_delete.txt";
static char *bmsg    = "buffered hello";

static void  heading(char *name) { printf("\n=== bio: %s ===\n", name); }

static void  check_expected_error(bool ok, char *label) {
	CHECK(ok && err(), label);
	errmsg(null);
}

static void cleanup_files(void) {
	remove(path);
	remove(delpath);
}

static int rewrite_file(char *p, char *s) {
	int fd = dcreate(p, (omode) {.r = 1, .w = 1, .t = 1}, (perm) {.bits = 0644});
	if (fd < 0) return -1;
	if (s) dwrite(fd, s, strlen(s));
	dclose(fd);
	return 0;
}

static vlong read_file_text(char *p, char *buf, uvlong cap) {
	int fd = dopen(p, (omode) {.r = 1});
	if (fd < 0) return -1;
	memset(buf, 0, cap);
	vlong n = dread(fd, buf, cap);
	dclose(fd);
	return n;
}

static void check_path_text(char *want, char *label) {
	char  buf [128];
	vlong n = read_file_text(path, buf, sizeof(buf));
	CHECK(n == ( vlong ) strlen(want) && strcmp(buf, want) == 0, label);
}

static void buffered_delete_on_close(void) {
#if defined(_WIN32)
	heading("delete-on-close");
	remove(delpath);
	int bd = bopen(0, delpath, (omode) {.r = 1, .w = 1, .t = 1, .d = 1});
	CHECK(bd >= 0, "bopen delete-on-close");
	if (bd >= 0) {
		bwrite(bd, "gone", 4);
		bclose(bd);
		rmbio(bd);
	}
	int fd = dopen(delpath, (omode) {.r = 1});
	check_expected_error(fd < 0, "delete-on-close removes bopen-owned file");
#endif
}

static void buffered_first_slice(void) {
	char buf [128];

	heading("buffered first slice");
	int bd = bopen(0, null, (omode) {.r = 1});
	check_expected_error(bd < 0, "bopen rejects null path");

	bd = bopen(0, path, (omode) {.r = 1, .w = 1, .t = 1});
	CHECK(bd >= 0 && bfildes(bd) >= 0, "bopen + bfildes");
	vlong n = bwrite(bd, bmsg, strlen(bmsg));
	CHECK(n == ( vlong ) strlen(bmsg), "bwrite returns byte count");
	bflush(bd);
	CHECK(!err(), "bflush succeeds without error");
	bclose(bd);
	CHECK(bfildes(bd) < 0, "bclose detaches fd");
	rmbio(bd);

	bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen for read");
	memset(buf, 0, sizeof(buf));
	n = bread(bd, buf, 4);
	CHECK(n == 4 && memcmp(buf, "buff", 4) == 0, "bread partial from buffer");
	n = bread(bd, buf + 4, sizeof(buf) - 4);
	CHECK(n == ( vlong ) strlen(bmsg) - 4 && strcmp(buf, bmsg) == 0, "bread drains remaining data");
	rmbio(bd);

	bd     = mkbio(0);
	int fd = dopen(path, (omode) {.r = 1});
	CHECK(bd >= 0, "mkbio descriptor");
	binit(bd, fd, (omode) {.r = 1});
	memset(buf, 0, sizeof(buf));
	n = bread(bd, buf, sizeof(buf));
	CHECK(n == ( vlong ) strlen(bmsg) && strcmp(buf, bmsg) == 0, "binit binds existing fd");
	bclose(bd);
	CHECK(dseek(fd, 0, 0) >= 0 && !err(), "bclose disassociates binit fd without closing it");
	dclose(fd);
	rmbio(bd);

	bd      = mkbio(0);
	fd      = dopen(path, (omode) {.r = 1});
	int fd2 = dopen(path, (omode) {.r = 1});
	CHECK(bd >= 0, "mkbio for rebind external fd");
	binit(bd, fd, (omode) {.r = 1});
	binit(bd, fd2, (omode) {.r = 1});
	CHECK(dseek(fd, 0, 0) >= 0 && !err(), "binit rebind disassociates old external fd");
	dclose(fd);
	bclose(bd);
	dclose(fd2);
	rmbio(bd);

	bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen owns opened fd");
	int owned_fd = bfildes(bd);
	bclose(bd);
	n = dread(owned_fd, buf, 1);
	check_expected_error(n < 0, "bclose closes bopen-owned fd");
	rmbio(bd);

	bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen for rebind closes owned fd");
	owned_fd = bfildes(bd);
	fd       = dopen(path, (omode) {.r = 1});
	binit(bd, fd, (omode) {.r = 1});
	n = dread(owned_fd, buf, 1);
	check_expected_error(n < 0, "binit rebind closes old bopen-owned fd");
	CHECK(dseek(fd, 0, 0) >= 0 && !err(), "binit rebind keeps new external fd open");
	bclose(bd);
	dclose(fd);
	rmbio(bd);

	bd = bopen(0, path, (omode) {.r = 1, .w = 1, .t = 1});
	CHECK(bd >= 0, "bopen pending write before rebind");
	bwrite(bd, "rebind flush", 12);
	fd = dopen(path, (omode) {.r = 1});
	binit(bd, fd, (omode) {.r = 1});
	memset(buf, 0, sizeof(buf));
	n = dread(fd, buf, sizeof(buf));
	CHECK(n == 12 && strcmp(buf, "rebind flush") == 0, "binit flushes pending owned output before rebind");
	bclose(bd);
	dclose(fd);
	rmbio(bd);

	bool reuse_ok = true;
	for (int i = 0; i < 300; i++) {
		bd = mkbio(0);
		if (bd < 0) {
			reuse_ok = false;
			break;
		}
		rmbio(bd);
	}
	CHECK(reuse_ok, "bio descriptors reuse under repeated create/destroy");
}

static void buffered_edge_cases(void) {
	char buf [128];

	heading("buffered edge cases");
	int bd = bopen(0, path, (omode) {.r = 1, .w = 1, .t = 1});
	CHECK(bd >= 0, "bopen for close-time flush");
	bwrite(bd, "close flush", 11);
	bclose(bd);
	rmbio(bd);
	int fd = dopen(path, (omode) {.r = 1});
	memset(buf, 0, sizeof(buf));
	vlong n = dread(fd, buf, sizeof(buf));
	CHECK(n == 11 && strcmp(buf, "close flush") == 0, "bclose flushes pending output");
	dclose(fd);

	rewrite_file(path, "abcdef");
	bd = bopen(0, path, (omode) {.r = 1, .w = 1});
	CHECK(bd >= 0, "bopen read/write for read-to-write switch");
	memset(buf, 0, sizeof(buf));
	n = bread(bd, buf, 2);
	CHECK(n == 2 && memcmp(buf, "ab", 2) == 0, "bread leaves unread bytes buffered");
	n = bwrite(bd, "XY", 2);
	CHECK(n == 2, "bwrite after partial read returns byte count");
	bclose(bd);
	rmbio(bd);
	n = read_file_text(path, buf, sizeof(buf));
	CHECK(n == 6 && memcmp(buf, "abXYef", 6) == 0, "bwrite after partial read writes at logical position");

	rewrite_file(path, "abcdef");
	bd = bopen(0, path, (omode) {.r = 1, .w = 1});
	CHECK(bd >= 0, "bopen read/write for pushed read-to-write switch");
	CHECK(bgetc(bd) == 'a' && bgetc(bd) == 'b', "bgetc before pushed write switch");
	bungetc(bd);
	n = bwrite(bd, "XY", 2);
	CHECK(n == 2, "bwrite after pushback returns byte count");
	bclose(bd);
	rmbio(bd);
	n = read_file_text(path, buf, sizeof(buf));
	CHECK(n == 6 && memcmp(buf, "aXYdef", 6) == 0, "bwrite after pushback writes at logical position");

	rewrite_file(path, "abcdef");
	bd = bopen(0, path, (omode) {.r = 1, .w = 1});
	CHECK(bd >= 0, "bopen read/write for write-to-read switch");
	n = bwrite(bd, "XY", 2);
	CHECK(n == 2, "bwrite before read returns byte count");
	memset(buf, 0, sizeof(buf));
	n = bread(bd, buf, 4);
	CHECK(n == 4 && memcmp(buf, "cdef", 4) == 0, "bread after pending write flushes and reads at logical position");
	bclose(bd);
	rmbio(bd);
	n = read_file_text(path, buf, sizeof(buf));
	CHECK(n == 6 && memcmp(buf, "XYcdef", 6) == 0, "write-to-read switch persists pending output");

	bd = bopen(0, path, (omode) {.r = 1, .w = 1, .t = 1});
	CHECK(bd >= 0, "bopen for large buffered write");
	char big [COETUA_BIO_BUFSZ + 37];
	for (uvlong i = 0; i < sizeof(big); i++) big [i] = ( char ) ('A' + (i % 26));
	n = bwrite(bd, big, sizeof(big));
	CHECK(n == ( vlong ) sizeof(big), "bwrite spans internal buffer");
	bclose(bd);
	rmbio(bd);
	fd = dopen(path, (omode) {.r = 1});
	char big_read [sizeof(big)];
	memset(big_read, 0, sizeof(big_read));
	n = dreadn(fd, big_read, sizeof(big_read));
	CHECK(n == ( vlong ) sizeof(big_read) && memcmp(big, big_read, sizeof(big)) == 0,
	      "large buffered write round-trips");
	dclose(fd);

	bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen read-only for write rejection");
	n = bwrite(bd, "x", 1);
	check_expected_error(n < 0, "bwrite rejects read-only bio immediately");
	rmbio(bd);

	bd = bopen(0, path, (omode) {.w = 1, .t = 1});
	CHECK(bd >= 0, "bopen write-only for read rejection");
	n = bread(bd, buf, 1);
	check_expected_error(n < 0, "bread rejects write-only bio immediately");
	rmbio(bd);
}

static void buffered_descriptor_growth(void) {
	heading("buffer descriptor growth");
	int  bds [COETUA_BIO_TABLE_SEED + 20];
	bool ok = true;
	for (uvlong i = 0; i < arrlen(bds); i++) {
		bds [i] = mkbio(0);
		if (bds [i] < 0) {
			ok = false;
			break;
		}
	}
	for (uvlong i = 0; i < arrlen(bds); i++)
		if (bds [i] >= 0) rmbio(bds [i]);
	CHECK(ok, "bio descriptors grow past seed size");
}

static void buffered_small_edges(void) {
	char buf [128];

	heading("buffered small edges");
	int bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen read-only for harmless flush");
	bflush(bd);
	CHECK(!err(), "bflush on read-only buffer is harmless");
	vlong n = bread(bd, buf, 0);
	CHECK(n == 0 && !err(), "bread zero length returns zero");
	n = bread(bd, null, 0);
	CHECK(n == 0 && !err(), "bread null zero length returns zero");
	n = bread(bd, null, 1);
	check_expected_error(n < 0, "bread rejects null nonzero buffer");
	bclose(bd);
	bclose(bd);
	CHECK(bfildes(bd) < 0 && !err(), "bclose is idempotent");
	rmbio(bd);
	rmbio(bd);
	CHECK(!err(), "rmbio is idempotent after close");

	rewrite_file(path, "base");
	bd = bopen(0, path, (omode) {.r = 1, .w = 1, .a = 1});
	CHECK(bd >= 0, "bopen append mode for buffered write");
	n = bwrite(bd, "+tail", 5);
	CHECK(n == 5, "bwrite append returns byte count");
	bclose(bd);
	rmbio(bd);
	check_path_text("base+tail", "buffered append writes at end");

	bd = bopen(0, path, (omode) {.w = 1, .t = 1});
	CHECK(bd >= 0, "bopen write-only for zero write");
	n = bwrite(bd, buf, 0);
	CHECK(n == 0 && !err(), "bwrite zero length returns zero");
	n = bwrite(bd, null, 0);
	CHECK(n == 0 && !err(), "bwrite null zero length returns zero");
	n = bwrite(bd, null, 1);
	check_expected_error(n < 0, "bwrite rejects null nonzero buffer");
	rmbio(bd);
}

static void buffered_seek_offset(void) {
	char buf [128];

	heading("seek offset buffered");
	rewrite_file(path, "abcdef");
	int bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen for bseek read");
	CHECK(boffset(bd) == 0, "boffset starts at zero");
	CHECK(bbuffered(bd) == 0, "bbuffered starts empty");
	CHECK(bgetc(bd) == 'a', "bgetc before boffset");
	CHECK(boffset(bd) == 1, "boffset advances after bgetc");
	CHECK(bbuffered(bd) == 5, "bbuffered counts unread read buffer");
	bungetc(bd);
	CHECK(boffset(bd) == 0, "boffset includes pushback");
	CHECK(bbuffered(bd) == 6, "bbuffered includes pushback");
	CHECK(bseek(bd, 3, 0) == 3, "bseek absolute discards read buffer");
	CHECK(boffset(bd) == 3 && bbuffered(bd) == 0, "bseek resets buffered state");
	CHECK(bgetc(bd) == 'd', "bseek absolute positions next byte");
	CHECK(bseek(bd, -1, 1) == 3, "bseek relative uses logical offset");
	CHECK(bgetc(bd) == 'd', "bseek relative rereads expected byte");
	CHECK(bseek(bd, -1, 2) == 5, "bseek from end");
	CHECK(bgetc(bd) == 'f', "bseek from end positions byte");
	check_expected_error(bseek(bd, 0, 99) < 0, "bseek rejects bad whence");
	rmbio(bd);

	bd = bopen(0, path, (omode) {.r = 1, .w = 1, .t = 1});
	CHECK(bd >= 0, "bopen for bseek write");
	CHECK(bwrite(bd, "abc", 3) == 3, "bwrite before boffset");
	CHECK(boffset(bd) == 3, "boffset counts pending output");
	CHECK(bbuffered(bd) == 3, "bbuffered counts pending output");
	CHECK(bseek(bd, 1, 0) == 1, "bseek flushes pending output");
	CHECK(bwrite(bd, "Z", 1) == 1, "bwrite after bseek");
	bclose(bd);
	rmbio(bd);
	memset(buf, 0, sizeof(buf));
	read_file_text(path, buf, sizeof(buf));
	CHECK(memcmp(buf, "aZc", 3) == 0, "bseek write persists flushed output");
}

static int test_bvprint(int bd, char *fm, ...) {
	va_list args;
	va_start(args, fm);
	int n = bvprint(bd, fm, args);
	va_end(args);
	return n;
}

static void buffered_print_helpers(void) {
	char buf [128];

	heading("print helpers");
	int bd = bopen(0, path, (omode) {.w = 1, .t = 1});
	CHECK(bd >= 0, "bopen for bprint");
	int n = bprint(bd, "hello %s %d %x", "bio", 42, 255);
	CHECK(n == ( int ) strlen("hello bio 42 ff"), "bprint returns byte count");
	CHECK(bbuffered(bd) == ( uvlong ) n, "bprint leaves output buffered");
	CHECK(test_bvprint(bd, " %C", 0x03bb) == 3, "bvprint writes UTF formatted output");
	bclose(bd);
	rmbio(bd);
	memset(buf, 0, sizeof(buf));
	read_file_text(path, buf, sizeof(buf));
	CHECK(strcmp(buf, "hello bio 42 ff \xce\xbb") == 0, "bprint output persists");

	bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen read-only for bprint rejection");
	CHECK(bprint(bd, "nope") < 0, "bprint rejects read-only bio");
	check_expected_error(true, "bprint read-only sets error");
	rmbio(bd);
}

static bool near_double(double a, double b) {
	double d = a - b;
	return d < 0 ? d > -0.0000001 : d < 0.0000001;
}

static void buffered_getd_helper(void) {
	heading("getd helper");
	rewrite_file(path, " \t-12.5e2X 3.25 -4E-1 .75 42end");
	int bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen for bgetd");
	CHECK(near_double(bgetd(bd), -1250.0), "bgetd parses sign decimal exponent");
	CHECK(!err(), "bgetd valid number leaves no error");
	CHECK(bgetc(bd) == 'X', "bgetd backs up terminating byte");
	CHECK(near_double(bgetd(bd), 3.25), "bgetd parses decimal after separator");
	CHECK(near_double(bgetd(bd), -0.4), "bgetd parses negative exponent");
	CHECK(near_double(bgetd(bd), 0.75), "bgetd parses leading-dot fraction");
	CHECK(near_double(bgetd(bd), 42.0), "bgetd parses integer");
	CHECK(bgetc(bd) == 'e', "bgetd leaves word terminator unread");
	rmbio(bd);

	rewrite_file(path, "12e-x 7E+q 8Ez");
	bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen for bgetd malformed exponents");
	CHECK(near_double(bgetd(bd), 12.0), "bgetd ignores exponent sign without exponent digits");
	CHECK(bgetc(bd) == 'e' && bgetc(bd) == '-' && bgetc(bd) == 'x', "bgetd restores malformed negative exponent");
	CHECK(near_double(bgetd(bd), 7.0), "bgetd ignores positive exponent without digits");
	CHECK(bgetc(bd) == 'E' && bgetc(bd) == '+' && bgetc(bd) == 'q', "bgetd restores malformed positive exponent");
	CHECK(near_double(bgetd(bd), 8.0), "bgetd ignores bare exponent marker without digits");
	CHECK(bgetc(bd) == 'E' && bgetc(bd) == 'z', "bgetd restores bare exponent marker");
	rmbio(bd);

	rewrite_file(path, "12e-x");
	bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen for bgetd stale unget guard");
	CHECK(near_double(bgetd(bd), 12.0), "bgetd parses mantissa before malformed exponent");
	bungetc(bd);
	CHECK(bgetc(bd) == 'e' && bgetc(bd) == '-' && bgetc(bd) == 'x', "bgetd rollback leaves no stale bungetc byte");
	rmbio(bd);

	rewrite_file(path, "abc");
	bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen for bgetd malformed input");
	CHECK(bgetd(bd) == 0.0 && err(), "bgetd reports missing number");
	errmsg(null);
	CHECK(bgetc(bd) == 'a', "bgetd malformed input backs up first byte");
	rmbio(bd);

	rewrite_file(path, "");
	bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen for bgetd EOF");
	CHECK(bgetd(bd) == 0.0 && err(), "bgetd reports EOF before number");
	errmsg(null);
	rmbio(bd);

	bd = bopen(0, path, (omode) {.w = 1, .t = 1});
	CHECK(bd >= 0, "bopen write-only for bgetd rejection");
	CHECK(bgetd(bd) == 0.0 && err(), "bgetd rejects write-only bio");
	errmsg(null);
	rmbio(bd);
}

static void buffered_char_helpers(void) {
	char buf [128];

	heading("buffered char helpers");
	rewrite_file(path, "abc");
	int bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen for bgetc");
	CHECK(bgetc(bd) == 'a', "bgetc reads first byte");
	bungetc(bd);
	CHECK(bgetc(bd) == 'a', "bungetc replays previous byte");
	CHECK(bgetc(bd) == 'b', "bgetc resumes after replay");
	bungetc(bd);
	memset(buf, 0, sizeof(buf));
	CHECK(bread(bd, buf, 2) == 2 && memcmp(buf, "bc", 2) == 0, "bread drains byte pushback first");
	bungetc(bd);
	CHECK(bgetc(bd) < 0 && !err(), "bungetc without saved byte is harmless at EOF");
	rmbio(bd);

	rewrite_file(path, "abcdeZ");
	bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen for five byte backup");
	CHECK(bgetc(bd) == 'a', "backup byte 1 source");
	bungetc(bd);
	CHECK(bgetc(bd) == 'a', "backup byte 1 replay");
	CHECK(bgetc(bd) == 'b', "backup byte 2 source");
	bungetc(bd);
	CHECK(bgetc(bd) == 'b', "backup byte 2 replay");
	CHECK(bgetc(bd) == 'c', "backup byte 3 source");
	bungetc(bd);
	CHECK(bgetc(bd) == 'c', "backup byte 3 replay");
	CHECK(bgetc(bd) == 'd', "backup byte 4 source");
	bungetc(bd);
	CHECK(bgetc(bd) == 'd', "backup byte 4 replay");
	CHECK(bgetc(bd) == 'e', "backup byte 5 source");
	bungetc(bd);
	CHECK(bgetc(bd) == 'e', "backup byte 5 replay");
	CHECK(bgetc(bd) == 'Z', "stream resumes after five backed bytes");
	rmbio(bd);

	rewrite_file(path, "abc");
	bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen for final byte");
	CHECK(bgetc(bd) == 'a', "bgetc rereads first byte after reset");
	CHECK(bgetc(bd) == 'b', "bgetc rereads second byte after reset");
	CHECK(bgetc(bd) == 'c', "bgetc reads final byte");
	CHECK(bgetc(bd) < 0 && !err(), "bgetc returns EOF without error");
	rmbio(bd);

	bd = bopen(0, path, (omode) {.w = 1, .t = 1});
	CHECK(bd >= 0, "bopen for bputc");
	CHECK(bputc(bd, 'x') == 1, "bputc writes one byte");
	CHECK(bputc(bd, 'y') == 1, "bputc writes another byte");
	bclose(bd);
	rmbio(bd);
	check_path_text("xy", "bputc output persists");

	bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen read-only for bputc rejection");
	CHECK(bputc(bd, 'z') < 0, "bputc rejects read-only bio");
	check_expected_error(true, "bputc read-only sets error");
	rmbio(bd);

	bd = bopen(0, path, (omode) {.w = 1, .t = 1});
	CHECK(bd >= 0, "bopen write-only for bgetc rejection");
	CHECK(bgetc(bd) < 0, "bgetc rejects write-only bio");
	check_expected_error(true, "bgetc write-only sets error");
	rmbio(bd);

	memset(buf, 0, sizeof(buf));
}

static void buffered_rune_helpers(void) {
	char buf [128];

	heading("buffered rune helpers");
	rewrite_file(path, "A\xce\xbb\xf0\x9f\x90\xb1");
	int bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen for bgetrune");
	CHECK(bgetrune(bd) == 'A', "bgetrune reads ASCII rune");
	CHECK(bgetrune(bd) == 0x03bb, "bgetrune reads two-byte rune");
	bungetrune(bd);
	CHECK(bgetrune(bd) == 0x03bb, "bungetrune replays previous rune");
	bungetrune(bd);
	CHECK(bgetc(bd) == 0xce && bgetc(bd) == 0xbb, "bungetrune can replay previous rune as bytes");
	CHECK(bgetrune(bd) == 0x1f431, "bgetrune reads four-byte rune");
	bungetrune(bd);
	memset(buf, 0, sizeof(buf));
	CHECK(bread(bd, buf, 4) == 4 && memcmp(buf, "\xf0\x9f\x90\xb1", 4) == 0, "bread drains rune pushback as bytes");
	CHECK(bgetrune(bd) == ( rune ) -1 && !err(), "bgetrune returns EOF without error");
	rmbio(bd);

	bd = bopen(0, path, (omode) {.w = 1, .t = 1});
	CHECK(bd >= 0, "bopen for bputrune");
	CHECK(bputrune(bd, 'Z') == 1, "bputrune writes ASCII rune");
	CHECK(bputrune(bd, 0x03bb) == 2, "bputrune writes two-byte rune");
	CHECK(bputrune(bd, 0x1f431) == 4, "bputrune writes four-byte rune");
	bclose(bd);
	rmbio(bd);
	memset(buf, 0, sizeof(buf));
	read_file_text(path, buf, sizeof(buf));
	CHECK(strcmp(buf, "Z\xce\xbb\xf0\x9f\x90\xb1") == 0, "bputrune output persists");

	rewrite_file(path, "\xce");
	bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen incomplete UTF");
	CHECK(bgetrune(bd) == ( rune ) -1 && err(), "bgetrune rejects incomplete UTF");
	errmsg(null);
	rmbio(bd);

	bd = bopen(0, path, (omode) {.w = 1, .t = 1});
	CHECK(bd >= 0, "bopen for invalid bputrune");
	CHECK(bputrune(bd, 0x110000) == 3 && !err(), "bputrune maps invalid rune to Runeerror");
	rmbio(bd);
	memset(buf, 0, sizeof(buf));
	read_file_text(path, buf, sizeof(buf));
	CHECK(memcmp(buf, "\xef\xbf\xbd", 3) == 0, "invalid bputrune output persists as Runeerror");
}

static void buffered_line_helpers(void) {
	heading("buffered line helpers");
	rewrite_file(path, "one\ntwo\nlast");
	int bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen for brdline");
	arrst line = brdline(bd, '\n');
	CHECK(line.len == 4 && memcmp(line.x, "one\n", 4) == 0, "brdline includes delimiter");
	line = brdline(bd, '\n');
	CHECK(line.len == 4 && memcmp(line.x, "two\n", 4) == 0, "brdline reads second line");
	line = brdline(bd, '\n');
	CHECK(line.len == 4 && memcmp(line.x, "last", 4) == 0, "brdline returns final unterminated line");
	line = brdline(bd, '\n');
	CHECK(line.len == 0 && line.x == null && !err(), "brdline returns empty at EOF");
	rmbio(bd);

	rewrite_file(path, "abcdef\nrest");
	bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen for brdline after pushback");
	CHECK(bgetc(bd) == 'a' && bgetc(bd) == 'b', "bgetc primes read buffer before brdline");
	bungetc(bd);
	line = brdline(bd, '\n');
	CHECK(line.len == 6 && memcmp(line.x, "bcdef\n", 6) == 0, "brdline preserves unread bytes after pushback");
	line = brdline(bd, '\n');
	CHECK(line.len == 4 && memcmp(line.x, "rest", 4) == 0, "brdline resumes after pushed-back line");
	rmbio(bd);

	char *longline = aden(0, COETUA_BIO_BUFSZ + 32);
	memset(longline, 'x', COETUA_BIO_BUFSZ + 31);
	longline [COETUA_BIO_BUFSZ + 31] = 0;
	rewrite_file(path, longline);
	bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen for long brdline");
	line = brdline(bd, '\n');
	CHECK(line.len == COETUA_BIO_BUFSZ, "brdline returns full buffer without delimiter");
	line = brdline(bd, '\n');
	CHECK(line.len == 31, "brdline continues long unterminated line");
	rmbio(bd);

	longline [COETUA_BIO_BUFSZ + 30] = '\n';
	longline [COETUA_BIO_BUFSZ + 31] = 0;
	rewrite_file(path, longline);
	bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen for long brdstr");
	int   sd = brdstr(bd, 0, '\n', false);
	slitr s  = obslitr(sd);
	CHECK(s.len == COETUA_BIO_BUFSZ + 31 && s.s [s.len - 1] == '\n', "brdstr returns arbitrarily long line");
	rmstrand(sd);
	rmbio(bd);

	rewrite_file(path, "alpha\nbeta\n");
	bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen for brdstr");
	sd = brdstr(bd, 0, '\n', false);
	s  = obslitr(sd);
	CHECK(s.len == 6 && strcmp(s.s, "alpha\n") == 0, "brdstr includes delimiter by default");
	CHECK(s.s [s.len] == 0, "brdstr result is NUL terminated");
	rmstrand(sd);
	sd = brdstr(bd, 0, '\n', true);
	s  = obslitr(sd);
	CHECK(s.len == 4 && strcmp(s.s, "beta") == 0, "brdstr nulldelim omits delimiter");
	CHECK(s.s [s.len] == 0, "brdstr nulldelim result is NUL terminated");
	rmstrand(sd);
	rmbio(bd);

	rewrite_file(path, "A\xce\xbbZ");
	bd = bopen(0, path, (omode) {.r = 1});
	CHECK(bd >= 0, "bopen for rune-delimited brdstr");
	sd = brdstr(bd, 0, 0x03bb, true);
	s  = obslitr(sd);
	CHECK(s.len == 1 && strcmp(s.s, "A") == 0, "brdstr accepts rune delimiter");
	rmstrand(sd);
	CHECK(bgetc(bd) == 'Z', "brdstr leaves bytes after delimiter unread");
	rmbio(bd);
}

static void buffered_error_edges(void) {
	heading("error edges");
	binit(-1, -1, (omode) {.r = 1});
	check_expected_error(true, "binit bad descriptor sets error");
	check_expected_error(bfildes(-1) < 0, "bfildes bad descriptor sets error");
	check_expected_error(bbuffered(-1) == 0, "bbuffered bad descriptor sets error");
	bflush(-1);
	check_expected_error(true, "bflush bad descriptor sets error");
	check_expected_error(brdstr(-1, 0, '\n', false) < 0, "brdstr bad descriptor sets error");
}

int main(void) {
	cleanup_files();

	buffered_delete_on_close();
	buffered_first_slice();
	buffered_edge_cases();
	buffered_descriptor_growth();
	buffered_small_edges();
	buffered_seek_offset();
	buffered_print_helpers();
	buffered_getd_helper();
	buffered_char_helpers();
	buffered_rune_helpers();
	buffered_line_helpers();
	buffered_error_edges();

	printf("\n=== result: %d failures ===\n", failures);
	cleanup_files();
	return failures;
}
