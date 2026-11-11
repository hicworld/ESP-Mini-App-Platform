(module
  (import "platform" "clear" (func $clear (param i32)))
  (import "platform" "rect" (func $rect (param i32 i32 i32 i32 i32)))
  (import "platform" "text" (func $text (param i32 i32 i32 i32 i32)))
  (memory (export "memory") 1 4)
  (data (i32.const 0) "Tap anywhere to paint")
  (func (export "app_init")
    (call $clear (i32.const 0x101d32))
    (call $text (i32.const 20) (i32.const 30) (i32.const 0) (i32.const 21) (i32.const 0xffffff)))
  (func (export "app_event") (param $type i32) (param $x i32) (param $y i32)
    (if (i32.eq (local.get $type) (i32.const 2)) (then
      (call $rect (local.get $x) (local.get $y) (i32.const 22) (i32.const 22) (i32.const 0x63dfbd))))))
