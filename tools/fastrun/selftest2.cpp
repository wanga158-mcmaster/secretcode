// fastrun self-test 2: a host function of an async, fuelled store calls into a second (fuel-free) engine's store.
#include <wasmtime.h>
#include <wasmtime/async.h>
#include <cstdio>
#include <cstring>
#include <string>

static wasm_engine_t* g_inner_engine; static wasmtime_store_t* g_inner_store; static wasmtime_func_t g_inner_ok, g_inner_trap;
static int g_mode = 0, g_polls = 0;
static std::string msg(wasm_trap_t* t) { wasm_byte_vec_t m; wasm_trap_message(t, &m); std::string s(m.data, m.size); wasm_byte_vec_delete(&m); wasm_trap_delete(t); return s; }
static wasm_trap_t* host(void*, wasmtime_caller_t*, wasmtime_val_raw_t* a, size_t) {
    wasm_trap_t* trap = nullptr; wasmtime_val_raw_t r[2]; r[0].i32 = 5;
    auto* ctx = wasmtime_store_context(g_inner_store);
    auto* e = wasmtime_func_call_unchecked(ctx, g_mode ? &g_inner_trap : &g_inner_ok, r, 1, &trap);
    if (e) std::printf("[inner error] ");
    if (trap) std::printf("[inner trap: %.40s] ", msg(trap).c_str());
    a[0].i32 = r[0].i32; return nullptr; }
static bool ready(void*) { return ++g_polls % 2 == 0; }
static void wait_cb(void*, wasmtime_caller_t*, const wasmtime_val_t*, size_t, wasmtime_val_t*, size_t, wasm_trap_t**, wasmtime_async_continuation_t* c) { c->callback = ready; c->env = nullptr; c->finalizer = nullptr; }

int main() {
    const char* inner = R"((module (func (export "ok") (param i32) (result i32) (i32.add (local.get 0) (i32.const 1))) (func (export "bad") (param i32) (result i32) unreachable)))";
    const char* outer = R"((module (import "env" "host" (func $h (param i32) (result i32))) (import "env" "wait" (func $w))
      (func (export "run") (result i32) (call $w) (call $h (i32.const 1)) (call $w) (drop) (call $h (i32.const 2)))
      (func (export "run_then_trap") (call $w) (drop (call $h (i32.const 1))) (call $w) unreachable)))";
    g_inner_engine = wasm_engine_new();
    wasm_byte_vec_t w; wasmtime_wat2wasm(inner, std::strlen(inner), &w);
    wasmtime_module_t* im; wasmtime_module_new(g_inner_engine, (const uint8_t*)w.data, w.size, &im);
    g_inner_store = wasmtime_store_new(g_inner_engine, nullptr, nullptr);
    wasmtime_instance_t ii; wasm_trap_t* t = nullptr;
    wasmtime_instance_new(wasmtime_store_context(g_inner_store), im, nullptr, 0, &ii, &t);
    wasmtime_extern_t x;
    wasmtime_instance_export_get(wasmtime_store_context(g_inner_store), &ii, "ok", 2, &x); g_inner_ok = x.of.func;
    wasmtime_instance_export_get(wasmtime_store_context(g_inner_store), &ii, "bad", 3, &x); g_inner_trap = x.of.func;
    wasm_config_t* cfg = wasm_config_new(); wasmtime_config_consume_fuel_set(cfg, true); wasmtime_config_async_stack_size_set(cfg, 8u << 20);
    wasm_engine_t* oe = wasm_engine_new_with_config(cfg);
    wasm_byte_vec_t w2; wasmtime_wat2wasm(outer, std::strlen(outer), &w2);
    wasmtime_module_t* om; wasmtime_module_new(oe, (const uint8_t*)w2.data, w2.size, &om);
    wasmtime_linker_t* l = wasmtime_linker_new(oe);
    wasm_valtype_t* p1 = wasm_valtype_new_i32(); wasm_valtype_t* r1 = wasm_valtype_new_i32();
    wasm_functype_t* ht = wasm_functype_new_1_1(p1, r1);
    wasmtime_linker_define_func_unchecked(l, "env", 3, "host", 4, ht, host, nullptr, nullptr);
    wasm_functype_t* wt = wasm_functype_new_0_0();
    wasmtime_linker_define_async_func(l, "env", 3, "wait", 4, wt, wait_cb, nullptr, nullptr);
    for (int mode = 0; mode < 2; ++mode) for (const char* fn : {"run", "run_then_trap"}) {
        g_mode = mode;
        wasmtime_store_t* s = wasmtime_store_new(oe, nullptr, nullptr); auto* c = wasmtime_store_context(s);
        wasmtime_context_set_fuel(c, 1u << 30); wasmtime_context_fuel_async_yield_interval(c, 1000000);
        wasmtime_instance_t oi; wasm_trap_t* tr = nullptr; wasmtime_error_t* er = nullptr;
        auto* f0 = wasmtime_linker_instantiate_async(l, c, om, &oi, &tr, &er); while (!wasmtime_call_future_poll(f0)) {} wasmtime_call_future_delete(f0);
        wasmtime_instance_export_get(c, &oi, fn, std::strlen(fn), &x);
        wasmtime_val_t res; tr = nullptr; er = nullptr;
        std::printf("mode %d %s: ", mode, fn); std::fflush(stdout);
        auto* f = wasmtime_func_call_async(c, &x.of.func, nullptr, 0, &res, std::strcmp(fn, "run") == 0 ? 1 : 0, &tr, &er);
        int polls = 0; while (!wasmtime_call_future_poll(f)) ++polls;
        wasmtime_call_future_delete(f);
        if (tr) std::printf("trap %.50s (polls %d)\n", msg(tr).c_str(), polls); else if (er) std::printf("error\n"); else std::printf("result %d (polls %d)\n", res.of.i32, polls);
        wasmtime_store_delete(s);
    }
    std::printf("done\n");
}
