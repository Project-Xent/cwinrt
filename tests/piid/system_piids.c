#include <stdio.h>
#include <windows.h>
#include <wchar.h>

typedef struct metadata_builder metadata_builder;
typedef struct metadata_locator metadata_locator;

typedef struct metadata_builder_vtbl {
	HRESULT(STDMETHODCALLTYPE *set_interface)(metadata_builder *self, GUID iid);
	HRESULT(STDMETHODCALLTYPE *set_delegate)(metadata_builder *self, GUID iid);
	HRESULT(STDMETHODCALLTYPE *set_iface_group)(metadata_builder *self, PCWSTR name, PCWSTR def_name, GUID const *def_iid);
	HRESULT(STDMETHODCALLTYPE *set_param_iface_group)(
	  metadata_builder *self, PCWSTR name, UINT32 count, PCWSTR const *elements);
	HRESULT(STDMETHODCALLTYPE *set_runtime_class)(metadata_builder *self, PCWSTR name, PCWSTR def_name, GUID const *def_iid);
	HRESULT(STDMETHODCALLTYPE *set_param_runtime_class)(
	  metadata_builder *self, PCWSTR name, UINT32 count, PCWSTR const *elements);
	HRESULT(STDMETHODCALLTYPE *set_struct)(metadata_builder *self, PCWSTR name, UINT32 count, PCWSTR const *fields);
	HRESULT(STDMETHODCALLTYPE *set_enum)(metadata_builder *self, PCWSTR name, PCWSTR base);
	HRESULT(STDMETHODCALLTYPE *set_param_interface)(metadata_builder *self, GUID iid, UINT32 count);
	HRESULT(STDMETHODCALLTYPE *set_param_delegate)(metadata_builder *self, GUID iid, UINT32 count);
} metadata_builder_vtbl;

struct metadata_builder {
	metadata_builder_vtbl const *vtbl;
};

typedef struct metadata_locator_vtbl {
	HRESULT(STDMETHODCALLTYPE *locate)(metadata_locator const *self, PCWSTR name, metadata_builder *builder);
} metadata_locator_vtbl;

struct metadata_locator {
	metadata_locator_vtbl const *vtbl;
};

/*
 * roparameterizediid.h exposes these two callback objects only as abstract
 * interfaces. Their ABI is a single vtable pointer; this declaration lets the
 * C oracle call the system PIID implementation without reproducing its algorithm.
 */
HRESULT WINAPI RoGetParameterizedTypeInstanceIID(
  UINT32 count, PCWSTR *elements, metadata_locator const *locator, GUID *iid, void **extra);

typedef enum metadata_kind {
	META_PARAM_INTERFACE,
	META_PARAM_DELEGATE,
	META_STRUCT,
	META_ENUM,
	META_RUNTIME_CLASS
} metadata_kind;

typedef struct metadata_type {
	PCWSTR        name;
	metadata_kind kind;
	GUID          iid;
	UINT32        count;
	PCWSTR const *parts;
} metadata_type;

#define GUID_VALUE(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7)                                                        \
	{(a), (b), (c), {(d0), (d1), (d2), (d3), (d4), (d5), (d6), (d7)}}

static PCWSTR const fields_i64 [] = {L"Int64"};
static PCWSTR const fields_point [] = {L"Single", L"Single"};
static PCWSTR const fields_rect [] = {L"Single", L"Single", L"Single", L"Single"};
static PCWSTR const fields_color [] = {L"UInt8", L"UInt8", L"UInt8", L"UInt8"};
static PCWSTR const enum_i32 [] = {L"Int32"};
static PCWSTR const storage_file [] = {L"Windows.Storage.IStorageFile"};

