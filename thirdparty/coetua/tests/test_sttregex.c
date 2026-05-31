#include "coetua.h"
#include "sttregex.h"
#include <stdio.h>

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

static void expect_stt(char *input, char *script, char *want, char *label) {
	int sd = sttregex(0, input, script);
	if (sd < 0) {
		CHECK(false, label);
		return;
	}
	slitr got = obslitr(sd);
	bool  ok  = slitreq(got, slitr_of(want));
	if (!ok)
		printf("  script: %s\n  got:    '%.*s'\n  want:   '%s'\n", script, ( int ) got.len, got.s ? got.s : "", want);
	CHECK(ok, label);
	rmstrand(sd);
}

static void expect_stt_error(char *input, char *script, char *want, char *label) {
	errmsg(null);
	int sd = sttregex(0, input, script);
	if (sd < 0) {
		CHECK(false, label);
		return;
	}
	slitr got = obslitr(sd);
	bool  ok  = slitreq(got, slitr_of(want)) && err();
	if (!ok)
		printf("  script: %s\n  got:    '%.*s'\n  want:   '%s'\n  err:    '%s'\n", script, ( int ) got.len,
		       got.s ? got.s : "", want, errmsg(null));
	CHECK(ok, label);
	errmsg(null);
	rmstrand(sd);
}

static void basic_commands(void) {
	printf("=== sttregex: basic commands ===\n");

	expect_stt("abc123def456ghi", "x/%d+/p", "123456", "x picks digit runs");
	expect_stt("hello world\nfoo bar\nhello again\nbye\n", "x/.*\\n/ g/hello/p", "hello world\nhello again\n",
	           "x with guard keeps matching lines");
	expect_stt("hello world", "s/world/coetua/", "hello coetua", "s substitutes a match");
	expect_stt("remove me", "d", "", "d deletes current selection");
	expect_stt("keep me", "p", "keep me", "p keeps current selection");
	expect_stt("original", "c/replacement/", "replacement", "c replaces current selection");
	expect_stt("hello", "a/world/", "helloworld", "a appends text");
	expect_stt("world", "i/hello /", "hello world", "i inserts text");
}

static void captures_and_chains(void) {
	printf("\n=== sttregex: captures and chains ===\n");

	expect_stt("move(x, y)", "x/move\\(([^,]+), ([^)]+)\\)/ c/\\2, \\1/", "y, x", "replacement can reorder captures");
	expect_stt("9front", "x/^(%d)/d a/\\1/", "front9", "x hands captures to following command");
	expect_stt("a1b22c333", "x/%d+/d", "abc", "x edit deletes all digit runs");
	expect_stt("a1b22c333", "x/%d+/d a/!/", "abc!", "post-x command runs once on edited text");
	expect_stt("a1b22c333", "x/%d+/p a/!/", "122333!", "post-x command runs once on picked text");
	expect_stt("  9front  ", "x/%s*%w+%s*/ x/%w+/ x/^(%d)/d a/\\1/", "  front9  ",
	           "nested x preserves padding while editing word");
	expect_stt("target  \n  9front  \n  0cirn\n", "x/%s*%w+%s*\\n/ x/%w+/ x/^(%d)/d a/\\1/",
	           "target  \n  front9  \n  cirn0\n", "nested x edits words inside lines");
	expect_stt("ab", "x/(a)(b)/ c/[&|\\1|\\2]/", "[ab|a|b]", "replacement expands whole match and captures");
	expect_stt("ab", "x/(a)(b)/ c/[\\&|\\1|\\2]/", "[&|a|b]", "replacement escapes ampersand");
	expect_stt("abcde", "x/(a)(bcde)/ x/(b)cde/ x/(c)(de)/ c/[&|\\1|\\2|\\3|\\4|\\5]/", "ab[cde|a|bcde|b|c|de]",
	           "nested captures append left to right");
	expect_stt("a1 b2", "x/(%a)(%d)/ c/\\2\\1/", "1a 2b", "x rewrites each alnum pair");
	expect_stt("aaaa", "x/a@()'/ c/\\1/", "4", "captured star quantity expands");
	expect_stt("aaaaa", "x/a@(2)+/ c/\\1/", "5", "captured at-least quantity expands");
	expect_stt("aaa", "x/a@(3)-/ c/\\1/", "3", "captured at-most quantity expands");
	expect_stt("aaaa", "x/a@(2~4)'/ c/\\1/", "4", "captured range quantity expands");
	expect_stt("aaaaa", "x/a@(2^4);/ c/\\1/", "5", "captured outside quantity expands above range");
	expect_stt("a", "x/a@(2^4);/ c/\\1/", "1", "captured outside quantity expands below range");
	expect_stt("aaaa", "x/(a)@()'/ c/\\1:\\2/", "a:4", "text capture and quantity capture share numbering");
}

