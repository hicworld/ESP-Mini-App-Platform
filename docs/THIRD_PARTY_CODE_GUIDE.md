# 第三方应用代码规范与构建指南

面向准备为 ESP Mini App Platform 编写应用的作者。当前目标设备是 ESP-Mosaico／ESP32-S31，应用是独立 WASM 模块，由平台固件加载；它不是 Arduino/ESP-IDF 固件，也不能直接运行普通桌面 C/C++/Rust 程序。本文依据当前应用 ABI v1 与平台 0.5.0 源码。接口详情见 [SDK.md](SDK.md)、[app_format.md](app_format.md) 和 [HARDWARE_REQUIREMENTS.md](HARDWARE_REQUIREMENTS.md)。

## 1. 先运行一个示例

解压 `ESP-Mini-App-Developer-Kit.zip`，在解压目录打开 PowerShell。准备 Python、Node.js；C/C++ 使用 WASI SDK 34，Rust 另需 rustup 与 `wasm32-unknown-unknown` target。

工作台顶部的“检查开发环境”会显示 Python 依赖、Node.js、WASI SDK 和 Rust target 的状态与修复命令。工作台从桌面启动时可能没有终端里的完整 PATH。若已安装 Node.js 但构建仍提示找不到它，可设置 `NODE_EXE` 为 `node.exe` 的绝对路径，再启动工作台。

```powershell
python -m pip install -r requirements-app-sdk.txt
./tools/setup_sdk.ps1
node --version
rustup target add wasm32-unknown-unknown  # 仅 Rust 作者需要

python tools/build_c_app.py examples/c_color_pad --unsigned
python tools/check_submission.py examples/c_color_pad
```

无签名产物在 `dist/submission/c_color_pad.wasm`。若使用本地工作台，运行 `python tools/developer_studio.py`，从“示例应用”打开触摸调色板，阅读功能说明与源码后点击“复制到我的项目”；也可在“我的项目”中导入 `examples/c_color_pad` 文件夹。进入项目开发页后编辑源码与 Manifest，保存并构建，再跳转到“电脑交互预览”。“开发文档”有本指南和平台函数速查。工作台导入的是副本；同名项目会加序号。项目可移入“最近移除”并恢复，原始导入目录不会被改动。工作台会阻止覆盖已在外部修改的源码。预览在浏览器运行 WASM，不能代替设备上的 WAMR、外设和执行预算检查。

| 起点 | 命令 | 重点 |
| --- | --- | --- |
| `templates/third_party_app` | `python tools/build_c_app.py templates/third_party_app --unsigned` | 最小 C 模板 |
| `examples/c_color_pad` | `python tools/build_c_app.py examples/c_color_pad --unsigned` | 绘图与触摸 |
| `examples/c_scene_timer` | `python tools/build_c_app.py examples/c_scene_timer --unsigned` | 场景与定时 |
| `examples/c_optional_input` | `python tools/build_c_app.py examples/c_optional_input --unsigned` | 可选外设与降级 |
| `examples/rust_saved_counter` | `python tools/build_rust_app.py examples/rust_saved_counter --unsigned` | Rust 与存储 |
| `apps/bounce` | `python tools/build_c_app.py apps/bounce --unsigned` | C++ 类与场景 |

每次构建后，运行 `python tools/check_submission.py <应用目录>`。示例 ID 如 `org.example.colorpad` 只是占位，正式投稿必须换成作者自己的 ID。

## 2. 项目结构与 Manifest

C/C++ 目录放 `main.c`、`main.cpp` 或 `main.cc` 中的一个以及 `manifest.json`；Rust 目录放 `Cargo.toml`、`src/lib.rs`、`manifest.json`。当前构建脚本编译一个 C/C++ 入口文件；额外代码可以放在本目录的头文件并由入口包含。Rust 应用通过本包 `sdk/rust/miniapp` 绑定平台 API；复制 Rust 示例到其他层级时，核对 `Cargo.toml` 中相对路径。工作台导入已使用 `miniapp` 路径依赖的 Rust 示例时，会把该 SDK 依赖复制到项目内。

Manifest 的核心字段如下；字段名和能力名称保持英文，版本为 `X.Y.Z`：

```json
{
  "manifest_version": 1,
  "api_version": 1,
  "id": "org.yourname.myapp",
  "name": "My App",
  "version": "0.1.0",
  "platform_min": "0.4.8",
  "entry": "main.wasm",
  "permissions": ["display", "touch", "input"],
  "hardware": ["display", "touch"],
  "optional_hardware": ["motion", "buttons"]
}
```

- `id` 至少三段、小写字母和数字组成，每段以字母开头，总长少于 48 字符；同一应用更新时保持 ID 不变并提升版本。`name` 为 1–39 **UTF-8 字节**，不等同于 39 个汉字。
- 可选填公开显示的 `author`（最多 80 个 UTF-8 字节）和 `description`（最多 500 个 UTF-8 字节）。本地工作台的“查看应用详情”可预览它们，保存 Manifest 后会进入投稿包；新系统的真机应用详情页也会显示 `author`。作者的私人联系方式仍单独填写在投稿信息中。
- `platform_min` 写实际使用的最早接口版本。使用 `scene` 至少 0.4.0，使用 `platform_hardware()` 至少 0.4.8；使用 `platform_imu()`、`platform_image()` 等第一批新接口至少 0.5.0。不要把新 API 宣称为旧固件支持。
- `permissions` 只声明实际调用的能力：`display`、`touch`、`storage`、`environment`、`input`、`audio`、`sensors`、`feedback`。没有声明相应权限却调用宿主 API，运行时会拒绝或使应用异常。
- `hardware` 是缺少时不能启动的外设；`optional_hardware` 是缺少时仍可启动、代码必须降级的外设。两组不得重复。可选外设不是权限授权。例如接收按键事件还需要 `input` 权限。
- 可选外设运行中也可能变化；像 `c_optional_input` 一样定期查询 `platform_hardware()`，在能力缺失时保留触摸等替代操作。
- `entry` 固定为 `main.wasm`。`wasm_size` 和 `sha256` 由构建/签名校验流程根据实际 WASM 计算，不要手填。编码后的 Manifest 有 2048 字节上限。

