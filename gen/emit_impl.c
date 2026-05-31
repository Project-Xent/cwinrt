#include "emit_impl.h"

#include "arena.h"
#include "bio.h"
#include "dispatch_util.h"
#include "err.h"
#include "fmt.h"
#include "io.h"
#include "name.h"
#include <stdio.h>
#include <string.h>

static int emit_write_str(int bd, char const *s) {
	vlong n;
	if (!s) return 0;
	n = bwrite(bd, ( void * ) s, strlen(s));
	return n < 0 ? -1 : 0;
}

static int emit_static_class_method(int bd, cwinrt_mapped_method const *m) {
	char        call_args [512];
	char        iid_sym [128];
	char        factory_iid_arg [160];
	char const *obj_kw;
	char const *factory_fn;

	if (!m->static_class_winrt || !m->delegate_c_name || !m->dispatch_iface_c) return 0;
	if (cwinrt_params_to_call_args(m->params_c, call_args, sizeof(call_args)) != 0) return 0;

	factory_iid_arg [0] = '\0';
	if (m->dispatch_has_iid) {
		cwinrt_name_iid_symbol(m->dispatch_iface_c, iid_sym, sizeof(iid_sym));
		snprintf(factory_iid_arg, sizeof(factory_iid_arg), "&%s", iid_sym);
	}
	else snprintf(factory_iid_arg, sizeof(factory_iid_arg), "NULL");

	if (cwinrt_dispatch_iface_is_statics_facade(m->dispatch_iface_c)) {
		obj_kw     = "statics";
		factory_fn = "cwinrt_factory_get_statics";
	}
	else {
		obj_kw     = "inst";
		factory_fn = "cwinrt_factory_activate";
	}

	return bprint(
               bd,
               "%s\n"
               "{\n"
               "    %s *%s = NULL;\n"
               "    HRESULT hr = %s(L\"%s\", %s, (void **)&%s);\n"
               "    if (FAILED(hr)) return hr;\n"
               "    hr = %s(%s%s%s);\n"
               "    ((IUnknown *)%s)->lpVtbl->Release((IUnknown *)%s);\n"
               "    return hr;\n"
               "}\n\n",
               m->c_sig,
               m->dispatch_iface_c,
               obj_kw,
               factory_fn,
               m->static_class_winrt,
               factory_iid_arg,
               obj_kw,
               m->delegate_c_name,
               obj_kw,
               call_args[0] ? ", " : "",
               call_args,
               obj_kw,
               obj_kw)
                   < 0
               ? -1
               : 0;
}

static int emit_static_activate_vtable(int bd, cwinrt_mapped_method const *m) {
	char call_args [512];
	char fn_params [768];
	char iid_sym [128];
	char factory_iid_arg [160];

	if (!m->static_class_winrt || !m->dispatch_iface_c || !m->vtable_slot) return 0;
	if (cwinrt_dispatch_iface_is_statics_facade(m->dispatch_iface_c)) return 0;
	if (cwinrt_params_to_call_args(m->params_c, call_args, sizeof(call_args)) != 0) return 0;

	if (m->params_c && m->params_c [0])
		snprintf(fn_params, sizeof(fn_params), "%s *self, %s", m->dispatch_iface_c, m->params_c);
	else snprintf(fn_params, sizeof(fn_params), "%s *self", m->dispatch_iface_c);

	factory_iid_arg [0] = '\0';
	if (m->dispatch_has_iid) {
		cwinrt_name_iid_symbol(m->dispatch_iface_c, iid_sym, sizeof(iid_sym));
		snprintf(factory_iid_arg, sizeof(factory_iid_arg), "&%s", iid_sym);
	}
	else snprintf(factory_iid_arg, sizeof(factory_iid_arg), "NULL");

	return bprint(
               bd,
               "%s\n"
               "{\n"
               "    %s *inst = NULL;\n"
               "    HRESULT hr = cwinrt_factory_activate(L\"%s\", %s, (void **)&inst);\n"
               "    if (FAILED(hr)) return hr;\n"
               "    {\n"
               "        typedef HRESULT (__stdcall *cwinrt_inst_fn_t)(%s);\n"
               "        cwinrt_inst_fn_t fn = (cwinrt_inst_fn_t)((void **)((IUnknown *)inst)->lpVtbl)[%ud];\n"
               "        hr = fn(inst%s%s);\n"
               "    }\n"
               "    ((IUnknown *)inst)->lpVtbl->Release((IUnknown *)inst);\n"
               "    return hr;\n"
               "}\n\n",
               m->c_sig,
               m->dispatch_iface_c,
               m->static_class_winrt,
               factory_iid_arg,
               fn_params,
               m->vtable_slot,
               call_args[0] ? ", " : "",
               call_args)
                   < 0
               ? -1
               : 0;
}

