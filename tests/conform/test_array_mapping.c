/* Conform: WinRT arrays project to (T *data, uint32_t len) in C signatures. */
#include <cwinrt/Windows.Foundation.Collections.h>
#include <stdio.h>

/* IVectorView`1.GetMany(startIndex, items) */
HRESULT wfoco_vector_view_1_get_many(
    WFOCO_IVectorView_1 *self,
    uint32_t startIndex,
    cwinrt_hstring *items,
    uint32_t items_len,
    uint32_t *out);

int main(void)
{
    (void)wfoco_vector_view_1_get_many;
    printf("conform array_mapping: array params use data + len\n");
    return 0;
}
