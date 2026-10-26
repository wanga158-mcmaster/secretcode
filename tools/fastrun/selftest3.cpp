// fastrun self-test 3: fuel exhaustion with a yield interval on an async store (suspend, refuel, resume).
#include <wasmtime.h>
#include <wasmtime/async.h>
#include <cstdio>
#include <cstring>
#include <string>
int main() {
    const char* wat = R"((module (func (export "spin") (param i32) (result i32) (local i32)
        (loop $l (local.set 1 (i32.add (local.get 1) (i32.const 1))) (br_if $l (i32.lt_u (local.get 1) (local.get 0)))) (local.get 1))))";
    wasm_config_t* cfg = wasm_config_new(); wasmtime_config_consume_fuel_set(cfg, true);
    wasm_engine_t* e = wasm_engine_new_with_config(cfg);
    wasm_byte_vec_t w; wasmtime_wat2wasm(wat, std::strlen(wat), &w);
    wasmtime_module_t* m; wasmtime_module_new(e, (const uint8_t*)w.data, w.size, &m);
    wasmtime_store_t* s = wasmtime_store_new(e, nullptr, nullptr); auto* c = wasmtime_store_context(s);
    wasmtime_linker_t* l = wasmtime_linker_new(e);
    wasmtime_instance_t inst; wasm_trap_t* tr = nullptr; wasmtime_error_t* er = nullptr;
    const unsigned long long reserve = 1ull << 40, budget = 100000;
    wasmtime_context_set_fuel(c, reserve);
    auto* f0 = wasmtime_linker_instantiate_async(l, c, m, &inst, &tr, &er); while (!wasmtime_call_future_poll(f0)) {} wasmtime_call_future_delete(f0);
    wasmtime_extern_t x; wasmtime_instance_export_get(c, &inst, "spin", 4, &x);
    wasmtime_val_t arg; arg.kind = WASMTIME_I32; arg.of.i32 = 1000000; wasmtime_val_t res;
    std::printf("yield interval %llu\n", budget); std::fflush(stdout);
    if (auto* err = wasmtime_context_fuel_async_yield_interval(c, budget)) { std::printf("interval error\n"); return 1; }
    wasmtime_context_set_fuel(c, budget + reserve);
    auto* f = wasmtime_func_call_async(c, &x.of.func, &arg, 1, &res, 1, &tr, &er);
    for (int turn = 0; turn < 100; ++turn) {
        bool done = wasmtime_call_future_poll(f);
        unsigned long long fuel = 0; wasmtime_context_get_fuel(c, &fuel);
        std::printf("turn %d done %d fuel-left-in-chunk %lld\n", turn, done ? 1 : 0, (long long)fuel - (long long)reserve); std::fflush(stdout);
        if (done) break;
        wasmtime_context_fuel_async_yield_interval(c, budget);
        wasmtime_context_set_fuel(c, budget + reserve);
    }
    wasmtime_call_future_delete(f);
    if (tr) std::printf("trap\n"); else if (er) std::printf("error\n"); else std::printf("result %d\n", res.of.i32);
}