static int emit_static_vtable_direct(int bd, cwinrt_mapped_method const *m) {
	char call_args [512];
	char fn_params [768];
	char iid_sym [128];
	char factory_iid_arg [160];

	if (!m->static_class_winrt || !m->dispatch_iface_c || !m->vtable_slot) return 0;
	if (!cwinrt_dispatch_iface_is_statics_facade(m->dispatch_iface_c)) return 0;
	if (cwinrt_params_to_call_args(m->params_c, call_args, sizeof(call_args)) != 0) return 0;

	if (m->params_c && m->params_c [0])
		snprintf(fn_params, sizeof(fn_params), "%s *self, %s", m->dispatch_iface_c, m->params_c);
	else snprintf(fn_params, sizeof(fn_params), "%s *self", m->dispatch_iface_c);

	factory_iid_arg [0] = '\0';
	if (m->dispatch_has_iid) {
		cwinrt_name_iid_symbol(m->dispatch_iface_c, iid_sym, sizeof(iid_sym));
		snprintf(factory_iid_arg, sizeof(factory_iid_arg), "&%s", iid_sym);
	}
	else snprintf(factory_iid_arg, sizeof(factory_iid_arg), "NULL");

	return bprint(
               bd,
               "%s\n"
               "{\n"
               "    %s *statics = NULL;\n"
               "    HRESULT hr = cwinrt_factory_get_statics(L\"%s\", %s, (void **)&statics);\n"
               "    if (FAILED(hr)) return hr;\n"
               "    {\n"
               "        typedef HRESULT (__stdcall *cwinrt_static_fn_t)(%s);\n"
               "        cwinrt_static_fn_t fn = (cwinrt_static_fn_t)((void **)((IUnknown *)statics)->lpVtbl)[%ud];\n"
               "        hr = fn(statics%s%s);\n"
               "    }\n"
               "    ((IUnknown *)statics)->lpVtbl->Release((IUnknown *)statics);\n"
               "    return hr;\n"
               "}\n\n",
               m->c_sig,
               m->dispatch_iface_c,
               m->static_class_winrt,
               factory_iid_arg,
               fn_params,
               m->vtable_slot,
               call_args[0] ? ", " : "",
               call_args)
                   < 0
               ? -1
               : 0;
}

