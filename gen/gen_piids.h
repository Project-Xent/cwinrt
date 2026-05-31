#pragma once

/* Diagnostic / emission subcommands for parameterized interface IDs (PIIDs) and
   generic-instantiation signatures. Each returns a process exit code. */

int gen_selftest_piid2(char const *winmd_path);
int gen_dump_piids(char const *winmd_path);
int gen_emit_piids(char const *winmd_path, char const *out_dir);
int gen_selftest_piid_rfc(void);
int gen_dump_sig(char const *winmd_path, char const *type_name);
