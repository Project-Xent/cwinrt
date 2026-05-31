// Authoritative PIID reference: prints canonical guid_v for a curated set of
// generic instantiations, so cwinrt-gen --dump-piids can be diffed against it.
#include <cstdio>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.Globalization.h>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;

template <typename T>
static void dump(char const *name) {
    guid g = guid_of<T>();
    std::printf(
      "%s %08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X\n", name, g.Data1, g.Data2, g.Data3, g.Data4[0],
      g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]
    );
}

int main() {
    dump<IIterable<hstring>>("Windows.Foundation.Collections.IIterable`1<String>");
    dump<IIterable<IInspectable>>("Windows.Foundation.Collections.IIterable`1<Object>");
    dump<IVector<hstring>>("Windows.Foundation.Collections.IVector`1<String>");
    dump<IVectorView<hstring>>("Windows.Foundation.Collections.IVectorView`1<String>");
    dump<IMap<hstring, hstring>>("Windows.Foundation.Collections.IMap`2<String,String>");
    dump<IMapView<hstring, hstring>>("Windows.Foundation.Collections.IMapView`2<String,String>");
    dump<IKeyValuePair<hstring, IInspectable>>(
      "Windows.Foundation.Collections.IKeyValuePair`2<String,Object>");
    dump<IReference<int32_t>>("Windows.Foundation.IReference`1<Int32>");
    dump<IReference<bool>>("Windows.Foundation.IReference`1<Boolean>");
    dump<IAsyncOperation<bool>>("Windows.Foundation.IAsyncOperation`1<Boolean>");
    dump<AsyncOperationCompletedHandler<bool>>(
      "Windows.Foundation.AsyncOperationCompletedHandler`1<Boolean>");
    dump<EventHandler<IInspectable>>("Windows.Foundation.EventHandler`1<Object>");
    dump<TypedEventHandler<IInspectable, IInspectable>>(
      "Windows.Foundation.TypedEventHandler`2<Object,Object>");
    // nested generic: IVector of a generic interface
    dump<IVector<IKeyValuePair<hstring, IInspectable>>>(
      "Windows.Foundation.Collections.IVector`1<IKeyValuePair<String,Object>>");

    // struct type args (exercise sigbuild struct recursion)
    dump<IReference<DateTime>>("Windows.Foundation.IReference`1<DateTime>");
    dump<IReference<TimeSpan>>("Windows.Foundation.IReference`1<TimeSpan>");
    dump<IReference<Point>>("Windows.Foundation.IReference`1<Point>");
    dump<IReference<Rect>>("Windows.Foundation.IReference`1<Rect>");
    dump<IReference<winrt::Windows::UI::Color>>("Windows.Foundation.IReference`1<Color>");
    dump<IReference<guid>>("Windows.Foundation.IReference`1<Guid>");
    // enum type arg
    dump<IReference<winrt::Windows::Globalization::DayOfWeek>>(
      "Windows.Foundation.IReference`1<DayOfWeek>");
    // runtimeclass type arg (exercise rc(...) + default-interface)
    dump<IIterable<winrt::Windows::Storage::StorageFile>>(
      "Windows.Foundation.Collections.IIterable`1<StorageFile>");
    dump<IAsyncOperation<winrt::Windows::Storage::StorageFile>>(
      "Windows.Foundation.IAsyncOperation`1<StorageFile>");
    // nested generic interface as arg
    dump<IMapView<hstring, IVectorView<hstring>>>(
      "Windows.Foundation.Collections.IMapView`2<String,IVectorView<String>>");
    return 0;
}
