# 第一批开放能力（平台 0.5.0）

本页对应 [platform.h](../sdk/include/platform.h)、[capabilities.h](../sdk/include/capabilities.h) 和 [综合示例](../examples/c_first_batch/main.c)。C/C++、Rust 均使用原有 ABI v1，应用仍只导出 `app_init` 和 `app_event`。新版导入函数要求设备系统至少为 0.5.0；Manifest 请写 `"platform_min": "0.5.0"`。

## 触摸手势

Manifest 声明 `touch` 权限以及必需或可选的 `touch` 外设。事件 `app_event(type,a,b)`：

| type | a | b | 含义 |
| --- | --- | --- | --- |
| 2 | x | y | 短按点击，兼容旧应用 |
| 4 | x | y | 手指按下 |
| 5 | x | y | 移动，最长约每 30 ms 一次 |
| 6 | x | y | 抬起或取消 |
| 7 | x | y | 静止约 600 ms 的长按，单次手势只发一次 |
| 8 | 方向（1 左、2 右、3 上、4 下） | 位移像素 | 单轴位移至少 45 px 的滑动 |

坐标相对于 480 × 400 应用画布并被限制在画布内。长按或滑动后不再补发 `type=2`。`type=6` 在 `type=8` 之前发出。触摸事件通过消息队列送给应用，移动消息可能因队列拥堵丢失；不要把每个移动点当作必须收到的数据。

## 中文与图片

固件内置 Noto Sans SC 的 GB2312 汉字集合及平台所需符号。用 UTF-8 字节串调用 `platform_text` 或 `SCENE_TEXT`；`length` 是字节数。`SCENE_TEXT.text` 最多容纳 31 字节，长句应拆成多个命令。GB2312 以外的生僻字、表情符号仍可能显示替代字形。

应用可用 `platform_image(x,y,width,height,pixels)` 画 RGB565 不透明图片。像素是按行排列的小端 `uint16_t`；每边最多 128 px、每次回调最多 8 张。先调用 `platform_scene` 创建帧画布，再调用 `platform_image` 叠加图片；无帧画布时返回 `-1`。坐标和图片必须完整落在画布内，缓冲区必须位于 WASM 内存；无权限、越界或超配额会停止应用。生成嵌入式头文件：

```bash
python tools/image_to_c.py artwork.png apps/my_app/artwork.h --name artwork --background 0x1b2a24
```

需要 Pillow（`python -m pip install Pillow`）。透明像素按 `--background` 合成。将 PNG 和生成的头文件放进源码目录，`#include "artwork.h"` 后调用 `platform_image(10,10,ARTWORK_WIDTH,ARTWORK_HEIGHT,artwork_pixels)`。图片数据编进 WASM，不需要改变 `.app` 包格式；仍受 256 KiB WASM 上限约束。

## 传感器和电量

Manifest 权限 `sensors`；每次 `app_init` / `app_event` 最多 4 次传感器读取。函数成功返回结构字节数，外设不可用或读取失败返回 `-1`，权限/缓冲区错误会停止应用。

| 函数 | 输出 | 单位 |
| --- | --- | --- |
| `platform_imu(&sample,sizeof(sample))` | `platform_imu_sample_t`，28 字节 | 加速度 `ax_mg` 等：毫 g；角速度 `gx_mdps` 等：毫度/秒；`sampled_ms`：采样时间 |
| `platform_orientation(&sample,sizeof(sample))` | `platform_orientation_t`，20 字节 | `roll_md` 横滚、`pitch_md` 俯仰：毫度；`heading_md` 未校准磁航向：毫度，磁力计缺失为 `-1`；`flags` bit 0/1 标记倾角/航向有效 |
| `platform_magnet(0,&sample,sizeof(sample))` | `platform_magnet_sample_t`，16 字节 | `x_ut100` 等：微特斯拉 × 100；两个传感器编号为 0、1 |
| `platform_battery(&sample,sizeof(sample))` | `platform_battery_sample_t`，16 字节 | `voltage_mv`：毫伏；`current_ma`：毫安；`percent`：0–100 |

Mosaico 对短时间重复读取使用缓存：IMU 40 ms、磁力计 50 ms、电量 500 ms；`sampled_ms` 是实际 IMU/磁力计采样时刻。姿态角由重力方向估计，设备快速运动时会偏移；航向由磁力计估计，未做硬铁/软铁校准，不宜用于精密导航。

先用 `platform_hardware()` 检查 `PLATFORM_HW_IMU`、`PLATFORM_HW_MAGNETOMETER`、`PLATFORM_HW_BATTERY`。Mosaico 会在启动时探测实际器件，仅把初始化成功的能力设为可用。Korvo-1 当前未装这些器件，相应位不置位，调用返回 `-1`。需要双板兼容时，将它们写进 `optional_hardware`，提供触摸等备用交互。传感器数据仅用于应用本地逻辑，平台不授予原始 I²C 总线访问。

## 震动与状态 LED

Manifest 权限 `feedback`。`platform_feedback(kind,intensity,duration_ms)` 中 `kind=1` 是震动马达，`kind=2` 是状态 LED；`intensity` 为 0–100，持续时间为 0–1000 ms（0 表示停止）。每次回调最多 4 次；设备不可用返回 `-1`，参数错误返回 `-2`，成功返回 `0`。应用停止时平台会关闭马达和 LED，最长一次持续 1 秒。先检查 `PLATFORM_HW_HAPTIC` 或 `PLATFORM_HW_LED`；Korvo-1 返回不可用。LED 的实际亮度是开/关，`intensity` 只用于 0 与非 0 的判断。Mosaico 某些硬件版本可能未装状态 LED；能力位只能说明 GPIO 输出已初始化，不能检测灯珠是否实际装配。应用应把 LED 作为可选体验。

## 工作台预览

电脑预览页可输入加速度、磁场和电量，模拟按下、移动、长按、滑动，并显示震动/LED 调用。`模拟外设` 控制可用能力位；这是模拟值，真机是否可用以 `platform_hardware()` 为准。图片、权限与调用次数也在浏览器预览中检查。最终行为仍应在目标板卡上验证。

## Manifest 示例

```json
{
  "permissions": ["display", "touch", "sensors", "feedback"],
  "hardware": ["display", "touch"],
  "optional_hardware": ["imu", "magnetometer", "battery", "haptic", "led"]
}
```