static metadata_type const metadata [] = {
  {L"Windows.Foundation.Collections.IIterable`1", META_PARAM_INTERFACE,
   GUID_VALUE(0xFAA585EA, 0x6214, 0x4217, 0xAF, 0xDA, 0x7F, 0x46, 0xDE, 0x58, 0x69, 0xB3), 1, NULL},
  {L"Windows.Foundation.Collections.IVector`1", META_PARAM_INTERFACE,
   GUID_VALUE(0x913337E9, 0x11A1, 0x4345, 0xA3, 0xA2, 0x4E, 0x7F, 0x95, 0x6E, 0x22, 0x2D), 1, NULL},
  {L"Windows.Foundation.Collections.IVectorView`1", META_PARAM_INTERFACE,
   GUID_VALUE(0xBBE1FA4C, 0xB0E3, 0x4583, 0xBA, 0xEF, 0x1F, 0x1B, 0x2E, 0x48, 0x3E, 0x56), 1, NULL},
  {L"Windows.Foundation.Collections.IMap`2", META_PARAM_INTERFACE,
   GUID_VALUE(0x3C2925FE, 0x8519, 0x45C1, 0xAA, 0x79, 0x19, 0x7B, 0x67, 0x18, 0xC1, 0xC1), 2, NULL},
  {L"Windows.Foundation.Collections.IMapView`2", META_PARAM_INTERFACE,
   GUID_VALUE(0xE480CE40, 0xA338, 0x4ADA, 0xAD, 0xCF, 0x27, 0x22, 0x72, 0xE4, 0x8C, 0xB9), 2, NULL},
  {L"Windows.Foundation.Collections.IKeyValuePair`2", META_PARAM_INTERFACE,
   GUID_VALUE(0x02B51929, 0xC1C4, 0x4A7E, 0x89, 0x40, 0x03, 0x12, 0xB5, 0xC1, 0x85, 0x00), 2, NULL},
  {L"Windows.Foundation.IReference`1", META_PARAM_INTERFACE,
   GUID_VALUE(0x61C17706, 0x2D65, 0x11E0, 0x9A, 0xE8, 0xD4, 0x85, 0x64, 0x01, 0x54, 0x72), 1, NULL},
  {L"Windows.Foundation.IAsyncOperation`1", META_PARAM_INTERFACE,
   GUID_VALUE(0x9FC2B0BB, 0xE446, 0x44E2, 0xAA, 0x61, 0x9C, 0xAB, 0x8F, 0x63, 0x6A, 0xF2), 1, NULL},
  {L"Windows.Foundation.AsyncOperationCompletedHandler`1", META_PARAM_DELEGATE,
   GUID_VALUE(0xFCDCF02C, 0xE5D8, 0x4478, 0x91, 0x5A, 0x4D, 0x90, 0xB7, 0x4B, 0x83, 0xA5), 1, NULL},
  {L"Windows.Foundation.EventHandler`1", META_PARAM_DELEGATE,
   GUID_VALUE(0x9DE1C535, 0x6AE1, 0x11E0, 0x84, 0xE1, 0x18, 0xA9, 0x05, 0xBC, 0xC5, 0x3F), 1, NULL},
  {L"Windows.Foundation.TypedEventHandler`2", META_PARAM_DELEGATE,
   GUID_VALUE(0x9DE1C534, 0x6AE1, 0x11E0, 0x84, 0xE1, 0x18, 0xA9, 0x05, 0xBC, 0xC5, 0x3F), 2, NULL},
  {L"Windows.Foundation.DateTime", META_STRUCT, {0}, 1, fields_i64},
  {L"Windows.Foundation.TimeSpan", META_STRUCT, {0}, 1, fields_i64},
  {L"Windows.Foundation.Point", META_STRUCT, {0}, 2, fields_point},
  {L"Windows.Foundation.Rect", META_STRUCT, {0}, 4, fields_rect},
  {L"Windows.UI.Color", META_STRUCT, {0}, 4, fields_color},
  {L"Windows.Globalization.DayOfWeek", META_ENUM, {0}, 1, enum_i32},
  {L"Windows.Storage.StorageFile", META_RUNTIME_CLASS,
   GUID_VALUE(0xFA3F6186, 0x4214, 0x428C, 0xA6, 0x4C, 0x14, 0xC9, 0xAC, 0x73, 0x15, 0xEA), 1, storage_file},
};

