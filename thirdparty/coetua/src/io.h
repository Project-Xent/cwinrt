#pragma once
#include "atom.h"

/* ═══════════════════════════════════════════════════════
   I/O — raw file descriptors, POSIX-style
   fd 0/1/2 are reserved for stdin/stdout/stderr.
   Current backend is Windows-first; path normalization and
   richer permission policy belong to future path/raw-I/O work.
   ═══════════════════════════════════════════════════════ */

/* ── File open / create ─────────────────────────────── */

/* Open mode flags */
typedef struct omode {
	bool r:1; /* read                         */
	bool w:1; /* write                        */
	bool x:1; /* exclusive (fail if exists)   */
	bool t:1; /* truncate to zero before open */
	bool d:1; /* remove after close (Windows raw d I/O) */
	bool a:1; /* append-only                  */
} omode;

/* Permission bits */
typedef struct perm {
	ushort bits :9; /* rwx for owner/group/other */
	bool   isdir:1;
	bool   islnk:1;
} perm;

/* `perm` is a thin platform-facing mode shape for raw I/O. On Windows,
   owner-write state is currently exposed as read-only/create-time mapping.
   Rich ACL policy and UTF-16 path normalization remain future work. */

/* Convert internal owner/group/other rwx bits to low 9 OS mode bits. */
int   permtomode(perm p);

/* Open an existing file. Returns fd >= 0, or -1 (check errmsg). */
/* Paths are passed directly to the platform layer.  On Windows, use ordinary
   narrow paths with forward slashes; UTF-16/path normalization belongs in a
   future path layer, not raw d I/O. */
int   dopen(char *file, omode mod);

/* Create a new file with given permissions. */
int   dcreate(char *file, omode mod, perm pm);

/* Close a file descriptor. */
void  dclose(int fd);

/* ── Read / Write ──────────────────────────────────── */

/* Read up to len bytes from fd into buf at current offset.
   Returns bytes read (0 = EOF), or -1 on error. */
vlong dread(int fd, void *buf, uvlong len);

static vlong inline dreada(int fd, arrst buf) { return dread(fd, buf.x, buf.len); }

/* Read exactly len bytes, retrying on short reads.
   Returns len on success, < len on EOF/error. */
uvlong dreadn(int fd, void *buf, uvlong len);

static uvlong inline dreadna(int fd, arrst buf) { return dreadn(fd, buf.x, buf.len); }

/* Write len bytes to fd at current offset.
   Returns bytes written, or -1 on error. */
vlong dwrite(int fd, void *buf, uvlong len);

static vlong inline dwritea(int fd, arrst buf) { return dwrite(fd, buf.x, buf.len); }

/* ── Seek ───────────────────────────────────────────── */

/* whence: 0=start, 1=current, 2=end.
   amount: positive = forward, negative = backward.
   Returns new offset, or -1 on error. */
vlong dseek(int fd, vlong amount, int whence);

/* ── Positional read ────────────────────────────────── */

/* Read len bytes from fd at absolute offset (does not change fd position). */
vlong pread(int fd, void *buf, uvlong len, uvlong offset);

static vlong inline preada(int fd, arrst buf, uvlong offset) { return pread(fd, buf.x, buf.len, offset); }

/* Write len bytes to fd at absolute offset (does not change fd position). */
vlong pwrite(int fd, void *buf, uvlong len, uvlong offset);

static vlong inline pwritea(int fd, arrst buf, uvlong offset) { return pwrite(fd, buf.x, buf.len, offset); }

/* ── Formatted I/O ──────────────────────────────────── */

/* Print to a file descriptor using fmts-style formatting.
   Returns bytes written, or -1 on error. */
int dvprint(int fd, char *fm, va_list args);
int dprint(int fd, char *fm, ...);
