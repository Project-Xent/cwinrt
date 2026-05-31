#pragma once

/* ═══════════════════════════════════════════════════════
   Coetua configuration
  Shared descriptor seed sizes and coarse growth constants.
  Override from the build with -DCOETUA_*=n as needed.
   ═══════════════════════════════════════════════════════ */

#ifndef COETUA_ARENA_BLOCK_SIZE
  #define COETUA_ARENA_BLOCK_SIZE (( uvlong ) 1 << 20)
#endif

#ifndef COETUA_ARENA_TABLE_SEED
  #define COETUA_ARENA_TABLE_SEED 64
#endif

#ifndef COETUA_SILO_TABLE_SEED
  #define COETUA_SILO_TABLE_SEED 256
#endif

#ifndef COETUA_SILO_CHUNK
  #define COETUA_SILO_CHUNK 64
#endif

#ifndef COETUA_ITER_TABLE_SEED
  #define COETUA_ITER_TABLE_SEED 128
#endif

#ifndef COETUA_STRAND_TABLE_SEED
  #define COETUA_STRAND_TABLE_SEED 256
#endif

#ifndef COETUA_STRAND_CHUNK
  #define COETUA_STRAND_CHUNK 64
#endif

#ifndef COETUA_FD_TABLE_SEED
  #define COETUA_FD_TABLE_SEED 256
#endif

#ifndef COETUA_BIO_TABLE_SEED
  #define COETUA_BIO_TABLE_SEED 128
#endif

#ifndef COETUA_BIO_BUFSZ
  #define COETUA_BIO_BUFSZ 8192
#endif

#ifndef COETUA_VERB_TABLE_SEED
  #define COETUA_VERB_TABLE_SEED 128
#endif

#ifndef COETUA_DAG_TABLE_SEED
  #define COETUA_DAG_TABLE_SEED 64
#endif

#ifndef COETUA_UGRAPH_TABLE_SEED
  #define COETUA_UGRAPH_TABLE_SEED 64
#endif

#ifndef COETUA_MTREE_TABLE_SEED
  #define COETUA_MTREE_TABLE_SEED 64
#endif

#ifndef COETUA_LATTICE_TABLE_SEED
  #define COETUA_LATTICE_TABLE_SEED 64
#endif

#ifndef COETUA_SKIPLIST_TABLE_SEED
  #define COETUA_SKIPLIST_TABLE_SEED 64
#endif

#ifndef COETUA_INDELTREE_TABLE_SEED
  #define COETUA_INDELTREE_TABLE_SEED 64
#endif

#ifndef COETUA_RETRXTREE_TABLE_SEED
  #define COETUA_RETRXTREE_TABLE_SEED 64
#endif

#ifndef COETUA_AMTRIE_TABLE_SEED
  #define COETUA_AMTRIE_TABLE_SEED 64
#endif

#ifndef COETUA_SPARSPLANE_TABLE_SEED
  #define COETUA_SPARSPLANE_TABLE_SEED 64
#endif

#ifndef COETUA_XPEDT_TABLE_SEED
  #define COETUA_XPEDT_TABLE_SEED 64
#endif
