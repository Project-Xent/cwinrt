#include "coetua.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

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

static void check_expected_error(bool ok, char *label) {
	CHECK(ok && err(), label);
	errmsg(null);
}

static bool verb_U(fmt *fp) {
	char *s = va_arg(fp->args, char *);
	if (!s) s = "";
	while (*s) {
		char c = *s++;
		if (c >= 'a' && c <= 'z') c -= 32;
		concatr(fp->sd, ( rune ) ( uchar ) c);
	}
	return true;
}

/* custom struct for # flag test */
typedef struct {
	int   id;
	char *name;
} Item;

static bool verb_I(fmt *fp) {
	Item *it = va_arg(fp->args, Item *);
	int   sd = fmts(0, "%d:%s", it->id, it->name);
	if (sd >= 0) {
		slitr s   = obslitr(sd);
		int   pad = ((fp->flags & FMT_WIDTH) && fp->width > ( int ) s.len) ? fp->width - ( int ) s.len : 0;
		if (!(fp->flags & FMT_LEFT))
			while (pad-- > 0) concatr(fp->sd, ' ');
		if (s.len > 0) concats(fp->sd, s);
		if (fp->flags & FMT_LEFT)
			while (pad-- > 0) concatr(fp->sd, ' ');
		rmstrand(sd);
	}
	return true;
}

static bool flag_Z(fmt *fp) {
	concatr(fp->sd, '[');
	fp->flags |= FMT_ALT;
	return false;
}

static bool verb_phi(fmt *fp) {
	int v = va_arg(fp->args, int);
	if (fp->flags & FMT_ALT) concat(fp->sd, "alt:");
	int sd = fmts(0, "%d", v);
	if (sd >= 0) {
		slitr s = obslitr(sd);
		concats(fp->sd, s);
		rmstrand(sd);
	}
	concatr(fp->sd, ']');
	return true;
}

static void fill_zeros(char *buf, int count) {
	buf [0] = '1';
	buf [1] = '.';
	memset(buf + 2, '0', ( uvlong ) count);
	buf [2 + count] = '\0';
}

static double posinf(void) {
	volatile double z = 0.0;
	return 1.0 / z;
}

static double neginf(void) {
	volatile double z = 0.0;
	return -1.0 / z;
}

static double quietnan(void) {
	volatile double z = 0.0;
	return z / z;
}

static void heading(char *name) { printf("\n=== fmt: %s ===\n", name); }

static void expect_fmt(int sd, char *want, char *label) {
	CHECK(sd >= 0 && slitreq(obslitr(sd), slitr_of(want)), label);
}

static void basic_verbs(void) {
	heading("basic verbs");
	int sd;

	sd = fmts(0, "%s", "hello");
	expect_fmt(sd, "hello", "%%s");

	sd = fmts(0, "%d", 42);
	expect_fmt(sd, "42", "%%d");

	sd = fmts(0, "%d", -42);
	expect_fmt(sd, "-42", "%%d negative");

	sd = fmts(0, "%ud", 42);
	expect_fmt(sd, "42", "%%ud unsigned");

	sd = fmts(0, "%x", 255);
	expect_fmt(sd, "ff", "%%x");

	sd = fmts(0, "%X", 255);
	expect_fmt(sd, "FF", "%%X");

	sd = fmts(0, "%o", 8);
	expect_fmt(sd, "10", "%%o");

	sd = fmts(0, "%b", 5);
	expect_fmt(sd, "101", "%%b");

	sd = fmts(0, "%%");
	expect_fmt(sd, "%", "%%%%");

	sd = fmts(0, "%c", 'A');
	expect_fmt(sd, "A", "%%c");
}

