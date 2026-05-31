#pragma once
#include "atom.h"

/* ═══════════════════════════════════════════════════════
   sttregex — structural regular expressions
   Applies Plan 9-style x/y/g/v/s commands to a strand.
   Uses regex9 VM underneath.
   ═══════════════════════════════════════════════════════ */

/*
   sttregex walks structural commands over an input string and returns a new
   strand.  Regex matches are leftmost-longest.  Commands may be chained or
   nested; each inner command sees the text selected by the outer command.

    Regex syntax, as accepted by regex9:

       .        any single byte except newline
       ^ $      start and end of the current working text
       ? * +    optional, zero-or-more, one-or-more
       |        alternation
       (...)    grouping and numbered capture; \1, \2, ... refer to groups
       [...]    character class; [^...] negates; ranges use '-'
       \n \t \r \\ \/ literal escapes; \uXXXX and \UXXXXXXXX rune escapes

    Shorthand classes use % plus a letter.  Uppercase means complement:

       %d digit          %D non-digit
       %a ASCII letter   %A non-letter
       %w word           %W non-word
       %s whitespace     %S non-whitespace
       %p punctuation    %P non-punctuation
       %c control        %C non-control
       %x hex digit      %X non-hex
       %l lowercase      %L non-lowercase
       %u uppercase      %U non-uppercase
       %g printable      %G non-printable

    Coetua also supports counted @ quantifiers on the preceding atom/group:

       @n'      exactly n repetitions
       @n+      n or more repetitions
       @m-      m or fewer repetitions
       @n~m'    between n and m repetitions, inclusive
       @n^m;    outside n..m repetitions: fewer than n or more than m

    A captured quantity form records the number of repetitions instead of a
    text span.  These forms share the same left-to-right capture numbering as
    (...), and can be expanded with \1, \2, ... in structural replacements:

       @()'     captured zero-or-more repetitions
       @(n)+    captured n-or-more repetitions
       @(m)-    captured m-or-fewer repetitions
       @(n~m)'  captured range repetitions
       @(n^m);  captured outside-range repetitions

   x/re/cmd   run cmd on each match
   y/re/cmd   run cmd on each gap between matches
   g/re/cmd   run cmd if current text matches
   v/re/cmd   run cmd if current text does not match
   s/re/t/    substitute matches with t

   p collects the current selection into the result.  d deletes it.  c/t/
   changes it, a/t/ appends to it, and i/t/ inserts before it.  Guards are
   selection filters: a failed g or satisfied v contributes nothing for p, but
   leaves the selection unchanged for mutating commands that are not run.  In
   replacements, & is the current whole match, \& is a literal ampersand, and
   \1, \2, ... are captures.  Nested x/g/v/s commands append their capture
   groups to the outer capture set while & remains the current working match.
   ^ and $ bind to the current working text.  Empty regex matches are ignored
   by x, y, and s scans; this keeps structural walks finite and byte-progressing.

   Examples:
     x/.*\n/ g/string/p
     x/.*\n/ g/rob/ v/robot/p
     x/%a+/ g/i/ v/../ c/I/
     x/{[^}]*}/ s/move\(([^,]+),([^)]+)\)/move(\2,\1)/
 */

/* Apply structural regex commands to str and return a new strand in arena.
   se: command string, e.g. "x/.*\\n/ g/string/p".
   Returns -1 on allocation failure.  Parse/regex errors leave the input copy
   as the returned strand and set errmsg(). */
int sttregex(int arena, char *str, char *se);
