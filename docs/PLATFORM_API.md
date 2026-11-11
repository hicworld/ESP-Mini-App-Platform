# 平台函数速查（C / C++）

这份文档对应应用 ABI v1 的 [platform.h](../sdk/include/platform.h) 和 [scene.h](../sdk/include/scene.h)。在 C/C++ 源文件中写 `#include "platform.h"`；Rust 对应函数见 `sdk/rust/miniapp/src/lib.rs`。所有坐标都以应用画布左上角为 `(0,0)`，画布为 **480 × 400** 像素。颜色用 `0xRRGGBB`，例如 `0xFFFFFF` 是白色。

应用必须导出 `void app_init(void)` 和 `void app_event(int type, int a, int b)`。启动时调用 `app_init`；`app_event` 的事件如下：

| `type` | `a` / `b` | 说明 |
| --- | --- | --- |
| 1 | `a` 为平台毫秒计时低 32 位，`b=0` | 约每 50 ms 触发一次；用无符号减法计算间隔 |
| 2 | `a=x`，`b=y` | 触摸释放，需 `touch` 权限和触摸外设 |
| 3 | `a=1` 人体感应，`2` 按键 A，`3` 按键 B；`b=0` | 需 `input` 权限和相应外设 |
| 4/5/6 | `a=x`，`b=y` | 按下 / 移动 / 抬起；需 `touch` 权限 |
| 7 | `a=x`，`b=y` | 长按；需 `touch` 权限 |
| 8 | `a` 为方向 1 左、2 右、3 上、4 下；`b` 为像素位移 | 滑动；需 `touch` 权限 |

在 `manifest.json` 中声明用到的权限和必需外设。调用缺少权限的受控接口可能让应用异常停止；可选外设缺失时应保留其他交互方式。一次回调最多执行 50,000 条 WASM 指令、96 次绘图调用；回调内不要等待或死循环。

## 屏幕绘图

下列三个即时绘图函数都需要 `permissions` 含 `display`，且不会自动清除上一次的图像。画完一帧通常先清屏，再画矩形和文字。单次 `app_init` 或 `app_event` 合计最多 96 次绘图调用。

| 函数 | 参数 | 行为 |
| --- | --- | --- |
| `platform_clear(uint32_t rgb)` | `rgb`：背景色 | 用颜色填满应用画布。 |
| `platform_rect(int x, int y, int w, int h, uint32_t rgb)` | 左上角、宽高、填充色 | 画实心矩形；应使用画布内的正宽高。越界矩形不会显示。 |
| `platform_text(int x, int y, const char *utf8, int length, uint32_t rgb)` | 起点、UTF-8 字节指针、**字节数**、颜色 | 显示文字；`length` 范围是 0–160，缓冲区必须有效。坐标在画布外时不会显示。 |

```c
static const char title[] = "Hello";
platform_clear(0x18231D);
platform_rect(20, 20, 120, 40, 0x357A5B);
platform_text(32, 28, title, (int)(sizeof(title) - 1), 0xFFFFFF);
```

`platform_text` 不会替你计算字符串长度。中文使用 UTF-8 时一个汉字通常占多个字节；不要把字符数直接当 `length`。若逐项绘图出现闪烁，可改用下面的整帧提交。

## 一次提交完整画面

`int32_t platform_scene(const scene_cmd_t *commands, int32_t count, uint32_t background)` 自平台 **0.4.0** 起可用，需要 `display` 权限。它先验证并复制整个命令数组，再以 `background` 为底色一次提交画面。成功返回 `0`；屏幕不可用时返回 `-1`，不合法的命令可能导致应用异常停止。一次提交会用完该回调的绘图额度；不要在同一回调中再调用 `platform_clear`、`platform_rect` 或 `platform_text`。`count` 为 1–96。

`scene_cmd_t` 的字段及形状：

| `kind` | 需要填写的字段 | 效果 |
| --- | --- | --- |
| `SCENE_RECT` | `x,y,w,h,rgb`；`p` 可作圆角半径 | 实心矩形 |
| `SCENE_ELLIPSE` | `x,y,w,h,rgb` | 椭圆 |
| `SCENE_TRIANGLE` | `(x,y)`、`(w,h)`、`(p,q)`、`rgb` | 三角形的三个顶点 |
| `SCENE_TEXT` | `x,y,w,h,rgb,text` | 在给定矩形内显示文字；`text[32]` 必须以 `\0` 结尾，最多装 31 个 UTF-8 字节 |

命令结构固定 64 字节；请把未使用字段初始化为 0，并确保图形边界和总绘制面积合法。完整用法见 [c_scene_timer](../examples/c_scene_timer/main.c)。

## 时间与外设