static HRESULT metadata_apply(metadata_type const *type, metadata_builder *builder) {
	switch (type->kind) {
	case META_PARAM_INTERFACE: return builder->vtbl->set_param_interface(builder, type->iid, type->count);
	case META_PARAM_DELEGATE: return builder->vtbl->set_param_delegate(builder, type->iid, type->count);
	case META_STRUCT: return builder->vtbl->set_struct(builder, type->name, type->count, type->parts);
	case META_ENUM: return builder->vtbl->set_enum(builder, type->name, type->parts [0]);
	case META_RUNTIME_CLASS:
		return builder->vtbl->set_runtime_class(builder, type->name, type->parts [0], &type->iid);
	}
	return E_UNEXPECTED;
}

static HRESULT STDMETHODCALLTYPE metadata_locate(
  metadata_locator const *self, PCWSTR name, metadata_builder *builder) {
	size_t i;
	(void)self;
	for (i = 0; i < sizeof(metadata) / sizeof(metadata [0]); i++)
		if (wcscmp(name, metadata [i].name) == 0) return metadata_apply(&metadata [i], builder);
	fwprintf(stderr, L"system PIID oracle: missing metadata for %ls\n", name);
	return E_NOTIMPL;
}

static metadata_locator_vtbl const locator_vtbl = {metadata_locate};
static metadata_locator const      locator = {&locator_vtbl};

typedef struct piid_case {
	char const   *name;
	UINT32        count;
	PCWSTR const *elements;
} piid_case;

#define TYPE1(name, a) static PCWSTR const name [] = {a}
#define TYPE2(name, a, b) static PCWSTR const name [] = {a, b}
#define TYPE3(name, a, b, c) static PCWSTR const name [] = {a, b, c}
#define TYPE4(name, a, b, c, d) static PCWSTR const name [] = {a, b, c, d}

TYPE2(iter_string, L"Windows.Foundation.Collections.IIterable`1", L"String");
TYPE2(iter_object, L"Windows.Foundation.Collections.IIterable`1", L"Object");
TYPE2(vector_string, L"Windows.Foundation.Collections.IVector`1", L"String");
TYPE2(view_string, L"Windows.Foundation.Collections.IVectorView`1", L"String");
TYPE3(map_string, L"Windows.Foundation.Collections.IMap`2", L"String", L"String");
TYPE3(map_view_string, L"Windows.Foundation.Collections.IMapView`2", L"String", L"String");
TYPE3(pair_object, L"Windows.Foundation.Collections.IKeyValuePair`2", L"String", L"Object");
TYPE2(ref_i32, L"Windows.Foundation.IReference`1", L"Int32");
TYPE2(ref_bool, L"Windows.Foundation.IReference`1", L"Boolean");
TYPE2(async_bool, L"Windows.Foundation.IAsyncOperation`1", L"Boolean");
TYPE2(async_handler_bool, L"Windows.Foundation.AsyncOperationCompletedHandler`1", L"Boolean");
TYPE2(event_object, L"Windows.Foundation.EventHandler`1", L"Object");
TYPE3(typed_event_object, L"Windows.Foundation.TypedEventHandler`2", L"Object", L"Object");
TYPE4(vector_pair, L"Windows.Foundation.Collections.IVector`1", L"Windows.Foundation.Collections.IKeyValuePair`2",
      L"String", L"Object");
