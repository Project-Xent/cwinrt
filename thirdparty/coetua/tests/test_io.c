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

static char *path      = "test_io_temp.txt";
static char *xpath     = "test_io_exclusive.txt";
static char *delpath   = "test_io_delete.txt";
static char *dirpath   = "test_io_created_dir";
static char *slashdir  = "test_io_dir";
static char *slashpath = "test_io_dir/slash_path.txt";
static char *msg       = "Coetua I/O test\n";

static void  heading(char *name) { printf("\n=== I/O: %s ===\n", name); }

static void  check_expected_error(bool ok, char *label) {
	CHECK(ok && err(), label);
	errmsg(null);
}

static void cleanup_files(void) {
#if defined(_WIN32)
	DWORD attr;
	attr = GetFileAttributesA(path);
	if (attr != INVALID_FILE_ATTRIBUTES) SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
	attr = GetFileAttributesA(xpath);
	if (attr != INVALID_FILE_ATTRIBUTES) SetFileAttributesA(xpath, FILE_ATTRIBUTE_NORMAL);
	attr = GetFileAttributesA(delpath);
	if (attr != INVALID_FILE_ATTRIBUTES) SetFileAttributesA(delpath, FILE_ATTRIBUTE_NORMAL);
	attr = GetFileAttributesA(slashpath);
	if (attr != INVALID_FILE_ATTRIBUTES) SetFileAttributesA(slashpath, FILE_ATTRIBUTE_NORMAL);
#endif
	remove(path);
	remove(xpath);
	remove(delpath);
	remove(slashpath);
#if defined(_WIN32)
	RemoveDirectoryA(slashdir);
	RemoveDirectoryA(dirpath);
#endif
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

static void permission_mapping(void) {
	heading("permission mapping");
	CHECK(permtomode((perm) {.bits = 0644}) == 0644, "permtomode 0644");
	CHECK(permtomode((perm) {.bits = 0755}) == 0755, "permtomode 0755");
	CHECK(permtomode((perm) {.bits = 0600}) == 0600, "permtomode 0600");
	CHECK(permtomode((perm) {.bits = 0}) == 0, "permtomode 0000");
}

static void raw_permission_application(void) {
	heading("permission application");
#if defined(_WIN32)
	remove(path);
	int fd = dcreate(path, (omode) {.r = 1}, (perm) {.bits = 0444});
	CHECK(fd >= 0, "dcreate read-only file");
	if (fd >= 0) {
		vlong n = dwrite(fd, msg, strlen(msg));
		CHECK(n < 0, "read-only fd rejects write");
		CHECK(err(), "dcreate read-only sets error");
		errmsg(null);
		dclose(fd);
		DWORD attr = GetFileAttributesA(path);
		CHECK(attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY),
		      "dcreate read-only file sets readonly attribute");
		SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
	}

	remove(path);
	fd = dcreate(path, (omode) {.r = 1, .w = 1, .t = 1}, (perm) {.bits = 0444});
	CHECK(fd >= 0, "dcreate writable fd with read-only metadata");
	if (fd >= 0) {
		vlong n = dwrite(fd, msg, strlen(msg));
		CHECK(n == ( vlong ) strlen(msg), "omode controls current fd write access");
		dclose(fd);
		DWORD attr = GetFileAttributesA(path);
		CHECK(attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY),
		      "read-only metadata still applied after writable fd");
		SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
	}

	fd = dcreate(path, (omode) {.r = 1, .w = 1, .t = 1}, (perm) {.bits = 0644});
	CHECK(fd >= 0, "dcreate writable file over existing read-only file");
	if (fd >= 0) {
		vlong n = dwrite(fd, "fresh", 5);
		CHECK(n == 5, "rewritten former read-only file accepts write");
		dclose(fd);
		DWORD attr = GetFileAttributesA(path);
		CHECK(attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_READONLY),
		      "writable permission clears previous readonly attribute");
	}
#endif
}

static void raw_create_read_seek(void) {
	char  buf [128] = {0};
	omode wm        = {.r = 1, .w = 1, .t = 1};
	perm  pm        = {.bits = 0644};

	heading("create + write + read");
	int fd = dcreate(path, wm, pm);
	CHECK(fd >= 0, "dcreate");
	vlong n = dwrite(fd, msg, strlen(msg));
	CHECK(n == ( vlong ) strlen(msg), "dwrite");
	dclose(fd);

	fd = dopen(path, (omode) {.r = 1});
	CHECK(fd >= 0, "dopen for read");
	n = dread(fd, buf, sizeof(buf));
	CHECK(n == ( vlong ) strlen(msg) && strcmp(buf, msg) == 0, "dread matches");

	heading("seek");
	vlong pos = dseek(fd, 0, 1);
	CHECK(pos == ( vlong ) strlen(msg), "dseek tell after read");
	dseek(fd, 0, 0);
	memset(buf, 0, sizeof(buf));
	n = dread(fd, buf, sizeof(buf));
	CHECK(n == ( vlong ) strlen(msg) && strcmp(buf, msg) == 0, "dseek rewind + read");
	check_expected_error(dseek(fd, 0, 99) < 0, "dseek rejects invalid whence");
	dclose(fd);
}