static void width_and_precision(void) {
	heading("width and precision");
	int sd;

	sd = fmts(0, "%5d", 42);
	expect_fmt(sd, "   42", "%%5d");

	sd = fmts(0, "%-5d", 42);
	expect_fmt(sd, "42   ", "%%-5d");

	sd = fmts(0, "%.5d", 42);
	expect_fmt(sd, "00042", "%%.5d");

	sd = fmts(0, "%.5s", "hello");
	expect_fmt(sd, "hello", "%%.5s shorter than str");
	sd = fmts(0, "%.3s", "hello");
	expect_fmt(sd, "hel", "%%.3s truncation");

	sd = fmts(0, "%5s", "ab");
	expect_fmt(sd, "   ab", "%%5s");

	sd = fmts(0, "%*d", 6, 42);
	expect_fmt(sd, "    42", "%%*d");
}

static void flags(void) {
	heading("flags");
	int sd;

	sd = fmts(0, "%+d", 42);
	expect_fmt(sd, "+42", "%%+d");

	sd = fmts(0, "% d", 42);
	expect_fmt(sd, " 42", "%% d");

	sd = fmts(0, "%#x", 255);
	expect_fmt(sd, "0xff", "%%#x");

	sd = fmts(0, "%#X", 255);
	expect_fmt(sd, "0XFF", "%%#X");

	sd = fmts(0, "%#o", 8);
	expect_fmt(sd, "010", "%%#o");

	sd = fmts(0, "%#08x", 255);
	expect_fmt(sd, "0x0000ff", "%%#08x keeps prefix before zero padding");

	sd = fmts(0, "%+05d", 42);
	expect_fmt(sd, "+0042", "%%+05d keeps sign before zero padding");
}

static void length_modifiers(void) {
	heading("length modifiers");
	int sd;

	sd = fmts(0, "%lld", ( vlong ) LLONG_MAX);
	expect_fmt(sd, "9223372036854775807", "%%lld");

	sd = fmts(0, "%llud", ( uvlong ) ULLONG_MAX);
	expect_fmt(sd, "18446744073709551615", "%%llud");

	sd = fmts(0, "%llx", ( uvlong ) 0xdeadbeefcafebabeull);
	expect_fmt(sd, "deadbeefcafebabe", "%%llx");
}

static void pointer_and_error(void) {
	heading("pointer and error");
	int sd;

	int x;
	sd = fmts(0, "%p", &x);
	CHECK(sd >= 0, "%%p");
	slitr ps = obslitr(sd);
	CHECK(ps.len >= 3 && ps.s [0] == '0' && ps.s [1] == 'x', "%%p starts with 0x");

	errmsg("test error message");
	sd = fmts(0, "%r");
	expect_fmt(sd, "test error message", "%%r with error");
	errmsg(null);
	sd = fmts(0, "%r");
	expect_fmt(sd, "(no error)", "%%r no error");
}

static void combined_formats(void) {
	heading("combined");
	int sd;

	sd = fmts(0, "%s: %d + %d = %d", "sum", 2, 3, 5);

	expect_fmt(sd, "sum: 2 + 3 = 5", "combined");

	sd = fmts(0, "%s=%-6d %s=%05d", "x", 1, "y", 2);
	expect_fmt(sd, "x=1      y=00002", "combined flags");

	sd = fmts(0, "%s: 0x%08llx", "addr", ( uvlong ) 0x1234);
	expect_fmt(sd, "addr: 0x00001234", "combined hex");
}

static void null_edge_cases(void) {
	heading("null/edge cases");
	int sd;

	sd = fmts(0, "%s", null);
	expect_fmt(sd, "(null)", "%%s null");

	sd = fmts(0, "%d %d", 0, 0);
	expect_fmt(sd, "0 0", "zero values");

	sd = fmts(0, "");
	expect_fmt(sd, "", "empty format");

	sd = fmts(0, "no verbs here");
	expect_fmt(sd, "no verbs here", "literal only");

	sd = fmts(0, "trailing %");
	expect_fmt(sd, "trailing ", "trailing %%");
}

