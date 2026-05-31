#pragma once
#include "atom.h"
#include "text.h"
#include <stdarg.h>

/* ═══════════════════════════════════════════════════════
   fmt — formatted output to a strand
   Custom verb dispatch is provided by fmtinstall.
   ═══════════════════════════════════════════════════════ */

/*
   fmts creates a strand, writes formatted output into it, and returns its
   descriptor.  Observe with obslitr(); release with rmstrand().  vfmts is the
   va_list form.

   Syntax: %[front-flags][width][.precision][flags]verb.  Only -, 0, +,
   and space must appear before width.  Width and precision may be '*' int
   arguments; negative dynamic width implies '-'.

   Flags: # , ; u h hh l ll.  #, h, and l may also appear before width.  ','
   groups integers by threes or marks %a as float-array input; ';' groups by
   fours or marks %a as arrst.

   Verbs:
     d o b x X   integers
     f e E g G   floats; Plan 9 spellings NaN, +Inf, -Inf; '.' decimal point
     s S c C t   char string, rune string, char, rune, slitr
     p r %       pointer, errmsg(), literal percent

   Integer precision pads digits.  Precision 0 with value 0 emits no digits.
   Alternate prefixes stay before zero field padding.

   %a formats arrays.  Precision is the C-array count; width pads the finished
   array field.  %;a reads an arrst and may omit precision.  %#a reads an extra
   element-stride argument for compound elements.  %,a reads float elements.

   %a arguments are linear:

       source, [element_stride], subfmt, [shared subfmt * args...]

   The subformat is normalized once; its '*' width/precision arguments are
   shared across all elements.  Each element is then formatted through the
   ordinary formatter.  No hidden globals, no shared va_list side channel.

   Examples:
       fmts(0, "[ %.3a ]", xs, "%d ")
       fmts(0, "|%12.3a|", xs, "%d ")
       fmts(0, "[ %;a]", mkarrst(n, xs), "%d ")
       fmts(0, "%.3#a", rows, "| %.3a|\\n", sizeof rows[0], "%d ")
 */

typedef struct fmt {
	rune    verb;  /* format verb (d, s, x, etc.) */
	int     sd;    /* target strand descriptor     */
	int     width; /* minimum field width           */
	int     prec;  /* precision                     */
	uint    flags; /* flag bits (see below)         */
	va_list args;  /* variadic arguments            */
} fmt;

/* Flags */
enum
{
	FMT_WIDTH  = 1 << 0,  /* width was specified        */
	FMT_PREC   = 1 << 1,  /* precision was specified    */
	FMT_ZERO   = 1 << 2,  /* pad with zeros (numeric)   */
	FMT_SHORT  = 1 << 3,  /* h  modifier                */
	FMT_BYTE   = 1 << 4,  /* hh modifier                */
	FMT_LONG   = 1 << 5,  /* l  modifier                */
	FMT_VLONG  = 1 << 6,  /* ll modifier                */
	FMT_UNSIGN = 1 << 7,  /* unsigned interpretation    */
	FMT_SPACE  = 1 << 8,  /* space before positive      */
	FMT_PLUS   = 1 << 9,  /* always show sign           */
	FMT_LEFT   = 1 << 10, /* left-justify               */
	FMT_ALT    = 1 << 11, /* alternate form (#)         */
	FMT_COMMA  = 1 << 12, /* comma every 3 digits (,)   */
	FMT_COMMA4 = 1 << 13, /* comma every 4 digits (;)   */
	FMT_FLOAT  = 1 << 14, /* array of float/double (,)  */
	FMT_ARRST  = 1 << 15, /* array struct source (;)    */
};

/* Format into a new strand in the given arena.
   Returns the strand descriptor, or -1 on error. */
int  vfmts(int arena, char *fm, va_list args);
int  fmts(int arena, char *fm, ...);

/* Core dispatcher: format one conversion.
   Returns true if fp->verb was consumed (verb), false if it was a flag. */
bool dofmt(fmt *fp);

/* Install a custom formatter for verb c.
   fn receives the fmt struct; should consume args and write to fp->sd.
   Return true if the verb was consumed, false to treat as a flag. */
void fmtinstall(rune c, bool (*fn)(fmt *));