static void raw_truncate_modes(void) {
	char buf [128];

	heading("truncate mode");
	int fd = dcreate(path, (omode) {.r = 1, .w = 1, .t = 1}, (perm) {.bits = 0644});
	dwrite(fd, "abcdef", 6);
	dclose(fd);
	fd = dopen(path, (omode) {.r = 1, .t = 1});
	CHECK(fd >= 0, "dopen truncate without explicit write");
	dclose(fd);
	fd = dopen(path, (omode) {.r = 1});
	memset(buf, 0, sizeof(buf));
	vlong n = dread(fd, buf, sizeof(buf));
	CHECK(n == 0, "truncate mode clears existing file");
	dclose(fd);

	rewrite_file(path, msg);
	fd = dcreate(path, (omode) {.r = 1, .t = 1, .a = 1}, (perm) {.bits = 0644});
	CHECK(fd >= 0, "dcreate truncate+append without explicit write");
	n = dwrite(fd, "tail", 4);
	CHECK(n == 4, "truncate+append write returns byte count");
	dclose(fd);
	check_path_text("tail", "truncate+append clears then appends");
	rewrite_file(path, msg);
	fd = dopen(path, (omode) {.r = 1, .t = 1, .a = 1});
	CHECK(fd >= 0, "dopen truncate+append without explicit write");
	n = dwrite(fd, "open", 4);
	CHECK(n == 4, "dopen truncate+append write returns byte count");
	dclose(fd);
	check_path_text("open", "dopen truncate+append clears then appends");
	rewrite_file(path, msg);
}

static void raw_exclusive_mode(void) {
	heading("exclusive mode");
	remove(xpath);
	int fd = dcreate(xpath, (omode) {.r = 1, .w = 1, .x = 1}, (perm) {.bits = 0644});
	CHECK(fd >= 0, "dcreate exclusive creates missing file");
	dclose(fd);

#if defined(_WIN32)
	SetFileAttributesA(xpath, FILE_ATTRIBUTE_READONLY);
#endif
	fd = dcreate(xpath, (omode) {.r = 1, .w = 1, .x = 1}, (perm) {.bits = 0644});
	check_expected_error(fd < 0, "dcreate exclusive rejects existing file");
#if defined(_WIN32)
	DWORD attr = GetFileAttributesA(xpath);
	CHECK(attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY),
	      "dcreate exclusive does not mutate existing readonly file");
	SetFileAttributesA(xpath, FILE_ATTRIBUTE_NORMAL);
#endif
	fd = dopen(xpath, (omode) {.r = 1, .x = 1});
	check_expected_error(fd < 0, "dopen rejects meaningless exclusive flag");
	remove(xpath);
}

static void raw_forward_slash_paths(void) {
	char buf [128];

	heading("forward-slash paths");
	remove(slashpath);
#if defined(_WIN32)
	CreateDirectoryA(slashdir, null);
#else
	( void ) slashdir;
#endif
	int fd = dcreate(slashpath, (omode) {.r = 1, .w = 1, .t = 1}, (perm) {.bits = 0644});
	CHECK(fd >= 0, "dcreate accepts forward-slash path");
	if (fd >= 0) {
		dwrite(fd, "slash", 5);
		dclose(fd);
		fd = dopen(slashpath, (omode) {.r = 1});
		memset(buf, 0, sizeof(buf));
		vlong n = dread(fd, buf, sizeof(buf));
		CHECK(n == 5 && strcmp(buf, "slash") == 0, "dopen reads forward-slash path");
		dclose(fd);
	}
}

