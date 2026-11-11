(module
  (import "platform" "clear" (func $clear (param i32)))
  (import "platform" "text" (func $text (param i32 i32 i32 i32 i32)))
  (memory (export "memory") 1 4)
  (data (i32.const 0) "Hello from WebAssembly!")
  (func (export "app_init")
    (call $clear (i32.const 0x101d32))
    (call $text (i32.const 28) (i32.const 100) (i32.const 0) (i32.const 23) (i32.const 0x63dfbd)))
  (func (export "app_event") (param i32 i32 i32)))
