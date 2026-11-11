# 应用 SDK（C / C++ / Rust）

本工程现在可以完成 C、C++、Rust 源码 → WASM → 签名 .app → USB 安装。Snake（C）、Bounce（C++，apps/bounce）、Bounce（Rust，apps/rust_bounce）是完整示例。

第三方开发者无需平台私钥即可使用 `--unsigned` 构建投稿 WASM；投稿内容与维护者签名发布流程见 [THIRD_PARTY_APPS.md](THIRD_PARTY_APPS.md)。默认命令仍会使用本机平台密钥签名，不要把该密钥分发给投稿者。

第一次写平台应用可先读 [第三方应用代码规范与构建指南](THIRD_PARTY_CODE_GUIDE.md)，按 [示例目录](../examples/README.md) 从触摸绘图、定时场景、可选输入或 Rust 存储开始。

各个 `platform_*` 函数的参数、权限、返回值及短示例见 [平台函数速查](PLATFORM_API.md)。

如果读的是独立的开发者 ZIP，请先按包内 `README.md` 安装 `requirements-app-sdk.txt` 并执行 `--unsigned` 示例。下面不带 `--unsigned` 的签名和 USB 安装命令只适用于持有平台私钥的维护者，不适用于投稿者。

## 安装与构建

固定使用 WebAssembly 项目的 WASI SDK 34.0 Windows x86_64 发行包，下载约 590 MiB。脚本校验官方 release asset 的 SHA256，不替换本机 Espressif 编译器。

```powershell
./tools/setup_sdk.ps1
python tools/build_c_app.py apps/snake
python tools/install_usb.py dist/snake.app --port COM4
```

默认工具链路径是 C:/esp/mini-app-sdk/wasi-sdk-34.0-x86_64-windows。可以设置 WASI_SDK_PATH 或传 --sdk PATH。首次工程初始化仍需 tools/bootstrap.ps1 生成开发密钥；已有密钥时必须保留。

工具链来源：[WebAssembly/wasi-sdk 34 release](https://github.com/WebAssembly/wasi-sdk/releases/tag/wasi-sdk-34)。

## ABI 与编译约束

- 包含 sdk/include/platform.h，导出无返回值的 app_init() 和 app_event(int type,int a,int b)。
- 目标 wasm32-unknown-unknown、MVP 指令集，使用 freestanding C；虽然编译器来自 WASI SDK，产物不导入 WASI。
- 不链接 libc、启动函数和系统调用；不使用 printf、malloc、文件或线程。无法解析的外部符号会使构建失败。
- 初始线性内存 64 KiB、最大 256 KiB，C 栈 16 KiB。常量和静态数据也在这块内存中。
- 每个回调最多 50,000 条 WASM 指令、96 次绘图调用，使用定时事件推进工作，不能阻塞等待。
- 导入仅来自 platform；图形、触摸和存储能力必须在 Manifest 声明。
- 事件 1 的 a 是平台毫秒计时低 32 位，需用无符号差值处理回绕；事件 2 为触摸释放，坐标相对 480×400 应用画布。
- C、C++ 和 Rust 构建链均已接入，产物都经同一套 ABI 校验；第三方库（需要 libc 或分配器的）仍未验证。

## C++ 与 Rust

C++ 复用 WASI SDK 自带的 clang++，不需要额外工具链：

```powershell
python tools/build_c_app.py apps/bounce
```

参数为 C 版本再叠加 `-std=c++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics`。没有 libstdc++，因此不使用 `new` / `delete`、RTTI、异常，也不使用需要运行期初始化的全局对象。类、模板、constexpr 与成员函数可正常使用，需要构造的对象放在栈上。

Rust 使用独立的 wasm32-unknown-unknown target（freestanding，不导入 WASI）：

```powershell
rustup target add wasm32-unknown-unknown
python tools/build_rust_app.py apps/rust_bounce
```

应用是 `#![no_std]` 的 cdylib，导出 `#[no_mangle] extern "C" fn app_init` 与 `app_event`，并自行提供 `#[panic_handler]`。构建脚本用 `-C link-arg` 把初始内存压到 64 KiB、栈固定 16 KiB 并置于数据之前；rustc 的默认内存声明会超出设备 4 页（256 KiB）的上限。缺少工具链时脚本会给出安装步骤并失败退出，不会静默跳过。

两种语言的产物都由 `tools/abi_check.mjs` 在签名前校验，规则与设备端 `runtime.c` 的装载检查一致。

## Snake

屏幕方向按钮控制移动，初始点击方向后开始。每 200 ms 移动一格，禁止立即反向；碰墙或碰到身体结束，点击屏幕重开。最多 64 节，达到上限结束本局。最高分通过 storage 权限写入独立 NVS 键 best，卸载应用后保留。

```powershell
node tools/snake_test.mjs
python tools/test_store_host.py
```

该测试验证导入接口、移动、吃食物、反向拒绝、碰撞、重开、最高分读写、计时回绕和绘图边界。板上 selftest 还会使用同一个已签名 C 产物进行初始化、移动、碰撞和重开，以检查 WAMR 兼容性及运行额度。

test_store_host.py 编译固件原始 store.c 和 cJSON，运行正常目录及 8 类拒绝用例。该电脑测试程序使用 WASI 输出测试结果，生产应用仍不使用 WASI；ESP-IDF/NVS/网络函数由链接器移除，不能把这项检查视为 HTTPS 或板上 NVS 验证。