static void raw_append_modes(void) {
	char buf [128];

	heading("append");
	int fd = dopen(path, (omode) {.r = 1, .w = 1, .a = 1});
	CHECK(fd >= 0, "dopen append");
	dwrite(fd, "appended\n", 9);
	dclose(fd);

	fd      = dopen(path, (omode) {.r = 1});
	vlong n = dread(fd, buf, sizeof(buf));
	CHECK(n > ( vlong ) strlen(msg) && strstr(buf, "appended") != null, "append visible");
	dclose(fd);

	rewrite_file(path, "base");
	fd = dopen(path, (omode) {.r = 1, .a = 1});
	CHECK(fd >= 0, "dopen append-only");
	dseek(fd, 0, 0);
	n = dwrite(fd, "+tail", 5);
	CHECK(n == 5, "append-only write returns byte count");
	dclose(fd);
	check_path_text("base+tail", "append-only ignores seek before write");

	rewrite_file(path, "base");
	fd = dopen(path, (omode) {.r = 1, .a = 1});
	CHECK(fd >= 0, "dopen append-only for pwrite reject");
	n = pwrite(fd, "X", 1, 0);
	check_expected_error(n < 0, "pwrite rejects append-only fd");
	dclose(fd);
	check_path_text("base", "append-only pwrite does not mutate file");

	rewrite_file(path, "base");
	fd = dopen(path, (omode) {.r = 1, .w = 1, .a = 1});
	CHECK(fd >= 0, "dopen write+append");
	dseek(fd, 0, 0);
	n = dwrite(fd, "+tail", 5);
	CHECK(n == 5, "write+append returns byte count");
	dclose(fd);
	check_path_text("base+tail", "write+append ignores seek before write");
	rewrite_file(path, msg);
}

static void raw_delete_on_close(void) {
#if defined(_WIN32)
	heading("delete-on-close");
	remove(delpath);
	int fd = dcreate(delpath, (omode) {.r = 1, .w = 1, .t = 1, .d = 1}, (perm) {.bits = 0644});
	CHECK(fd >= 0, "dcreate delete-on-close");
	if (fd >= 0) {
		dwrite(fd, "gone", 4);
		dclose(fd);
	}
	fd = dopen(delpath, (omode) {.r = 1});
	check_expected_error(fd < 0, "delete-on-close removes raw file");

#endif
}

static void raw_special_create_modes(void) {
#if defined(_WIN32)
	heading("special create modes");
	RemoveDirectoryA(dirpath);
	int fd = dcreate(dirpath, (omode) {.r = 1}, (perm) {.bits = 0755, .isdir = 1});
	CHECK(fd >= 0, "dcreate directory");
	if (fd >= 0) dclose(fd);
	DWORD attr = GetFileAttributesA(dirpath);
	CHECK(attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY),
	      "dcreate directory leaves directory on disk");
	RemoveDirectoryA(dirpath);
	fd = dcreate(dirpath, (omode) {.r = 1}, (perm) {.bits = 0555, .isdir = 1});
	CHECK(fd >= 0, "dcreate directory ignores file readonly mapping");
	if (fd >= 0) dclose(fd);
	attr = GetFileAttributesA(dirpath);
	CHECK(attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) && !(attr & FILE_ATTRIBUTE_READONLY),
	      "directory create does not apply read-only file attribute");
	fd = dcreate(dirpath, (omode) {.r = 1, .x = 1}, (perm) {.bits = 0755, .isdir = 1});
	check_expected_error(fd < 0, "dcreate exclusive directory rejects existing directory");
	fd = dcreate("test_io_link", (omode) {.r = 1}, (perm) {.bits = 0644, .islnk = 1});
	check_expected_error(fd < 0, "dcreate rejects symlink request");
	RemoveDirectoryA(dirpath);
#endif
}

static void raw_dreadn(void) {
	char buf [128];

	heading("dreadn");
	int fd = dopen(path, (omode) {.r = 1});
	dseek(fd, 0, 0);
	memset(buf, 0, sizeof(buf));
	uvlong n = dreadn(fd, buf, strlen(msg));
	CHECK(n == strlen(msg) && memcmp(buf, msg, strlen(msg)) == 0, "dreadn exact");
	dclose(fd);

	fd = dopen(path, (omode) {.r = 1});
	CHECK(fd >= 0, "dopen for dreadn null test");
	n = dreadn(fd, null, 1);
	check_expected_error(n == 0, "dreadn rejects null nonzero buffer");
	CHECK(dreadn(fd, null, 0) == 0 && !err(), "dreadn null zero length is harmless");
	dclose(fd);
}