static void custom_verbs(void) {
	heading("custom verbs");
	int sd;

	fmtinstall('U', verb_U);
	sd = fmts(0, "%U", "hello world");
	expect_fmt(sd, "HELLO WORLD", "custom %%U");

	fmtinstall('Z', flag_Z);
	fmtinstall(0x03c6, verb_phi);
	sd = fmts(0, "%Zφ", 17);
	expect_fmt(sd, "[alt:17]", "custom flag before UTF-8 rune verb");

	fmtinstall('I', verb_I);
}

static void array_verb(void) {
	heading("%a array verb");
	int sd;

	/* spec: { %.4a } — basic int array */
	{
		int arr [] = {1, 2, 3, 4};
		sd         = fmts(0, "{ %.4a }", arr, "%d, ");
		expect_fmt(sd, "{ 1, 2, 3, 4,  }", "%%a basic int array");
	}

	/* spec: [ %.*a] — dynamic count */
	{
		int arr [] = {10, 20, 30};
		sd         = fmts(0, "[ %.*a]", 3, arr, "%d ");
		expect_fmt(sd, "[ 10 20 30 ]", "%%a dynamic count");
	}

	/* width pads the finished array field; precision is the C-array count */
	{
		int arr [] = {1, 2, 3};
		sd         = fmts(0, "|%12.3a|", arr, "%d ");
		expect_fmt(sd, "|      1 2 3 |", "%%a width pads array field");
		sd = fmts(0, "|%*.*a|", 12, 3, arr, "%d ");
		expect_fmt(sd, "|      1 2 3 |", "%%*.*a pads field and counts elements");
		sd = fmts(0, "|%*.*a|", -12, 3, arr, "%d ");
		expect_fmt(sd, "|1 2 3       |", "%%*.*a negative width left-pads array field");
	}

	/* spec: %.3ua — unsigned sub-format */
	{
		uint arr [] = {0xff, 0x100, 0x1ff};
		sd          = fmts(0, "%.3ua", arr, "0x%04x ");
		expect_fmt(sd, "0x00ff 0x0100 0x01ff ", "%%a unsigned hex");
	}

	/* spec: ll flag — vlong array */
	{
		vlong arr [] = {1ll << 40, -(1ll << 40)};
		sd           = fmts(0, "%.2lla", arr, "%lld ");
		expect_fmt(sd, "1099511627776 -1099511627776 ", "%%a vlong");
	}

	/* spec: hh flag — byte array */
	{
		char arr [] = {65, 66, 67};
		sd          = fmts(0, "%.3hha", arr, "%c ");
		expect_fmt(sd, "A B C ", "%%a byte (hh)");
	}

	/* sub-format with literal wrapping: [%d] */
	{
		int arr [] = {7, 8, 9};
		sd         = fmts(0, "%.3a", arr, "[%d] ");
		expect_fmt(sd, "[7] [8] [9] ", "%%a with format-wrap");
	}

	/* spec: { %.3a%d } — trailing commas, mixed with outer verbs */
	{
		int arr [] = {1, 2, 3, 4};
		sd         = fmts(0, "{ %.3a%d }", arr, "%d, ", arr [3]);
		expect_fmt(sd, "{ 1, 2, 3, 4 }", "%%a trailing commas (spec)");
	}

	/* empty count */
	{
		int arr [] = {1, 2};
		sd         = fmts(0, "[%.0a]", arr, "%d");
		expect_fmt(sd, "[]", "%%a zero count");
	}
	{
		int arr [] = {1, 2};
		sd         = fmts(0, "[%.0a%d]", arr, "%.*d", 2, 7);
		expect_fmt(sd, "[7]", "%%a zero count consumes subfmt and stars");
	}

	/* spec: %.4#a — custom verb on struct array (# = void* + stride) */
	{
		Item items [] = {
		  {1, "alpha"},
		  {2, "beta" },
		  {3, "gamma"},
		};
		sd = fmts(0, "%.3#a", items, "%I ", sizeof(Item));
		expect_fmt(sd, "1:alpha 2:beta 3:gamma ", "%%a # custom verb (spec)");
	}
	{
		Item items [] = {
		  {1, "bad"},
		};
		sd = fmts(0, "%.1#a", items, "%I ", 0);
		CHECK(sd >= 0 && obslitr(sd).len == 0 && err(), "%%a # rejects zero stride");
		errmsg(null);
	}
	{
		Item items [] = {
		  {1, "a" },
		  {2, "bb"},
		};
		sd = fmts(0, "%.2#a", items, "%*I ", sizeof(Item), 8);
		expect_fmt(sd, "     1:a     2:bb ", "%%a # subfmt stars follow stride");
	}
	{
		int arr6 [2][3] = {
		  {1, 2, 3},
		  {4, 5, 6},
		};
		sd = fmts(0, "%.2#a", arr6, "[ %.*a]\n", sizeof(arr6 [0]), 3, "%d ");
		expect_fmt(sd, "[ 1 2 3 ]\n[ 4 5 6 ]\n", "%%a # nested subfmt stars follow stride");
		sd = fmts(0, "%.2#a", arr6, "|%*.*a|\n", sizeof(arr6 [0]), 10, 3, "%d ");
		expect_fmt(sd, "|    1 2 3 |\n|    4 5 6 |\n", "%%a # nested %%*.*a pads inner array field");
	}

	/* spec: (%.2ua%ud => %ud) — multi-verb subfmt, paired array elements */
	{
		uint arr6 [] = {10, 20, 30, 40, 50, 60};
		sd           = fmts(0, "(%.2ua%ud => %ud)", arr6, "%ud => %ud, ", arr6 [4], arr6 [5]);
		expect_fmt(sd, "(10 => 20, 30 => 40, 50 => 60)", "%%a multi-verb subfmt (spec)");
	}

	/* spec: < %.*,a> — dynamic width/precision via * flag in subfmt */
	{
		int arr [] = {1, 2, 3, 4};
		sd         = fmts(0, "< %.*a>", 4, arr, "%.*d ", 3);
		expect_fmt(sd, "< 001 002 003 004 >", "%%a with * flag in subfmt (spec)");
	}

	/* * flag + trailing args after subfmt */
	{
		int arr [] = {10, 20, 30};
		sd         = fmts(0, "|%.3a|", arr, "%*d ", 6);
		expect_fmt(sd, "|    10     20     30 |", "%%a with * width flag");
	}

	/* multi-verb subfmt with more than two dynamic * specifications */
	{
		int arr [] = {7, 70, 8, 80};
		sd         = fmts(0, "|%.2a|", arr, "%*.*d/%*d ", 3, 2, 4);
		expect_fmt(sd, "| 07/  70  08/  80 |", "%%a multi-verb subfmt with >2 * flags");
	}

	/* spec: [ %;a] — arrst supplies count and data */
	{
		int arr [] = {11, 22, 33};
		sd         = fmts(0, "[ %;a]", toarrst(arr), "%d ");
		expect_fmt(sd, "[ 11 22 33 ]", "%%a arrst source (;)");
	}

	/* spec: < %.*,a> — comma flag uses float array elements */
	{
		float arr [] = {1.25f, 2.5f, 3.75f};
		sd           = fmts(0, "< %.*,a>", 3, arr, "%.*f ", 2);
		expect_fmt(sd, "< 1.25 2.50 3.75 >", "%%a float array (,)");
	}
}

