/*
 * IAsyncOperationWithProgress<T,P> await on real hardware, headless.
 * DataWriter.StoreAsync returns a WithProgress operation
 * whose put_Completed sits at vtable slot 8 (a put/get_Progress pair precedes
 * it), not slot 6. Awaiting it via cwinrt_async_wait_with_progress must register
 * the completion handler at slot 8 and return; the old slot-6 code would instead
 * hit put_Progress, never fire completion, and time out.
 *
 * Fully in-memory: InMemoryRandomAccessStream -> DataWriter -> WriteByte ->
 * StoreAsync -> await -> GetResults (bytes stored).
 */
#include <cwinrt/init.h>
#include <cwinrt/async.h>
#include <cwinrt/factory.h>
#include <cwinrt/Windows.Storage.Streams.h>
#include <stdio.h>

#define CHECK(hr, what)                                                  \
    do {                                                                 \
        HRESULT _hr = (hr);                                              \
        if (FAILED(_hr)) {                                               \
            printf("FAIL %s: hr=0x%08lX\n", (what), (unsigned long)_hr); \
            goto done;                                                   \
        }                                                                \
    } while (0)

int main(void)
{
    WSTST_InMemoryRandomAccessStream *stream  = NULL;
    WSTST_IOutputStream              *ostream = NULL;
    WSTST_IDataWriterFactory         *fac     = NULL;
    WSTST_DataWriter                 *writer  = NULL;
    WSTST_DataWriterStoreOperation   *op      = NULL;
    uint32_t                          stored  = 0;
    int                               rc      = 1;
    HRESULT                           hr;

    hr = cwinrt_init(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) {
        printf("FAIL cwinrt_init: 0x%08lX\n", (unsigned long)hr);
        return 1;
    }

    CHECK(wstst_in_memory_random_access_stream_new(&stream), "InMemoryRandomAccessStream activate");
    CHECK(wstst_in_memory_random_access_stream_get_output_stream_at(stream, 0, &ostream), "get_output_stream_at");
    CHECK(cwinrt_factory_get_statics(L"Windows.Storage.Streams.DataWriter", &CWINRT_IID_WSTST_IDataWriterFactory,
                                     (void **)&fac), "DataWriter factory");
    CHECK(wstst_data_writer_factory_create_data_writer(fac, ostream, &writer), "CreateDataWriter");
    CHECK(wstst_data_writer_write_byte(writer, (uint8_t)0xAB), "WriteByte");
    CHECK(wstst_data_writer_store_async(writer, &op), "StoreAsync -> WithProgress op");

    /* await a real IAsyncOperationWithProgress (Completed at slot 8). */
    CHECK(cwinrt_async_wait_with_progress((IUnknown *)op, INFINITE), "cwinrt_async_wait_with_progress");

    /* Slot-correct typed GetResults on the concrete operation runtimeclass. */
    CHECK(wstst_data_writer_store_operation_get_results(op, &stored), "GetResults");
    if (stored != 1) {
        printf("FAIL expected 1 byte stored, got %u\n", stored);
        goto done;
    }

    printf("PASS e2e_async_progress: WithProgress await (slot 8) + GetResults = %u byte\n", stored);
    rc = 0;

done:
    if (op) ((IUnknown *)op)->lpVtbl->Release((IUnknown *)op);
    if (writer) ((IUnknown *)writer)->lpVtbl->Release((IUnknown *)writer);
    if (fac) ((IUnknown *)fac)->lpVtbl->Release((IUnknown *)fac);
    if (ostream) ((IUnknown *)ostream)->lpVtbl->Release((IUnknown *)ostream);
    if (stream) ((IUnknown *)stream)->lpVtbl->Release((IUnknown *)stream);
    cwinrt_uninit();
    return rc;
}