static int emit_instance_vtable(int bd, cwinrt_mapped_method const *m) {
	char call_args [512];

	if (!m->vtable_slot || !m->params_c) return 0;
	if (cwinrt_params_to_call_args(m->params_c, call_args, sizeof(call_args)) != 0) return 0;

	/* A class convenience method whose declaring interface is NOT the class's
	 * default interface must QI to that interface before dispatching: the vtable
	 * slot is relative to the declaring interface, so calling it on the default
	 * interface pointer lands on a different method. dispatch_iface_c is set only
	 * in that case; default-interface methods take the direct path below. */
	if (m->dispatch_iface_c && m->dispatch_has_iid) {
		char const *rest = strchr(call_args, ','); /* ", a1, ..." past `self`, or NULL */
		if (!rest) rest = "";
		return bprint(
		  bd,
		  "%s\n"
		  "{\n"
		  "    %s *cwinrt_disp_ = NULL;\n"
		  "    HRESULT cwinrt_hr_ = ((IUnknown *)self)->lpVtbl->QueryInterface((IUnknown *)self, &CWINRT_IID_%s, "
		  "(void **)&cwinrt_disp_);\n"
		  "    if (FAILED(cwinrt_hr_)) return cwinrt_hr_;\n"
		  "    {\n"
		  "        typedef HRESULT (__stdcall *cwinrt_fn_t)(%s);\n"
		  "        cwinrt_fn_t fn = (cwinrt_fn_t)((void **)((IUnknown *)cwinrt_disp_)->lpVtbl)[%ud];\n"
		  "        cwinrt_hr_ = fn((void *)cwinrt_disp_%s);\n"
		  "    }\n"
		  "    ((IUnknown *)cwinrt_disp_)->lpVtbl->Release((IUnknown *)cwinrt_disp_);\n"
		  "    return cwinrt_hr_;\n"
		  "}\n\n",
		  m->c_sig, m->dispatch_iface_c, m->dispatch_iface_c, m->params_c, m->vtable_slot, rest
		) < 0
		           ? -1
		           : 0;
	}

	return bprint(
               bd,
               "%s\n"
               "{\n"
               "    IUnknown *cwinrt_obj_ = (IUnknown *)self;\n"
               "    typedef HRESULT (__stdcall *cwinrt_fn_t)(%s);\n"
               "    cwinrt_fn_t fn = (cwinrt_fn_t)((void **)cwinrt_obj_->lpVtbl)[%ud];\n"
               "    return fn(%s);\n"
               "}\n\n",
               m->c_sig,
               m->params_c,
               m->vtable_slot,
               call_args)
                   < 0
               ? -1
               : 0;
}

static int emit_class_new(int bd, cwinrt_mapped_type const *t) {
	if (!t->is_activatable || !t->activate_c_name || !t->winrt_name || !t->c_typedef) return 0;
	if (t->kind == CWINRT_MAP_CLASS && !t->method_count) return 0;
	return bprint(
               bd,
               "HRESULT %s(%s **out)\n"
               "{\n"
               "    if (!out) return E_POINTER;\n"
               "    *out = NULL;\n"
               "    return cwinrt_factory_activate(L\"%s\", NULL, (void **)out);\n"
               "}\n\n",
               t->activate_c_name,
               t->c_typedef,
               t->winrt_name)
                   < 0
               ? -1
               : 0;
}

static bool emit_impl_event_core(char const *winrt_method, char *core, size_t cap) {
	if (!winrt_method || !core || cap < 2) return false;
	if (strncmp(winrt_method, "add_", 4) == 0) {
		snprintf(core, cap, "%s", winrt_method + 4);
		return core [0] != '\0';
	}
	return false;
}

static size_t emit_impl_event_snake_char(char c, char *dst, size_t di) {
	if (c < 'A' || c > 'Z') {
		dst [di++] = c;
		return di;
	}
	if (di > 0) dst [di++] = '_';
	dst [di++] = ( char ) (c - 'A' + 'a');
	return di;
}

static void emit_impl_event_snake(char const *core, char *dst, size_t cap) {
	size_t di = 0;
	size_t i  = 0;
	if (!core || !dst || cap < 2) return;
	for (i = 0; core [i] && di + 2 < cap; i++) di = emit_impl_event_snake_char(core [i], dst, di);
	dst [di] = '\0';
}

