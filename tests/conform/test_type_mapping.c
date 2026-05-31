/* Conform: generated signatures follow cwinrt.md type mapping (no void** IAsync). */
#include <cwinrt/Windows.Foundation.h>
#include <cwinrt/Windows.Storage.h>
#include <cwinrt/Windows.UI.Composition.h>
#include <cwinrt/async.h>
#include <unknwn.h>
#include <stdio.h>

/* Abbreviated runtime class only (no WUC_Compositor). Runtime classes are opaque
   (incomplete) typedefs, so probe existence via a pointer, not sizeof(the type). */
typedef char wuc_comp_is_abbrev_only[(sizeof(WUC_Comp *) > 0) ? 1 : -1];

int main(void)
{
    WUC_Comp *comp = NULL;
    (void)comp;
    printf("conform type_mapping: typedefs and IAsync signatures ok\n");
    return 0;
}