| 函数 | 权限、参数和返回值 | 常见用途 |
| --- | --- | --- |
| `uint32_t platform_millis(void)` | 无额外权限；返回平台启动以来的毫秒数，32 位回绕 | 定时动画：`if ((uint32_t)(now - last) >= 200)`。 |
| `uint32_t platform_hardware(void)` | 自平台 **0.4.8** 起可用；返回当前可用外设位图 | 用 `PLATFORM_HW_TOUCH`、`PLATFORM_HW_MOTION` 等位检查可选外设。外设状态可能变化。 |
| `void platform_settings(void)` | 自平台 **0.4.1** 起可用；请求打开系统持有的 Wi-Fi/位置设置 | 应用不能直接读取密码，也不能传入 URL。 |
| `int32_t platform_environment(environment_t *out, int32_t capacity)` | 自平台 **0.4.0** 起可用，需 `environment` 权限；`capacity` 至少 `sizeof(environment_t)`；成功返回结构大小 **56**，参数或权限错误会使应用异常 | 读取平台提供的时间、天气与位置摘要。 |

`environment_t.flags` 中 bit 0 表示时钟有效、bit 1 天气有效、bit 2 天气数据过期、bit 3 已配置位置、bit 4 输入外设可用。`seconds` 是本地当天秒数；`temperature` 是摄氏度乘 10；`weather` 为 WMO 代码；`daylight` 为 `-1` 未知、`0` 夜间、`1` 白天；`weather_age` 是天气数据年龄（秒）。先看有效标志，再使用字段。电脑预览中的环境数据仅供交互测试。

```c
uint32_t available = platform_hardware();
if (available & PLATFORM_HW_MOTION) {
    /* 可以响应人体感应事件；仍需在 Manifest 声明 input 权限。 */
}
environment_t env;
if (platform_environment(&env, sizeof(env)) == sizeof(env) && (env.flags & 2u)) {
    /* env.weather 有效。 */
}
```

## 应用存储

需在 `permissions` 中声明 `storage`。每个应用有自己的键值空间，最多 8 个键；键长 1–15 个 ASCII 字节，只能用字母、数字、`_`、`-`，不能以 `__` 开头。单值最多 128 字节。全运行时每秒最多 4 次**变化写入**；写入相同内容不占用闪存写入额度。卸载后数据默认保留，用户可在应用详情页清除。

| 函数 | 返回值 |
| --- | --- |
| `platform_kv_get(key, key_length, value, capacity)` | `>0`：读取字节数；`0`：键不存在；`-2`：缓冲区太小；`-1`：读取错误。无权限或参数不合法会使应用异常。 |
| `platform_kv_set(key, key_length, value, value_length)` | **非负数**：写入成功，或内容与旧值相同；当前设备端返回值长度，电脑预览返回 `0`。`-3`：键数、速率或系统保留空间额度不足；`-1`：存储错误。无权限或参数不合法会使应用异常。 |

```c
uint32_t counter = 0;
if (platform_kv_get("count", 5, &counter, sizeof(counter)) != sizeof(counter)) counter = 0;
counter++;
if (platform_kv_set("count", 5, &counter, sizeof(counter)) == -3) {
    /* 稍后重试，不要在当前回调中不断写入。 */
}
```

## 内置声音

`int32_t platform_audio(int32_t kind, int32_t value)` 自平台 **0.4.3** 起可用，需 `audio` 权限，每次回调最多调用 4 次；超过配额会使应用异常。声音由平台提供，不能传声音文件或 PCM 数据。

| `kind` | `value` | 用途 |
| --- | --- | --- |
| 1 | `0` 停止、`1` 白天音乐、`2` 夜间音乐 | 切换背景音乐 |
| 2 | `1` 抚摸、`2` 喂食、`3` 浇水、`4` 到访、`5` 风声 | 播放内置音效 |
| 3 | `0`–`75` 设置音量；`-1` 读取 | 音量 |

命令成功通常返回 `0`，读取音量时返回音量值；`-1` 表示不可用、`-2` 参数错误、`-3` 速率受限。应用停止或异常时平台会停止该应用的声音。

## 第一批新增能力（0.5.0）

新增 `platform_image`、`platform_imu`、`platform_orientation`、`platform_magnet`、`platform_battery`、`platform_feedback`；新增手势事件及 `sensors`、`feedback` 权限。接口参数、单位、调用额度、板卡差异和图片转换方法见 [第一批开放能力](FIRST_BATCH_CAPABILITIES.md)。

## 继续阅读

- [第三方应用代码规范与构建指南](THIRD_PARTY_CODE_GUIDE.md)：项目结构、Manifest、构建及投稿。
- [应用格式](app_format.md)：权限、外设及应用包约束。
- [示例目录](../examples/README.md)：能直接运行的最小实例。