static cwinrt_mapped_method const *
emit_event_find_remove(cwinrt_mapped_type const *t, char const *core) {
	uint32_t mj;
	for (mj = 0; mj < t->method_count; mj++) {
		char                        rem_core [128];
		cwinrt_mapped_method const *m2 = &t->methods [mj];
		if (!m2->winrt_name || strncmp(m2->winrt_name, "remove_", 7) != 0) continue;
		snprintf(rem_core, sizeof(rem_core), "%s", m2->winrt_name + 7);
		if (strcmp(core, rem_core) == 0) return m2;
	}
	return NULL;
}

typedef struct {
	cwinrt_mapped_unit const   *unit;
	cwinrt_mapped_type const   *t;
	char const                 *abbrev;
	char const                 *ev_snake;
	cwinrt_mapped_method const *add_m;
	cwinrt_mapped_method const *rem_m;
} emit_event_ctx;

/* Event on the class's default interface: add/remove slots dispatch directly on
   the class pointer, as before. */
static int emit_event_pair_direct(int bd, emit_event_ctx const *e) {
	return bprint(
	         bd,
	         "cwinrt_token %s_%s_on_%s(%s *self, cwinrt_event_fn fn, void *ctx)\n"
	         "{\n"
	         "    IUnknown *handler = NULL;\n"
	         "    cwinrt_event_handle eh = { 0 };\n"
	         "    cwinrt_token tok = { 0 };\n"
	         "    HRESULT hr = cwinrt_event_handler_create(fn, ctx, &handler);\n"
	         "    if (FAILED(hr)) return tok;\n"
	         "    hr = cwinrt_event_subscribe((IUnknown *)self, %udu, %udu, handler, &eh);\n"
	         "    handler->lpVtbl->Release(handler);\n"
	         "    if (FAILED(hr)) return tok;\n"
	         "    return eh.token;\n"
	         "}\n\n"
	         "void %s_%s_off_%s(%s *self, cwinrt_token token)\n"
	         "{\n"
	         "    cwinrt_event_handle eh = { (IUnknown *)self, %udu, token };\n"
	         "    cwinrt_event_unsubscribe(&eh);\n"
	         "}\n\n",
	         e->unit->ns_prefix, e->abbrev, e->ev_snake, e->t->c_typedef, e->add_m->vtable_slot, e->rem_m->vtable_slot,
	         e->unit->ns_prefix, e->abbrev, e->ev_snake, e->t->c_typedef, e->rem_m->vtable_slot
	       )
	         < 0
	       ? -1
	       : 0;
}

/* Event declared on a NON-default interface: the add/remove vtable slots are
   relative to that interface, so QI to it before (un)subscribing -- mirrors the
   property-accessor dispatch path. Calling the slots on the default-interface
   pointer would land on unrelated methods (an access violation in practice).
   Refcount: cwinrt_event_unsubscribe Releases handle->source once (the
   subscription's ref), so off_ must Release its own QI ref a second time to
   balance the QI -- otherwise every unsubscribe leaks one ref on the source. */
