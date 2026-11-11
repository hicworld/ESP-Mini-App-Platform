#pragma once
/* Minimal stand-in for the WAMR public API. Only what firmware/main/runtime.c
 * actually uses is declared; the test double drives all of it from main.c so the
 * host API boundary can be exercised without the real interpreter. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void *wasm_module_t;
typedef void *wasm_module_inst_t;
typedef void *wasm_exec_env_t;
typedef void *wasm_function_inst_t;

typedef enum {
    WASM_I32, WASM_I64, WASM_F32, WASM_F64, WASM_V128, WASM_FUNCREF, WASM_EXTERNREF
} wasm_valkind_t;

typedef struct { const char *name; } wasm_export_t;

typedef struct NativeSymbol {
    const char *name;
    void *func_ptr;
    const char *signature;
    void *attachment;
} NativeSymbol;

typedef enum { Alloc_With_Allocator, Alloc_With_System_Allocator, Alloc_With_Pool } mem_alloc_type_t;
typedef struct { void *heap_buf; uint32_t heap_size; } mem_alloc_pool_t;
typedef union { mem_alloc_pool_t pool; } mem_alloc_option_t;

typedef struct RuntimeInitArgs {
    mem_alloc_type_t mem_alloc_type;
    mem_alloc_option_t mem_alloc_option;
    const char *native_module_name;
    NativeSymbol *native_symbols;
    uint32_t n_native_symbols;
    uint32_t max_thread_num;
} RuntimeInitArgs;

struct InstantiationArgs2;

bool wasm_runtime_full_init(RuntimeInitArgs *args);
wasm_module_t wasm_runtime_load(const uint8_t *buf,uint32_t size,char *error_buf,uint32_t error_buf_size);
void wasm_runtime_unload(wasm_module_t module);
uint32_t wasm_runtime_get_export_count(wasm_module_t module);
void wasm_runtime_get_export_type(wasm_module_t module,int32_t index,wasm_export_t *out);

bool wasm_runtime_instantiation_args_create(struct InstantiationArgs2 **out);
void wasm_runtime_instantiation_args_set_default_stack_size(struct InstantiationArgs2 *args,uint32_t size);
void wasm_runtime_instantiation_args_set_host_managed_heap_size(struct InstantiationArgs2 *args,uint32_t size);
void wasm_runtime_instantiation_args_set_max_memory_pages(struct InstantiationArgs2 *args,uint32_t pages);
void wasm_runtime_instantiation_args_destroy(struct InstantiationArgs2 *args);
wasm_module_inst_t wasm_runtime_instantiate_ex2(wasm_module_t module,struct InstantiationArgs2 *args,
                                                 char *error_buf,uint32_t error_buf_size);
void wasm_runtime_deinstantiate(wasm_module_inst_t instance);

wasm_exec_env_t wasm_runtime_create_exec_env(wasm_module_inst_t instance,uint32_t stack_size);
void wasm_runtime_destroy_exec_env(wasm_exec_env_t env);
wasm_function_inst_t wasm_runtime_lookup_function(wasm_module_inst_t instance,const char *name);
uint32_t wasm_func_get_param_count(wasm_function_inst_t func,wasm_module_inst_t instance);
uint32_t wasm_func_get_result_count(wasm_function_inst_t func,wasm_module_inst_t instance);
void wasm_func_get_param_types(wasm_function_inst_t func,wasm_module_inst_t instance,wasm_valkind_t *types);

void wasm_runtime_set_instruction_count_limit(wasm_exec_env_t env,uint32_t count);
bool wasm_runtime_call_wasm(wasm_exec_env_t env,wasm_function_inst_t func,uint32_t argc,uint32_t argv[]);

void wasm_runtime_set_exception(wasm_module_inst_t instance,const char *text);
const char *wasm_runtime_get_exception(wasm_module_inst_t instance);
wasm_module_inst_t wasm_runtime_get_module_inst(wasm_exec_env_t env);
bool wasm_runtime_validate_app_addr(wasm_module_inst_t instance,uint32_t offset,uint32_t size);
void *wasm_runtime_addr_app_to_native(wasm_module_inst_t instance,uint32_t offset);