static void floating_point(void) {
	heading("floating point");
	int sd;

	{
		sd = fmts(0, "%f", 3.14);
		expect_fmt(sd, "3.140000", "%%f default");
	}

	{
		sd = fmts(0, "%.2f", 3.14159);
		expect_fmt(sd, "3.14", "%%.2f precision");
	}

	{
		sd = fmts(0, "%f", -42.5);
		expect_fmt(sd, "-42.500000", "%%f negative");
	}

	{
		sd = fmts(0, "%+f", 1.0);
		expect_fmt(sd, "+1.000000", "%%+f sign");
	}
	{
		sd = fmts(0, "%#f", 1.0);
		expect_fmt(sd, "1.000000", "%%#f keeps decimal form");
	}
	{
		sd = fmts(0, "%.0f", 1.4);
		expect_fmt(sd, "1", "%%.0f suppresses decimal");
	}
	{
		sd = fmts(0, "%#.0f", 1.4);
		expect_fmt(sd, "1.", "%%#.0f keeps decimal point");
	}
	{
		long double ld = 3.25l;
		sd             = fmts(0, "%lf", ld);
		expect_fmt(sd, "3.250000", "%%lf long double fixed");
	}

	{
		sd = fmts(0, "%e", 3.14);
		expect_fmt(sd, "3.140000e+00", "%%e basic");
	}

	{
		sd = fmts(0, "%E", 3.14);
		expect_fmt(sd, "3.140000E+00", "%%E basic");
	}
	{
		sd = fmts(0, "%#.0e", 3.14);
		expect_fmt(sd, "3.e+00", "%%#.0e keeps decimal point");
	}

	{
		sd = fmts(0, "%g", 3.14);
		expect_fmt(sd, "3.14", "%%g basic");
	}

	{
		sd = fmts(0, "%g", 0.000012345);
		expect_fmt(sd, "1.2345e-05", "%%g small");
	}
	{
		sd = fmts(0, "%.4g", 12.3400);
		expect_fmt(sd, "12.34", "%%g trims trailing zeros");
	}
	{
		sd = fmts(0, "%#.4g", 12.3400);
		expect_fmt(sd, "12.34", "%%#g keeps significant trailing policy");
	}
	{
		sd = fmts(0, "%.2G", 12345.0);
		expect_fmt(sd, "1.2E+04", "%%G exponent selection");
	}

	{
		sd = fmts(0, "%f", 0.0);
		expect_fmt(sd, "0.000000", "%%f zero");
	}
}

