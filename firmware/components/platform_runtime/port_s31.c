/* Minimal ESP-IDF 6.1 port for the no-WASI, no-AOT S31 interpreter profile. */
#include "platform_api_vmcore.h"
#include "platform_api_extension.h"
#include "esp_heap_caps.h"
#include <limits.h>
#include <stdarg.h>

#if WASM_ENABLE_AOT != 0 || WASM_ENABLE_JIT != 0 || WASM_ENABLE_LIBC_WASI != 0
#error "This S31 port supports the interpreter-only/no-WASI profile"
#endif

int bh_platform_init(void) { return 0; }
void bh_platform_destroy(void) {}
int os_printf(const char *format, ...) {
    va_list args; va_start(args,format); int ret=vprintf(format,args); va_end(args); return ret;
}
int os_vprintf(const char *format,va_list args) { return vprintf(format,args); }
uint64 os_time_get_boot_us(void) { return (uint64)esp_timer_get_time(); }
uint64 os_time_thread_cputime_us(void) { return os_time_get_boot_us(); }
uint8 *os_thread_get_stack_boundary(void) {
#if defined(CONFIG_FREERTOS_USE_TRACE_FACILITY) && CONFIG_FREERTOS_USE_TRACE_FACILITY
    TaskStatus_t status; vTaskGetInfo(xTaskGetCurrentTaskHandle(),&status,pdFALSE,eInvalid);
    return status.pxStackBase;
#else
    return (uint8*)xTaskGetStackStart(NULL);
#endif
}
void os_thread_jit_write_protect_np(bool enabled) { (void)enabled; }
int os_usleep(uint32 usec) { return usleep(usec); }

typedef struct { void *base; size_t size; } allocation_t;
void *os_malloc(unsigned size) {
    if (size>UINT_MAX-sizeof(allocation_t)-7) return NULL;
    void *base=heap_caps_malloc((size_t)size+sizeof(allocation_t)+7,MALLOC_CAP_8BIT);
    if (!base) return NULL;
    uintptr_t aligned=((uintptr_t)base+sizeof(allocation_t)+7)&~(uintptr_t)7;
    allocation_t *header=(allocation_t*)aligned-1;
    header->base=base; header->size=size; return (void*)aligned;
}
void os_free(void *ptr) { if (ptr) heap_caps_free(((allocation_t*)ptr-1)->base); }
void *os_realloc(void *ptr,unsigned size) {
    if (!ptr) return os_malloc(size);
    if (!size) { os_free(ptr); return NULL; }
    void *next=os_malloc(size); if (!next) return NULL;
    size_t old=((allocation_t*)ptr-1)->size;
    memcpy(next,ptr,old<size?old:size); os_free(ptr); return next;
}
int os_dumps_proc_mem_info(char *out,unsigned int size) { (void)out; (void)size; return -1; }
void *os_mmap(void *hint,size_t size,int prot,int flags,os_file_handle file) {
    (void)hint; (void)flags; (void)file;
    if ((prot&MMAP_PROT_EXEC) || size>UINT_MAX) return NULL;
    void *ptr=os_malloc((unsigned)size); if (ptr) memset(ptr,0,size); return ptr;
}
void os_munmap(void *addr,size_t size) { (void)size; os_free(addr); }
void *os_mremap(void *old,size_t old_size,size_t new_size) {
    (void)old_size; return new_size>UINT_MAX?NULL:os_realloc(old,(unsigned)new_size);
}
int os_mprotect(void *addr,size_t size,int prot) { (void)addr; (void)size; return (prot&MMAP_PROT_EXEC)?-1:0; }
void os_dcache_flush(void) {}
void os_icache_flush(void *start,size_t length) { (void)start; (void)length; }
