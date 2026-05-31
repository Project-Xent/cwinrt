#include "io.h"
#define COETUA_IO_BACKEND_IMPLEMENTATION
#include "internal/io_backend.h"
#include "config.h"
#include "err.h"
#include "fmt.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	io_handle handle;
	omode     mode;
	bool      used;
} fd_entry;

/* Thread-local: a coetua fd is opened, used, and closed within one thread, so
   the fd table is per-thread. Lock-free, matching the arena/strand tables. */
static _Thread_local fd_entry *fd_table;
static _Thread_local int       fdcap;

static int       fd_seed(void) { return COETUA_FD_TABLE_SEED < 4 ? 4 : COETUA_FD_TABLE_SEED; }

static bool      fd_table_init(void) {
	if (fd_table) return true;
	fdcap    = fd_seed();
	fd_table = ( fd_entry * ) calloc(( size_t ) fdcap, sizeof(fd_entry));
	if (!fd_table) {
		errmsg("io: out of memory");
		fdcap = 0;
		return false;
	}
	return true;
}

static bool fd_table_grow(void) {
	int newcap = fdcap ? fdcap * 2 : fd_seed();
	if (newcap <= fdcap) {
		errmsg("io: descriptor overflow");
		return false;
	}
	fd_entry *p = ( fd_entry * ) realloc(fd_table, ( size_t ) newcap * sizeof(fd_entry));
	if (!p) {
		errmsg("io: out of memory");
		return false;
	}
	memset(p + fdcap, 0, ( size_t ) (newcap - fdcap) * sizeof(fd_entry));
	fd_table = p;
	fdcap    = newcap;
	return true;
}

static bool fd_slot_free(int fd) { return fd >= 3 && !fd_table [fd].used; }

static int  fd_alloc(io_handle handle, omode mode) {
	if (!fd_table_init()) return -1;
	for (;;) {
		for (int i = 3; i < fdcap; i++)
			if (fd_slot_free(i)) {
				fd_table [i].handle = handle;
				fd_table [i].mode   = mode;
				fd_table [i].used   = true;
				return i;
			}
		if (!fd_table_grow()) return -1;
	}
}

static io_handle fd_get(int fd) {
	if (fd < 0) return io_handle_invalid();
	if (fd == 0) return io_os_stdin();
	if (fd == 1) return io_os_stdout();
	if (fd == 2) return io_os_stderr();
	if (!fd_table || fd >= fdcap || !fd_table [fd].used) return io_handle_invalid();
	return fd_table [fd].handle;
}

static bool fd_get_mode(int fd, omode *mode) {
	if (fd < 3 || !fd_table || fd >= fdcap || !fd_table [fd].used) return false;
	if (mode) *mode = fd_table [fd].mode;
	return true;
}

static int fd_from_handle(io_handle handle, omode mode, char *who) {
	if (!io_handle_valid(handle)) {
		if (!err()) errmsg(who);
		return -1;
	}
	int fd = fd_alloc(handle, mode);
	if (fd < 0) io_os_close(handle);
	return fd;
}

int permtomode(perm p) { return p.bits & 0777; }

int dopen(char *file, omode mod) {
	if (!file) {
		errmsg("dopen: bad path");
		return -1;
	}
	if (mod.x) {
		errmsg("dopen: exclusive mode requires create");
		return -1;
	}
	return fd_from_handle(io_os_open(file, mod), mod, "dopen: failed");
}

int dcreate(char *file, omode mod, perm pm) {
	if (!file) {
		errmsg("dcreate: bad path");
		return -1;
	}
	return fd_from_handle(io_os_create(file, mod, pm), mod, "dcreate: failed");
}

void dclose(int fd) {
	if (fd < 3 || !fd_table || fd >= fdcap || !fd_table [fd].used) return;
	io_os_close(fd_table [fd].handle);
	fd_table [fd].used = false;
}

vlong dread(int fd, void *buf, uvlong len) {
	if (!buf && len) {
		errmsg("dread: bad buffer");
		return -1;
	}
	if (len == 0) return 0;
	io_handle h = fd_get(fd);
	if (!io_handle_valid(h)) {
		errmsg("dread: bad fd");
		return -1;
	}
	vlong n = io_os_read(h, buf, len);
	if (n < 0) errmsg("dread: failed");
	return n;
}

