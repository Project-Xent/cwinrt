#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "io.h"

typedef uintptr_t io_handle;

static io_handle inline io_handle_invalid(void) { return ( io_handle ) ( uintptr_t ) -1; }

static bool inline io_handle_valid(io_handle h) { return h != io_handle_invalid(); }

io_handle io_os_stdin(void);
io_handle io_os_stdout(void);
io_handle io_os_stderr(void);

io_handle io_os_open(char *path, omode mod);
io_handle io_os_create(char *path, omode mod, perm pm);

void      io_os_close(io_handle h);
vlong     io_os_read(io_handle h, void *buf, uvlong len);
vlong     io_os_write(io_handle h, void *buf, uvlong len);
vlong     io_os_seek(io_handle h, vlong amount, int whence);

#ifdef COETUA_IO_BACKEND_IMPLEMENTATION
  #if defined(_WIN32)
	#include "io_win32_backend.h"
  #else
	#error "Coetua raw I/O currently has only the Windows backend; add a direct OS backend for this platform."
  #endif
#endif