`manifest.json` 必须与应用实际行为一致。平台应用包仅含一个 Manifest 和一个 WASM；图片、声音等外部文件不能靠放进项目文件夹就自动在设备上可用。

## 3. ABI 与代码约定

首次调用 `platform_clear`、`platform_text` 等函数时，建议对照 [平台函数速查](PLATFORM_API.md)：逐个说明坐标、颜色、字节长度、权限、成功/失败返回值，并附有 C 示例。

入口必须导出 `app_init()` 和 `app_event(int type, int a, int b)`；Rust 对应 `#[no_mangle] extern "C"`。运行时在启动时调用 `app_init`。`app_event` 的事件含义：

| type | a | b | 用法 |
| --- | --- | --- | --- |
| 1 | 平台毫秒计时低 32 位 | 0 | 约每 50 ms 的定时通知；用无符号减法计算间隔，处理回绕 |
| 2 | 触摸释放 x | 触摸释放 y | 坐标相对 480 × 400 应用画布 |
| 3 | 1 人体感应、2 按键 A、3 按键 B | 0 | 需 `input` 权限；有对应外设才会收到 |
| 4 / 5 / 6 | 按下 / 移动 / 抬起的 x | y | 0.5.0 起的触摸手势，需 	ouch 权限 |
| 7 | 长按位置 x | y | 静止约 600 ms，需 	ouch 权限 |
| 8 | 方向：1 左、2 右、3 上、4 下 | 位移像素 | 滑动手势，需 	ouch 权限 |

回调应短小、确定、可返回；不要在回调里等待输入、睡眠或运行无限循环。设备端单次入口预算为 50,000 条 WASM 指令，最多 96 次绘图调用，初始线性内存 64 KiB、运行上限 256 KiB，WASM 负载最大 256 KiB。屏幕 480 × 400，顶部 Home 控件由系统保留。

平台只提供 `platform` 模块中的受控宿主接口，不提供 WASI、文件系统、线程、任意网络 URL 或原生 GPIO。C/C++ 是 freestanding 构建，不能依赖 `printf`、`malloc`、libc、libstdc++、系统调用、异常或 RTTI。C++ 的全局对象必须能静态常量初始化；有动态构造器的 WASM 会被 ABI 检查拒绝。编译器也可能为大的局部数组初始化生成 `memset`：如果链接报未定义符号，改用静态纯数据数组或逐字段初始化。Rust 使用 `#![no_std]`、`cdylib` 和自己的 `#[panic_handler]`，不要引入需要分配器或 WASI 的库。

绘图调用的颜色格式是 `0xRRGGBB`。`platform_text` 的长度是 UTF-8 **字节数**，最多 160；静态英文字符串可传 `sizeof(text)-1`。`scene_cmd_t` 是 64 字节；文字命令含 32 字节内联缓冲，必须以 NUL 结尾。一次 `platform_scene` 表示完整且不透明的一帧，不能在同一个入口里与旧式 `clear/rect/text` 绘图混用。场景命令最多 96 条，尺寸和边界仍需符合 [scene.h](../sdk/include/scene.h) 与运行时校验。

使用 IMU 先在 Manifest 声明 `sensors` 权限和 `imu` 外设，再查询 `platform_hardware() & PLATFORM_HW_IMU`，调用 `platform_imu(&sample, sizeof(sample))` 并检查返回值。加速度单位是毫 g；每次回调最多读取 4 次传感器。倾斜弹球的完整源码见随本次验证生成的 `tilt_ball` 投稿项目；单位、错误码和可选硬件规则见 [第一批开放能力](FIRST_BATCH_CAPABILITIES.md)。

应用状态放在普通静态纯数据里；不要把指向栈变量的指针留到下一次事件。需要跨启动保存时，声明 `storage` 后使用 `kv_get/kv_set`：键最多 8 个，键名 1–15 字符，单值最多 128 字节；全运行时每秒最多 4 次变化写入。务必检查写入返回值，并在额度不足时延后重试。`rust_saved_counter` 展示了这个处理。

## 4. 构建、预览、投稿

修改 Manifest 和源码后，在开发者包根目录运行相应 `--unsigned` 构建，再运行 `check_submission.py`。构建脚本会检查 ABI 和 Manifest；自检输出 WASM 大小与 SHA256。若检查失败，先看编译/ABI 日志；“编译成功”本身不能证明设备可装载，也不能证明权限、交互或素材合法。

工作台可预览画面、触摸、定时和部分外设输入，并导出待审核 ZIP。投稿包应包括源码、Manifest、WASM、自检结果、作者与联系方式、用途、所申请权限的理由，以及源码和素材的来源/授权。模板见 `templates/third_party_app/投稿信息.md`，流程见 [THIRD_PARTY_APPS.md](THIRD_PARTY_APPS.md)。不要提交私钥、构建缓存、与应用无关的文件，也不要使用示例 ID 原样投稿。

`--unsigned` 产生的 WASM 和工作台 ZIP **不是**官方可安装 `.app`。平台维护者需审核源码与授权、重新构建、实机测试，再用平台密钥签名和发布；开发者无需接触平台私钥或正式在线目录。
