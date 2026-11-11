# 应用外设依赖

平台 0.4.8 起，应用包可以区分必需外设和可选外设。判断发生在签名校验完成后、WASM 加载和 `app_init()` 执行前。

```json
{
  "hardware": ["display", "touch"],
  "optional_hardware": ["speaker", "motion", "buttons", "wifi"]
}
```

- `hardware`：必需外设。设备缺少任意一项，平台返回首页并提示“无法启动：缺少必要外设”，应用代码不会执行。
- `optional_hardware`：可选外设。设备缺少时仍启动应用，并显示 5 秒提示；应用应隐藏相关入口或使用替代行为。
- 旧版已签名包没有 `optional_hardware` 时按空数组处理，因此无需重新签名即可继续安装和运行。
- 同一外设不能同时出现在两个数组中，未知名称和重复项会在打包或设备验签时被拒绝。

支持的标识为 `display`、`touch`、`speaker`、`motion`、`buttons`、`wifi`、`camera`、`microphone`、`temperature`、`humidity`、`light`、`imu`、`magnetometer`、`battery`、`haptic`、`led`。

当前 ESP-Mosaico 固定提供屏幕、触摸和 Wi-Fi。扬声器在音频任务成功建立后可用；Interaction V0.2 扩展板初始化成功后提供人体感应与两个按键。0.5.0 起，IMU、两颗磁力计、电池计、震动马达和状态 LED 在初始化成功后会单独宣告可用；传感器未装配或初始化失败则不宣告；LED 只能检测 GPIO 初始化，无法自动判断物理灯珠是否装配，应用应将它列为可选。摄像头、麦克风、温湿度和光照仍未开放。

应用可在运行中查询能力：

```c
uint32_t available = platform_hardware();
if (available & PLATFORM_HW_SPEAKER) {
    platform_audio(2, 4);
}
```

Rust SDK 对应 `miniapp::available_hardware()` 与 `HW_*` 常量。查询结果是能力位图，不授予权限；调用音频、输入等宿主接口仍必须在 `permissions` 中声明相应权限。

串口命令 `hardware` 会打印设备当前宣告的能力，便于装配和售后诊断。

Pocket Garden 0.1.5 的屏幕和触摸是必需项；扬声器、人体感应、按键和 Wi-Fi 是可选项。这样没有扩展板或暂时未联网时仍能进入生态舱，只是相应互动会降级。

0.5.0 新增的传感器和反馈能力详见 [第一批开放能力](FIRST_BATCH_CAPABILITIES.md)。