TYPE2(ref_datetime, L"Windows.Foundation.IReference`1", L"Windows.Foundation.DateTime");
TYPE2(ref_timespan, L"Windows.Foundation.IReference`1", L"Windows.Foundation.TimeSpan");
TYPE2(ref_point, L"Windows.Foundation.IReference`1", L"Windows.Foundation.Point");
TYPE2(ref_rect, L"Windows.Foundation.IReference`1", L"Windows.Foundation.Rect");
TYPE2(ref_color, L"Windows.Foundation.IReference`1", L"Windows.UI.Color");
TYPE2(ref_guid, L"Windows.Foundation.IReference`1", L"Guid");
TYPE2(ref_day, L"Windows.Foundation.IReference`1", L"Windows.Globalization.DayOfWeek");
TYPE2(iter_file, L"Windows.Foundation.Collections.IIterable`1", L"Windows.Storage.StorageFile");
TYPE2(async_file, L"Windows.Foundation.IAsyncOperation`1", L"Windows.Storage.StorageFile");
TYPE4(map_view_vector, L"Windows.Foundation.Collections.IMapView`2", L"String",
      L"Windows.Foundation.Collections.IVectorView`1", L"String");

#define CASE(name, elements) {(name), sizeof(elements) / sizeof(elements [0]), (elements)}

static piid_case const cases [] = {
  CASE("Windows.Foundation.Collections.IIterable`1<String>", iter_string),
  CASE("Windows.Foundation.Collections.IIterable`1<Object>", iter_object),
  CASE("Windows.Foundation.Collections.IVector`1<String>", vector_string),
  CASE("Windows.Foundation.Collections.IVectorView`1<String>", view_string),
  CASE("Windows.Foundation.Collections.IMap`2<String,String>", map_string),
  CASE("Windows.Foundation.Collections.IMapView`2<String,String>", map_view_string),
  CASE("Windows.Foundation.Collections.IKeyValuePair`2<String,Object>", pair_object),
  CASE("Windows.Foundation.IReference`1<Int32>", ref_i32),
  CASE("Windows.Foundation.IReference`1<Boolean>", ref_bool),
  CASE("Windows.Foundation.IAsyncOperation`1<Boolean>", async_bool),
  CASE("Windows.Foundation.AsyncOperationCompletedHandler`1<Boolean>", async_handler_bool),
  CASE("Windows.Foundation.EventHandler`1<Object>", event_object),
  CASE("Windows.Foundation.TypedEventHandler`2<Object,Object>", typed_event_object),
  CASE("Windows.Foundation.Collections.IVector`1<IKeyValuePair<String,Object>>", vector_pair),
  CASE("Windows.Foundation.IReference`1<DateTime>", ref_datetime),
  CASE("Windows.Foundation.IReference`1<TimeSpan>", ref_timespan),
  CASE("Windows.Foundation.IReference`1<Point>", ref_point),
  CASE("Windows.Foundation.IReference`1<Rect>", ref_rect),
  CASE("Windows.Foundation.IReference`1<Color>", ref_color),
  CASE("Windows.Foundation.IReference`1<Guid>", ref_guid),
  CASE("Windows.Foundation.IReference`1<DayOfWeek>", ref_day),
  CASE("Windows.Foundation.Collections.IIterable`1<StorageFile>", iter_file),
  CASE("Windows.Foundation.IAsyncOperation`1<StorageFile>", async_file),
  CASE("Windows.Foundation.Collections.IMapView`2<String,IVectorView<String>>", map_view_vector),
};

static int dump_case(piid_case const *test) {
	GUID    iid;
	HRESULT hr = RoGetParameterizedTypeInstanceIID(
	  test->count, ( PCWSTR * ) test->elements, &locator, &iid, NULL);
	if (FAILED(hr)) {
		fprintf(stderr, "system PIID oracle: %s failed 0x%08lX\n", test->name, ( unsigned long ) hr);
		return 1;
	}
	printf("%s %08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X\n", test->name, ( unsigned long ) iid.Data1,
	       iid.Data2, iid.Data3, iid.Data4 [0], iid.Data4 [1], iid.Data4 [2], iid.Data4 [3], iid.Data4 [4],
	       iid.Data4 [5], iid.Data4 [6], iid.Data4 [7]);
	return 0;
}

int main(void) {
	size_t i;
	int    failed = 0;
	for (i = 0; i < sizeof(cases) / sizeof(cases [0]); i++) failed |= dump_case(&cases [i]);
	return failed;
}