static void cality_examples(void) {
	printf("\n=== sttregex: Cality documented examples ===\n");

	expect_stt("one\nstring here\ntwo\nother string\n", "x/.*\\n/ g/string/p", "string here\nother string\n",
	           "Cality example keeps lines containing string");
	expect_stt("rob\nrobot\nrobust\nplain\n", "x/.*\\n/ g/rob/ v/robot/p", "rob\nrobust\n",
	           "Cality example filters rob but not robot");
	expect_stt("i am in this list", "x/%a+/ g/i/ v/../ c/I/", "I am in this list",
	           "Cality example capitalizes standalone i");
	expect_stt("before { move(x,y); keep(); } after move(a,b)", "x/{[^}]*}/ s/move\\(([^,]+),([^)]+)\\)/move(\\2,\\1)/",
	           "before { move(y,x); keep(); } after move(a,b)", "Cality example substitutes only inside braces");
}

static void complement_and_guards(void) {
	printf("\n=== sttregex: complement and guards ===\n");

	expect_stt("a1b22c333", "y/%d+/p", "abc", "y picks gaps between digit runs");
	expect_stt("a1b22c333", "y/%d+/d", "122333", "y edit deletes gaps");
	expect_stt("a1b22c333", "y/%d+/c/!/", "!1!22!333", "y edit replaces nonempty gaps");
	expect_stt("hello world", "v/hello/d", "hello world", "failed v edit preserves selection");
	expect_stt("hello world", "v/xyz/p", "hello world", "passing v pick keeps selection");
	expect_stt("hello world", "v/xyz/d", "", "passing v edit deletes selection");
	expect_stt("hello world", "g/hello/p", "hello world", "passing g pick keeps selection once");
	expect_stt("hello world", "g/xyz/p", "", "failed g pick contributes nothing");
	expect_stt("hello world", "g/hello/d", "", "passing g edit deletes selection");
	expect_stt("hello world", "g/xyz/d", "hello world", "failed g edit preserves selection");
	expect_stt("hello world", "v/hello/p", "", "failed v pick contributes nothing");

	expect_stt("abc", "x/%d+/p", "", "x pick with no matches is empty");
	expect_stt("abc", "x/%d+/d", "abc", "x edit with no matches preserves input");
	expect_stt("abc", "y/%d+/p", "abc", "y pick with no matches keeps whole gap");
	expect_stt("abc", "y/%d+/d", "", "y delete with no matches deletes whole gap");
	expect_stt("1a2", "y/%d+/a/!/", "1a!2", "y ignores leading empty gap");
}

static void substitution_tails(void) {
	printf("\n=== sttregex: substitution tails ===\n");

	expect_stt("a1 b22", "s/%d+/[&]/ x/%d+/p", "122", "post-substitution x sees substituted text");
	expect_stt("abc", "s/%d+/X/ a/!/", "abc!", "post-substitution command runs on unchanged text");
}

static void parser_edges(void) {
	printf("\n=== sttregex: parser edges ===\n");

	expect_stt("abc", "x/^/a/X/", "abc", "x ignores empty regex matches");
	expect_stt("abc", "s/^/X/", "abc", "s ignores empty regex matches");
	expect_stt_error("abc", "x/abc", "abc", "unterminated regex field reports error");
	expect_stt_error("abc", "c/abc", "abc", "unterminated text field reports error");
	expect_stt_error("abc", "q", "abc", "unknown command reports error");
	errmsg(null);
	CHECK(sttregex(0, null, "p") < 0 && err(), "null input reports error");
	errmsg(null);
	CHECK(sttregex(0, "abc", null) < 0 && err(), "null script reports error");
	errmsg(null);
}

int main(void) {
	basic_commands();
	captures_and_chains();
	cality_examples();
	complement_and_guards();
	substitution_tails();
	parser_edges();

	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