static void floating_point_boundaries(void) {
	heading("floating point boundaries");
	int sd;

	{
		sd = fmts(0, "%.0f", 9.5);
		expect_fmt(sd, "10", "%%.0f carries into next digit");
	}
	{
		sd = fmts(0, "%.2e", 9.995);
		expect_fmt(sd, "1.00e+01", "%%e carries in the obvious scientific form");
	}
	{
		sd = fmts(0, "%.4e", 9.99995);
		expect_fmt(sd, "1.0000e+01", "%%e carries without artificial bias");
	}
	{
		sd = fmts(0, "%.2f", -0.0);
		expect_fmt(sd, "0.00", "%%f follows Plan 9: negative zero is zero");
	}
	{
		sd = fmts(0, "%.2e", -0.0);
		expect_fmt(sd, "0.00e+00", "%%e follows Plan 9: negative zero is zero");
	}
	{
		sd = fmts(0, "%.2g", -0.0);
		expect_fmt(sd, "0", "%%g follows Plan 9: negative zero is zero");
	}
	{
		sd = fmts(0, "%.4g", 0.0001);
		expect_fmt(sd, "0.0001", "%%g uses fixed at exponent -4");
	}
	{
		sd = fmts(0, "%.4g", 0.00001);
		expect_fmt(sd, "1e-05", "%%g uses exponent below -4");
	}
	{
		sd = fmts(0, "%#.4g", 12.0);
		expect_fmt(sd, "12.00", "%%#g preserves trailing significant zeros");
	}
	{
		sd = fmts(0, "%.4g", 9999.5);
		expect_fmt(sd, "1e+04", "%%g switches to exponent form after rounding");
	}
	{
		sd = fmts(0, "%.4g", 0.00099995);
		expect_fmt(sd, "0.001", "%%g switches to fixed form after rounding");
	}
	{
		long double ld = 3.25l;
		sd             = fmts(0, "%le", ld);
		expect_fmt(sd, "3.250000e+00", "%%le reads long double");
	}
	{
		long double ld = 12345.0l;
		sd             = fmts(0, "%.2lG", ld);
		expect_fmt(sd, "1.2E+04", "%%lG reads long double");
	}
	{
		long double ld = 0.0000125l;
		sd             = fmts(0, "%.3lg", ld);
		expect_fmt(sd, "1.25e-05", "%%lg reads long double");
	}
	{
		sd = fmts(0, "%f", 3.14159e10);
		expect_fmt(sd, "31415900000.000000", "%%f handles Plan9 large fixed example");
	}
	{
		sd = fmts(0, "%f", 3.14159e-10);
		expect_fmt(sd, "0.000000", "%%f handles Plan9 tiny fixed example");
	}
	{
		sd = fmts(0, "%g", 2e25);
		expect_fmt(sd, "2e+25", "%%g handles huge exponent without integer overflow");
	}
	{
		sd = fmts(0, "%.0f", 1e20);
		expect_fmt(sd, "100000000000000000000", "%%f handles fixed integer part beyond uvlong");
	}
	{
		sd = fmts(0, "%.2f", 1e20);
		expect_fmt(sd, "100000000000000000000.00", "%%f handles huge fixed fraction padding");
	}
	{
		sd = fmts(0, "%e", 3.14159e10);
		expect_fmt(sd, "3.141590e+10", "%%e handles Plan9 large exponent example");
	}
	{
		sd = fmts(0, "%e", 3.14159e-10);
		expect_fmt(sd, "3.141590e-10", "%%e handles Plan9 tiny exponent example");
	}
	{
		sd = fmts(0, "%g", 3.14159e10);
		expect_fmt(sd, "3.14159e+10", "%%g handles Plan9 large exponent example");
	}
	{
		sd = fmts(0, "%g", 3.14159e-10);
		expect_fmt(sd, "3.14159e-10", "%%g handles Plan9 tiny exponent example");
	}
	{
		sd = fmts(0, "%.18g", 2e25);
		expect_fmt(sd, "2e+25", "%%.18g follows Plan9-style round-trip core digits");
	}
	{
		char expected [64];
		fill_zeros(expected, 30);
		sd      = fmts(0, "%.30f", 1.0);
		slitr s = obslitr(sd);
		CHECK(sd >= 0 && slitreq(s, slitr_of(expected)), "%%.30f exposes thirty fractional digits");
	}
	{
		char expected [64];
		fill_zeros(expected, 31);
		sd      = fmts(0, "%.31f", 1.0);
		slitr s = obslitr(sd);
		CHECK(sd >= 0 && slitreq(s, slitr_of(expected)), "%%.31f is not clamped to thirty digits");
	}
	{
		char expected [64];
		fill_zeros(expected, 30);
		memcpy(expected + 32, "e+00", 5);
		sd      = fmts(0, "%.30e", 1.0);
		slitr s = obslitr(sd);
		CHECK(sd >= 0 && slitreq(s, slitr_of(expected)), "%%.30e exposes thirty fractional digits");
	}
	{
		char expected [64];
		fill_zeros(expected, 31);
		memcpy(expected + 33, "e+00", 5);
		sd      = fmts(0, "%.31e", 1.0);
		slitr s = obslitr(sd);
		CHECK(sd >= 0 && slitreq(s, slitr_of(expected)), "%%.31e is not clamped to thirty digits");
	}
	{
		sd = fmts(0, "%.30g", 1.0);
		expect_fmt(sd, "1", "%%.30g keeps compact form for integral value");
	}
	{
		char expected [64];
		fill_zeros(expected, 29);
		sd      = fmts(0, "%#.30g", 1.0);
		slitr s = obslitr(sd);
		CHECK(sd >= 0 && slitreq(s, slitr_of(expected)), "%%#.30g retains thirty significant digits formatting");
	}
	{
		char expected [64];
		fill_zeros(expected, 30);
		sd      = fmts(0, "%#.31g", 1.0);
		slitr s = obslitr(sd);
		CHECK(sd >= 0 && slitreq(s, slitr_of(expected)), "%%#.31g is not clamped to thirty significant digits");
	}
	{
		sd = fmts(0, "%2.18g", 1.0);
		expect_fmt(sd, " 1", "%%2.18g trims one and honors field width");
	}
	{
		sd = fmts(0, "%2.18f", 1.0);
		expect_fmt(sd, "1.000000000000000000", "%%2.18f preserves requested fixed precision");
	}
	{
		double inf = posinf();
		sd         = fmts(0, "%f", inf);
		expect_fmt(sd, "+Inf", "%%f formats +Inf like Plan 9");
	}
	{
		double inf = posinf();
		sd         = fmts(0, "%+f", inf);
		expect_fmt(sd, "+Inf", "%%+f keeps Plan 9 +Inf spelling");
	}
	{
		double inf = neginf();
		sd         = fmts(0, "%f", inf);
		expect_fmt(sd, "-Inf", "%%f formats -Inf like Plan 9");
	}
	{
		double inf = posinf();
		sd         = fmts(0, "%E", inf);
		expect_fmt(sd, "+Inf", "%%E keeps Plan 9 +Inf spelling");
	}
	{
		double nanv = quietnan();
		sd          = fmts(0, "%G", nanv);
		expect_fmt(sd, "NaN", "%%G keeps Plan 9 NaN spelling");
	}
	{
		double nanv = quietnan();
		sd          = fmts(0, "%+g", nanv);
		expect_fmt(sd, "NaN", "%%+g does not sign Plan 9 NaN");
	}
	{
		double nanv = quietnan();
		sd          = fmts(0, "% G", nanv);
		expect_fmt(sd, "NaN", "%% G does not space-sign Plan 9 NaN");
	}
	{
		double inf = posinf();
		sd         = fmts(0, "%6f", inf);
		expect_fmt(sd, "  +Inf", "%%f pads +Inf with width");
	}
	{
		double inf = posinf();
		sd         = fmts(0, "%06f", inf);
		expect_fmt(sd, "  +Inf", "%%f space-pads +Inf despite zero flag");
	}
	{
		double nanv = quietnan();
		sd          = fmts(0, "%06g", nanv);
		expect_fmt(sd, "   NaN", "%%g space-pads NaN despite zero flag");
	}
	{
		sd = fmts(0, "%010.2f", 3.5);
		expect_fmt(sd, "0000003.50", "%%f zero pads after optional sign");
	}
	{
		sd = fmts(0, "%+010.2f", 3.5);
		expect_fmt(sd, "+000003.50", "%%f zero pads after plus sign");
	}
	{
		sd = fmts(0, "%010.2f", -3.5);
		expect_fmt(sd, "-000003.50", "%%f zero pads after minus sign");
	}
	{
		sd = fmts(0, "%-10.2e", 3.5);
		expect_fmt(sd, "3.50e+00  ", "%%e left justifies within width");
	}
}