static int emit_event_pair_qi(int bd, emit_event_ctx const *e) {
	char const *iface = e->add_m->dispatch_iface_c;
	return bprint(
	         bd,
	         "cwinrt_token %s_%s_on_%s(%s *self, cwinrt_event_fn fn, void *ctx)\n"
	         "{\n"
	         "    IUnknown *handler = NULL;\n"
	         "    %s *cwinrt_disp_ = NULL;\n"
	         "    cwinrt_event_handle eh = { 0 };\n"
	         "    cwinrt_token tok = { 0 };\n"
	         "    HRESULT hr = cwinrt_event_handler_create(fn, ctx, &handler);\n"
	         "    if (FAILED(hr)) return tok;\n"
	         "    hr = ((IUnknown *)self)->lpVtbl->QueryInterface((IUnknown *)self, &CWINRT_IID_%s, (void **)&cwinrt_disp_);\n"
	         "    if (FAILED(hr)) { handler->lpVtbl->Release(handler); return tok; }\n"
	         "    hr = cwinrt_event_subscribe((IUnknown *)cwinrt_disp_, %udu, %udu, handler, &eh);\n"
	         "    handler->lpVtbl->Release(handler);\n"
	         "    ((IUnknown *)cwinrt_disp_)->lpVtbl->Release((IUnknown *)cwinrt_disp_);\n"
	         "    if (FAILED(hr)) return tok;\n"
	         "    return eh.token;\n"
	         "}\n\n"
	         "void %s_%s_off_%s(%s *self, cwinrt_token token)\n"
	         "{\n"
	         "    %s *cwinrt_disp_ = NULL;\n"
	         "    HRESULT hr = ((IUnknown *)self)->lpVtbl->QueryInterface((IUnknown *)self, &CWINRT_IID_%s, (void **)&cwinrt_disp_);\n"
	         "    if (FAILED(hr)) return;\n"
	         "    {\n"
	         "        cwinrt_event_handle eh = { (IUnknown *)cwinrt_disp_, %udu, token };\n"
	         "        cwinrt_event_unsubscribe(&eh);\n"
	         "    }\n"
	         "    ((IUnknown *)cwinrt_disp_)->lpVtbl->Release((IUnknown *)cwinrt_disp_);\n"
	         "}\n\n",
	         e->unit->ns_prefix, e->abbrev, e->ev_snake, e->t->c_typedef, iface, iface,
	         e->add_m->vtable_slot, e->rem_m->vtable_slot,
	         e->unit->ns_prefix, e->abbrev, e->ev_snake, e->t->c_typedef, iface, iface, e->rem_m->vtable_slot
	       )
	         < 0
	       ? -1
	       : 0;
}

static int emit_event_pair(int bd, emit_event_ctx const *e) {
	if (e->add_m->dispatch_iface_c && e->add_m->dispatch_has_iid) return emit_event_pair_qi(bd, e);
	return emit_event_pair_direct(bd, e);
}

static int emit_event_impls(int bd, cwinrt_mapped_unit const *unit, cwinrt_mapped_type const *t) {
	uint32_t    mi;
	char const *dot;
	char const *shortn;
	char const *abbrev;
	char        core [128];
	char        ev_snake [128];

	if (t->kind == CWINRT_MAP_IFACE) return 0;

	dot    = t->winrt_name ? strrchr(t->winrt_name, '.') : NULL;
	shortn = dot ? dot + 1 : "";
	abbrev = cwinrt_name_type_abbrev(unit->filter_ns, shortn);

	for (mi = 0; mi < t->method_count; mi++) {
		emit_event_ctx             e     = {unit, t, abbrev, ev_snake, &t->methods [mi], NULL};
		cwinrt_mapped_method const *add_m = e.add_m;

		if (!add_m->winrt_name || strncmp(add_m->winrt_name, "add_", 4) != 0) continue;
		if (!emit_impl_event_core(add_m->winrt_name, core, sizeof(core))) continue;
		e.rem_m = emit_event_find_remove(t, core);
		if (!e.rem_m || !add_m->vtable_slot || !e.rem_m->vtable_slot) continue;

		emit_impl_event_snake(core, ev_snake, sizeof(ev_snake));
		if (emit_event_pair(bd, &e) != 0) return -1;
	}
	return 0;
}

static bool
emit_impl_skip_iface_dup(cwinrt_mapped_unit const *unit, cwinrt_mapped_type const *t, cwinrt_mapped_method const *m) {
	return cwinrt_mapped_skip_iface_method_dup(unit, t, m);
}

static bool emit_method_is_static_dispatch(cwinrt_mapped_method const *m) {
	return m->is_static && m->static_class_winrt && m->dispatch_iface_c;
}

/* Prefer the self-contained activate+vtable dispatch whenever a vtable slot is
   known. A separately-named statics wrapper is fragile: its name can collide with
   the class method itself or a sibling overload, producing the wrong arity (C2197).
   Returns 1 if emitted (with rc set), 0 if not handled here. */
