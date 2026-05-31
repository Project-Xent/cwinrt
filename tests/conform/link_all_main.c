/* link_all: a stub main linked against every namespace's impl
   translation unit. Because the impl objects are linked directly (not pulled
   from an archive on demand), the linker sees all 342 TUs at once and surfaces
   duplicate/conflicting external symbols (LNK2005) and unresolved references
   that the per-namespace compile gate cannot. */
int main(void) { return 0; }