static void slitr_verb(void) {
	heading("%t slitr verb");
	int sd;

	{
		slitr sl = slitr_of("hello world");
		sd       = fmts(0, "[%t]", sl);
		expect_fmt(sd, "[hello world]", "%%t basic slitr");
	}
	{
		slitr sl = slitr_of("abcdef");
		sd       = fmts(0, "%.3t", sl);
		expect_fmt(sd, "abc", "%%t precision truncation");
	}
	{
		slitr sl = slitr_of("ab");
		sd       = fmts(0, "%5t", sl);
		expect_fmt(sd, "   ab", "%%t width padding");
	}
	{
		slitr sl = slitr_of("ab");
		sd       = fmts(0, "%-5t", sl);
		expect_fmt(sd, "ab   ", "%%t left justify");
	}
}

static void comma_flags(void) {
	heading("comma flags");
	int sd;

	sd = fmts(0, "%,d", 1234567);
	expect_fmt(sd, "1,234,567", "comma every 3 digits");
	sd = fmts(0, "%,d", 42);
	expect_fmt(sd, "42", "comma short number");
	sd = fmts(0, "%,d", 0);
	expect_fmt(sd, "0", "comma zero");
	sd = fmts(0, "%,d", -1234567);
	expect_fmt(sd, "-1,234,567", "comma negative");
	sd = fmts(0, "%;d", 12345678);
	expect_fmt(sd, "1234,5678", "comma every 4 digits (;)");
	sd = fmts(0, "%,u", 1000000u);
	expect_fmt(sd, "1,000,000", "comma unsigned");
	sd = fmts(0, "%,b", 255);
	expect_fmt(sd, "11,111,111", "comma binary");
	sd = fmts(0, "%ll,b", ( uvlong ) ULLONG_MAX);
	expect_fmt(sd, "1,111,111,111,111,111,111,111,111,111,111,111,111,111,111,111,111,111,111,111,111,111",
	           "comma binary full uvlong");
	sd = fmts(0, "%lld", ( vlong ) LLONG_MIN);
	expect_fmt(sd, "-9223372036854775808", "signed minimum without overflow");
}

