#pragma once
#define WIN32_LEAN_AND_MEAN
#include "err.h"
#include <windows.h>

static io_handle inline win_handle_from(HANDLE h) {
	return h == INVALID_HANDLE_VALUE || h == NULL ? io_handle_invalid() : ( io_handle ) ( uintptr_t ) h;
}

static inline HANDLE win_handle_to(io_handle h) { return ( HANDLE ) ( uintptr_t ) h; }

io_handle            io_os_stdin(void) { return win_handle_from(GetStdHandle(STD_INPUT_HANDLE)); }

io_handle            io_os_stdout(void) { return win_handle_from(GetStdHandle(STD_OUTPUT_HANDLE)); }

io_handle            io_os_stderr(void) { return win_handle_from(GetStdHandle(STD_ERROR_HANDLE)); }

static bool          owner_writable(perm pm) { return (pm.bits & 0200) != 0; }

static DWORD         win_access_of(omode mod, perm pm) {
	if (pm.isdir) return FILE_LIST_DIRECTORY;

	DWORD access = 0;
	if (mod.r) access |= GENERIC_READ;
	if (mod.w || mod.t) access |= GENERIC_WRITE;
	if (mod.a) access |= FILE_APPEND_DATA;
	if (mod.d) access |= DELETE;
	return access ? access : GENERIC_READ;
}

static DWORD win_creation_open(omode mod) {
	if (mod.t) return TRUNCATE_EXISTING;
	return OPEN_EXISTING;
}

static DWORD win_creation_create(omode mod) {
	if (mod.x) return CREATE_NEW;
	if (mod.t) return CREATE_ALWAYS;
	return OPEN_ALWAYS;
}

static DWORD win_flags_of(omode mod, perm pm) {
	DWORD flags = FILE_ATTRIBUTE_NORMAL;
	if (mod.d) flags |= FILE_FLAG_DELETE_ON_CLOSE;
	if (pm.isdir) flags |= FILE_FLAG_BACKUP_SEMANTICS;
	return flags;
}

static bool win_apply_readonly(char *path, perm pm) {
	DWORD attr = GetFileAttributesA(path);
	if (attr == INVALID_FILE_ATTRIBUTES) return false;
	if (owner_writable(pm)) attr &= ~FILE_ATTRIBUTE_READONLY;
	else attr |= FILE_ATTRIBUTE_READONLY;
	return SetFileAttributesA(path, attr) != 0;
}

static bool win_prepare_existing_file(char *path, perm pm) {
	DWORD attr = GetFileAttributesA(path);
	if (attr == INVALID_FILE_ATTRIBUTES) return true;
	if (attr & FILE_ATTRIBUTE_DIRECTORY) return true;
	if (!owner_writable(pm) || !(attr & FILE_ATTRIBUTE_READONLY)) return true;
	return SetFileAttributesA(path, attr & ~FILE_ATTRIBUTE_READONLY) != 0;
}

static bool win_dir_exists(char *path) {
	DWORD attr = GetFileAttributesA(path);
	return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool win_create_dir(char *path, omode mod) {
	if (win_dir_exists(path)) return !mod.x;
	return CreateDirectoryA(path, null) != 0;
}

io_handle io_os_open(char *path, omode mod) {
	HANDLE h
	  = CreateFileA(path, win_access_of(mod, ( perm ) {0}), FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	                null, win_creation_open(mod), win_flags_of(mod, ( perm ) {0}), null);
	return win_handle_from(h);
}

io_handle io_os_create(char *path, omode mod, perm pm) {
	if (pm.islnk) {
		errmsg("dcreate: symlink not supported");
		return io_handle_invalid();
	}
	if (pm.isdir && !win_create_dir(path, mod)) return io_handle_invalid();
	if (!pm.isdir && !mod.x && !win_prepare_existing_file(path, pm)) {
		errmsg("dcreate: permission mapping failed");
		return io_handle_invalid();
	}

	HANDLE h = CreateFileA(path, win_access_of(mod, pm), FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, null,
	                       pm.isdir ? OPEN_EXISTING : win_creation_create(mod), win_flags_of(mod, pm), null);
	if (h == INVALID_HANDLE_VALUE) return io_handle_invalid();
	if (!pm.isdir && !win_apply_readonly(path, pm)) {
		CloseHandle(h);
		errmsg("dcreate: permission mapping failed");
		return io_handle_invalid();
	}
	return win_handle_from(h);
}

void io_os_close(io_handle h) {
	if (io_handle_valid(h)) CloseHandle(win_handle_to(h));
}

vlong io_os_read(io_handle h, void *buf, uvlong len) {
	DWORD want = len > 0xffffffffull ? 0xfffffffful : ( DWORD ) len;
	DWORD got  = 0;
	if (!ReadFile(win_handle_to(h), buf, want, &got, null)) return -1;
	return ( vlong ) got;
}

vlong io_os_write(io_handle h, void *buf, uvlong len) {
	DWORD want = len > 0xffffffffull ? 0xfffffffful : ( DWORD ) len;
	DWORD put  = 0;
	if (!WriteFile(win_handle_to(h), buf, want, &put, null)) return -1;
	return ( vlong ) put;
}

vlong io_os_seek(io_handle h, vlong amount, int whence) {
	DWORD         method = whence == 0 ? FILE_BEGIN : whence == 1 ? FILE_CURRENT : FILE_END;
	LARGE_INTEGER move;
	LARGE_INTEGER pos;
	move.QuadPart = amount;
	if (!SetFilePointerEx(win_handle_to(h), move, &pos, method)) return -1;
	return ( vlong ) pos.QuadPart;
}