static int emit_method_static(int bd, cwinrt_mapped_method const *m, int *rc) {
	if (m->vtable_slot) {
		if (cwinrt_dispatch_iface_is_statics_facade(m->dispatch_iface_c))
			*rc = emit_static_vtable_direct(bd, m) != 0 ? -1 : 0;
		else *rc = emit_static_activate_vtable(bd, m) != 0 ? -1 : 0;
		return 1;
	}
	if (m->delegate_c_name) {
		*rc = emit_static_class_method(bd, m);
		return 1;
	}
	return 0;
}

static bool emit_method_is_instance_vtable(cwinrt_mapped_method const *m) {
	if (!m->vtable_slot || m->is_static) return false;
	if (m->params_c && cwinrt_params_has_self(m->params_c)) return true;
	return !m->params_c || !m->params_c [0];
}

static int emit_method_impl(int bd, cwinrt_mapped_method const *m) {
	int rc = 0;
	if (emit_method_is_static_dispatch(m) && emit_method_static(bd, m, &rc)) return rc;
	if (emit_method_is_instance_vtable(m)) return emit_instance_vtable(bd, m);
	return bprint(bd, "%s { return E_NOTIMPL; }\n\n", m->c_sig) < 0 ? -1 : 0;
}

static int emit_impl_prologue(int bd, cwinrt_mapped_unit const *unit) {
	emit_write_str(bd, "/* Generated by cwinrt-gen; do not edit. */\n\n");
	emit_write_str(
	  bd,
	  "#include <windows.h>\n#include <unknwn.h>\n#include <cwinrt/factory.h>\n" "#include <cwinrt/event.h>\n"
	);
	return bprint(bd, "#include <cwinrt/%s>\n\n", unit->header_name) < 0 ? -1 : 0;
}

static bool emit_impl_is_event_method(cwinrt_mapped_method const *m) {
	return m->winrt_name
	    && (strncmp(m->winrt_name, "add_", 4) == 0 || strncmp(m->winrt_name, "remove_", 7) == 0);
}

static int emit_impl_type(int bd, cwinrt_mapped_unit const *unit, cwinrt_mapped_type const *t) {
	uint32_t mi;
	if (emit_class_new(bd, t) != 0) return -1;
	for (mi = 0; mi < t->method_count; mi++) {
		cwinrt_mapped_method const *m = &t->methods [mi];
		if (emit_impl_is_event_method(m)) continue;
		if (emit_impl_skip_iface_dup(unit, t, m)) continue;
		if (emit_method_impl(bd, m) != 0) return -1;
	}
	return emit_event_impls(bd, unit, t);
}

static int emit_impl_body(int bd, cwinrt_mapped_unit const *unit) {
	uint32_t ti;
	if (emit_impl_prologue(bd, unit) != 0) return -1;
	for (ti = 0; ti < unit->type_count; ti++) {
		if (emit_impl_type(bd, unit, &unit->types [ti]) != 0) return -1;
	}
	return 0;
}

int cwinrt_emit_impl(cwinrt_mapped_unit const *unit, cwinrt_emit_opts const *opts) {
	char  path [1024];
	int   arena;
	int   bd;
	int   rc;
	omode mod = {.w = true, .t = true};

	if (!unit || !opts || !opts->out_dir || !opts->impl_basename) return -1;
	snprintf(path, sizeof(path), "%s/%s", opts->out_dir, opts->impl_basename);
	arena = mkarena();
	if (arena < 0) {
		errmsg("emit_impl mkarena failed");
		return -1;
	}
	bd = bopen(arena, path, mod);
	if (bd < 0) {
		errmsg("emit_impl bopen failed");
		rmarena(arena);
		return -1;
	}
	rc = emit_impl_body(bd, unit);
	rmbio(bd);
	rmarena(arena);
	return rc;
}