static void rune_verb(void) {
	heading("%S rune verb");
	int sd;

	{
		rune r [] = {'h', 'e', 'l', 'l', 'o', 0};
		sd        = fmts(0, "[%S]", r);
		expect_fmt(sd, "[hello]", "%%S basic runes");
	}
	{
		rune r [] = {'a', 'b', 'c', 'd', 'e', 'f', 0};
		sd        = fmts(0, "%.3S", r);
		expect_fmt(sd, "abc", "%%S precision truncation");
	}
	{
		rune r [] = {'x', 0};
		sd        = fmts(0, "%5S", r);
		expect_fmt(sd, "    x", "%%S width padding");
	}
	{
		/* UTF-8 multi-byte: 中 = U+4E2D */
		rune r [] = {0x4e2d, 0x6587, 0}; /* 中文 */
		sd        = fmts(0, "[%S]", r);
		expect_fmt(sd, "[中文]", "%%S CJK runes");
	}
}

static void error_edges(void) {
	heading("errors");
	check_expected_error(fmts(0, null) < 0, "fmts null format sets error");
	fmtinstall('Q', null);
	check_expected_error(true, "fmtinstall null callback sets error");
	int sd = fmts(0, "%t", (slitr) {.s = null, .len = 1});
	check_expected_error(sd >= 0, "%%t null nonzero slitr sets error");
	if (sd >= 0) rmstrand(sd);
	sd = fmts(0, "%;a", (arrst) {.x = null, .len = 1}, "%d");
	check_expected_error(sd >= 0, "%%a null nonzero arrst source sets error");
	if (sd >= 0) rmstrand(sd);
	sd = fmts(0, "%.1a", ( void * ) null, "%d");
	check_expected_error(sd >= 0, "%%a null counted source sets error");
	if (sd >= 0) rmstrand(sd);
	sd = fmts(0, "%.0a", ( void * ) null, "%d");
	CHECK(sd >= 0 && obslitr(sd).len == 0 && !err(), "%%a zero count accepts null source");
	if (sd >= 0) rmstrand(sd);
}

int main(void) {
	basic_verbs();
	width_and_precision();
	flags();
	length_modifiers();
	pointer_and_error();
	combined_formats();
	null_edge_cases();
	custom_verbs();
	array_verb();
	floating_point();
	floating_point_boundaries();
	slitr_verb();
	comma_flags();
	rune_verb();
	error_edges();

	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
