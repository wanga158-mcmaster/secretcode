// fastrun self-test: wasmtime trap handling and wasm exception handling, sync and on an async fiber.
#include <wasmtime.h>
#include <wasmtime/async.h>
#include <cstdio>
#include <cstring>
#include <string>

static const char* kWat = R"((module
  (import "env" "wait" (func $wait))
  (tag $e (param i32))
  (func (export "trap_after_wait") (call $wait) (call $wait) unreachable)
  (func (export "catch_after_wait") (result i32)
    (call $wait)
    (block $h (result i32)
      (try_table (catch $e $h)
        (throw $e (i32.const 11)))
      (i32.const -1)))
  (func (export "trap") unreachable)
  (func (export "uncaught_trap") (throw $e (i32.const 3)))
  (func (export "uncaught_after_wait_trap") (call $wait) (throw $e (i32.const 4)))
  (func (export "catch") (result i32)
    (block $h (result i32)
      (try_table (catch $e $h)
        (throw $e (i32.const 7)))
      (i32.const -1)))
  (func $thrower (param i32) (throw $e (local.get 0)))
  (func (export "deep") (result i32)
    (block $h (result i32)
      (try_table (catch $e $h)
        (call $thrower (i32.const 9)))
      (i32.const -1))))
)";

static int g_polls = 0;
static bool wait_ready(void*) { return ++g_polls % 2 == 0; }
static void wait_cb(void*, wasmtime_caller_t*, const wasmtime_val_t*, size_t, wasmtime_val_t*, size_t, wasm_trap_t**,
                    wasmtime_async_continuation_t* c) { c->callback = wait_ready; c->env = nullptr; c->finalizer = nullptr; }
static std::string message(wasmtime_error_t* e)
{
    wasm_byte_vec_t m;
    wasmtime_error_message(e, &m);
    std::string s(m.data, m.size);
    wasm_byte_vec_delete(&m);
    wasmtime_error_delete(e);
    return s;
}
static std::string trap_message(wasm_trap_t* t)
{
    wasm_byte_vec_t m;
    wasm_trap_message(t, &m);
    std::string s(m.data, m.size);
    wasm_byte_vec_delete(&m);
    wasm_trap_delete(t);
    return s;
}

int main(int argc, char** argv)
{
    const bool signals = !(argc > 1 && std::strcmp(argv[1], "nosignals") == 0);
    wasm_config_t* config = wasm_config_new();
    wasmtime_config_wasm_exceptions_set(config, true);
    wasmtime_config_consume_fuel_set(config, true);
    wasmtime_config_signals_based_traps_set(config, signals);
    wasm_engine_t* engine = wasm_engine_new_with_config(config);
    wasm_byte_vec_t wasm;
    if (auto* e = wasmtime_wat2wasm(kWat, std::strlen(kWat), &wasm)) { std::printf("wat: %s\n", message(e).c_str()); return 1; }
    wasmtime_module_t* module;
    if (auto* e = wasmtime_module_new(engine, reinterpret_cast<const uint8_t*>(wasm.data), wasm.size, &module)) { std::printf("compile: %s\n", message(e).c_str()); return 1; }
    for (int async = 0; async < 2; ++async)
    {
        wasmtime_store_t* store = wasmtime_store_new(engine, nullptr, nullptr);
        wasmtime_context_t* ctx = wasmtime_store_context(store);
        wasmtime_context_set_fuel(ctx, 1u << 30);
        wasmtime_linker_t* linker = wasmtime_linker_new(engine);
        {
            wasm_functype_t* ty = wasm_functype_new_0_0();
            if (async) wasmtime_linker_define_async_func(linker, "env", 3, "wait", 4, ty, wait_cb, nullptr, nullptr);
            else wasmtime_linker_define_func(linker, "env", 3, "wait", 4, ty,
                [](void*, wasmtime_caller_t*, const wasmtime_val_t*, size_t, wasmtime_val_t*, size_t) -> wasm_trap_t* { return nullptr; }, nullptr, nullptr);
            wasm_functype_delete(ty);
        }
        wasmtime_instance_t instance;
        wasm_trap_t* trap = nullptr;
        wasmtime_error_t* error = nullptr;
        if (async)
        {
            auto* f = wasmtime_linker_instantiate_async(linker, ctx, module, &instance, &trap, &error);
            while (!wasmtime_call_future_poll(f)) {}
            wasmtime_call_future_delete(f);
        }
        else error = wasmtime_linker_instantiate(linker, ctx, module, &instance, &trap);
        if (error || trap) { std::printf("instantiate failed\n"); return 1; }
        for (const char* name : { "catch", "deep", "trap", "catch_after_wait", "trap_after_wait", "uncaught_trap", "uncaught_after_wait_trap" })
        {
            wasmtime_extern_t item;
            wasmtime_instance_export_get(ctx, &instance, name, std::strlen(name), &item);
            wasmtime_val_t result;
            trap = nullptr;
            error = nullptr;
            const size_t nres = std::strstr(name, "trap") ? 0 : 1;
            std::printf("%s %s ... ", async ? "async" : "sync ", name);
            std::fflush(stdout);
            if (async)
            {
                auto* f = wasmtime_func_call_async(ctx, &item.of.func, nullptr, 0, &result, nres, &trap, &error);
                while (!wasmtime_call_future_poll(f)) {}
                wasmtime_call_future_delete(f);
            }
            else error = wasmtime_func_call(ctx, &item.of.func, nullptr, 0, &result, nres, &trap);
            if (error) std::printf("error: %s\n", message(error).c_str());
            else if (trap) std::printf("trap: %s\n", trap_message(trap).c_str());
            else std::printf("result %d\n", nres ? result.of.i32 : 0);
        }
        wasmtime_linker_delete(linker);
        wasmtime_store_delete(store);
    }
    std::printf("selftest done (signals_based_traps=%d)\n", signals ? 1 : 0);
    return 0;
}