static void raw_positional_io(void) {
	char buf [128];

	heading("positional I/O");
	int fd = dcreate(path, (omode) {.r = 1, .w = 1, .t = 1}, (perm) {.bits = 0644});
	dwrite(fd, "abcdef", 6);
	dseek(fd, 6, 0);
	memset(buf, 0, sizeof(buf));
	vlong n = pread(fd, buf, 2, 1);
	CHECK(n == 2 && memcmp(buf, "bc", 2) == 0, "pread reads at offset");
	vlong pos = dseek(fd, 0, 1);
	CHECK(pos == 6, "pread preserves current offset");
	n = pread(fd, null, 1, 1);
	check_expected_error(n < 0, "pread rejects null nonzero buffer");
	pos = dseek(fd, 0, 1);
	CHECK(pos == 6, "pread failure preserves current offset");
	CHECK(pread(fd, null, 0, 1) == 0 && !err(), "pread null zero length is harmless");
	n = pwrite(fd, "XY", 2, 2);
	CHECK(n == 2, "pwrite returns byte count");
	pos = dseek(fd, 0, 1);
	CHECK(pos == 6, "pwrite preserves current offset");
	n = pwrite(fd, null, 1, 1);
	check_expected_error(n < 0, "pwrite rejects null nonzero buffer");
	pos = dseek(fd, 0, 1);
	CHECK(pos == 6, "pwrite failure preserves current offset");
	CHECK(pwrite(fd, null, 0, 1) == 0 && !err(), "pwrite null zero length is harmless");
	n = pread(fd, buf, 1, ( uvlong ) 1 << 63);
	check_expected_error(n < 0, "pread rejects offset beyond vlong range");
	pos = dseek(fd, 0, 1);
	CHECK(pos == 6, "pread huge offset preserves current offset");
	n = pwrite(fd, "Z", 1, ( uvlong ) 1 << 63);
	check_expected_error(n < 0, "pwrite rejects offset beyond vlong range");
	pos = dseek(fd, 0, 1);
	CHECK(pos == 6, "pwrite huge offset preserves current offset");
	memset(buf, 0, sizeof(buf));
	dseek(fd, 0, 0);
	dread(fd, buf, 6);
	CHECK(memcmp(buf, "abXYef", 6) == 0, "pwrite overwrites at offset");
	dclose(fd);
	n = pread(fd, buf, 1, 0);
	check_expected_error(n < 0, "pread bad fd sets error");
	n = pwrite(fd, "Q", 1, 0);
	check_expected_error(n < 0, "pwrite bad fd sets error");
}

static void raw_dprint(void) {
	char buf [128];

	heading("dprint");
	int fd = dcreate(path, (omode) {.r = 1, .w = 1, .t = 1}, (perm) {.bits = 0644});
	dprint(fd, "formatted: %s = %d", "answer", 42);
	dclose(fd);

	fd = dopen(path, (omode) {.r = 1});
	memset(buf, 0, sizeof(buf));
	dread(fd, buf, sizeof(buf));
	CHECK(strcmp(buf, "formatted: answer = 42") == 0, "dprint formatted");
	dclose(fd);
}

static void raw_errors(void) {
	heading("errors");
	int fd = dopen("/nonexistent_xyz_file_12345", (omode) {.r = 1});
	check_expected_error(fd < 0, "dopen nonexistent sets error");
	fd = dopen(null, (omode) {.r = 1});
	check_expected_error(fd < 0, "dopen rejects null path");
	fd = dcreate(null, (omode) {.r = 1, .w = 1, .t = 1}, (perm) {.bits = 0644});
	check_expected_error(fd < 0, "dcreate rejects null path");
	vlong n = dwrite(-1, "x", 1);
	check_expected_error(n < 0, "dwrite bad fd sets error");

	fd = dcreate(path, (omode) {.r = 1, .w = 1, .t = 1}, (perm) {.bits = 0644});
	CHECK(fd >= 0, "dcreate for raw buffer edge cases");
	CHECK(dwrite(fd, null, 0) == 0 && !err(), "dwrite null zero length is harmless");
	n = dwrite(fd, null, 1);
	check_expected_error(n < 0, "dwrite rejects null nonzero buffer");
	dclose(fd);

	fd = dopen(path, (omode) {.r = 1});
	CHECK(fd >= 0, "dopen for raw buffer edge cases");
	CHECK(dread(fd, null, 0) == 0 && !err(), "dread null zero length is harmless");
	n = dread(fd, null, 1);
	check_expected_error(n < 0, "dread rejects null nonzero buffer");
	dclose(fd);
}

int main(void) {
	cleanup_files();

	permission_mapping();
	raw_create_read_seek();
	raw_permission_application();
	raw_truncate_modes();
	raw_exclusive_mode();
	raw_forward_slash_paths();
	raw_append_modes();
	raw_delete_on_close();
	raw_special_create_modes();
	raw_dreadn();
	raw_positional_io();
	raw_dprint();
	raw_errors();

	printf("\n=== result: %d failures ===\n", failures);
	cleanup_files();
	return failures;
}