uvlong dreadn(int fd, void *buf, uvlong len) {
	if (!buf && len) {
		errmsg("dreadn: bad buffer");
		return 0;
	}
	uvlong total = 0;
	uchar *p     = ( uchar * ) buf;
	while (total < len) {
		vlong n = dread(fd, p + total, len - total);
		if (n <= 0) break;
		total += ( uvlong ) n;
	}
	return total;
}

vlong dwrite(int fd, void *buf, uvlong len) {
	if (!buf && len) {
		errmsg("dwrite: bad buffer");
		return -1;
	}
	if (len == 0) return 0;
	omode mode;
	if (fd_get_mode(fd, &mode) && mode.a && dseek(fd, 0, 2) < 0) return -1;
	io_handle h = fd_get(fd);
	if (!io_handle_valid(h)) {
		errmsg("dwrite: bad fd");
		return -1;
	}
	vlong n = io_os_write(h, buf, len);
	if (n < 0) errmsg("dwrite: failed");
	return n;
}

vlong dseek(int fd, vlong amount, int whence) {
	if (whence < 0 || whence > 2) {
		errmsg("dseek: bad whence");
		return -1;
	}
	io_handle h = fd_get(fd);
	if (!io_handle_valid(h)) {
		errmsg("dseek: bad fd");
		return -1;
	}
	vlong off = io_os_seek(h, amount, whence);
	if (off < 0) errmsg("dseek: failed");
	return off;
}

static bool offset_fits_vlong(uvlong offset) {
	uvlong max = ((( uvlong ) 1 << (sizeof(vlong) * CHAR_BIT - 1)) - 1);
	return offset <= max;
}

static bool offset_in_range(uvlong offset, char *rangeerr) {
	if (!offset_fits_vlong(offset)) {
		errmsg(rangeerr);
		return false;
	}
	return true;
}

static bool restore_offset(int fd, vlong saved, char *who) {
	if (dseek(fd, saved, 0) >= 0) return true;
	if (!err()) errmsg(who);
	return false;
}

vlong pread(int fd, void *buf, uvlong len, uvlong offset) {
	if (!buf && len) {
		errmsg("pread: bad buffer");
		return -1;
	}
	if (len == 0) return 0;
	vlong saved = dseek(fd, 0, 1);
	if (saved < 0) {
		if (!err()) errmsg("pread: bad fd");
		return -1;
	}
	if (!offset_in_range(offset, "pread: offset out of range")) return -1;
	if (dseek(fd, ( vlong ) offset, 0) < 0) {
		restore_offset(fd, saved, "pread: failed to restore offset");
		return -1;
	}
	vlong n = dread(fd, buf, len);
	if (!restore_offset(fd, saved, "pread: failed to restore offset")) return -1;
	return n;
}

vlong pwrite(int fd, void *buf, uvlong len, uvlong offset) {
	if (!buf && len) {
		errmsg("pwrite: bad buffer");
		return -1;
	}
	if (len == 0) return 0;
	omode mode;
	if (fd_get_mode(fd, &mode) && mode.a) {
		errmsg("pwrite: append-only fd");
		return -1;
	}
	vlong saved = dseek(fd, 0, 1);
	if (saved < 0) {
		if (!err()) errmsg("pwrite: bad fd");
		return -1;
	}
	if (!offset_in_range(offset, "pwrite: offset out of range")) return -1;
	if (dseek(fd, ( vlong ) offset, 0) < 0) {
		restore_offset(fd, saved, "pwrite: failed to restore offset");
		return -1;
	}
	vlong n = dwrite(fd, buf, len);
	if (!restore_offset(fd, saved, "pwrite: failed to restore offset")) return -1;
	return n;
}

int dvprint(int fd, char *fm, va_list args) {
	int sd = vfmts(0, fm, args);
	if (sd < 0) return -1;
	slitr s = obslitr(sd);
	vlong n = 0;
	if (s.len > 0) n = dwrite(fd, s.s, s.len);
	rmstrand(sd);
	return n < 0 ? -1 : ( int ) n;
}

int dprint(int fd, char *fm, ...) {
	va_list args;
	va_start(args, fm);
	int n = dvprint(fd, fm, args);
	va_end(args);
	return n;
}
