# 风来小院 / Pocket Garden

2026-09-28，用户确认 UI 后开始实现。应用 ID `com.example.garden`，当前版本 0.1.3，最低平台 0.4.4。这是独立签名 WASM 应用；0.4.0 增加 scene/environment/input 接口，0.4.1 增加 settings 接口，0.4.3 增加受限 audio 接口，0.4.4 提高可用响度范围。

## 场景与交互

- 奶油色天空、草地、池塘、木屋、小兔、小猫；中文按钮与提示。
- 网络时间默认 UTC+8；设置城市后使用该城市配置的时区偏移。没有校时显示 `--:--`。
- 当地室外温度、WMO 天气、日出日落由系统后台获取；没有数据显示缺失，绝不回填预览样例。
- PIR 检测移动后：约 5 秒微风，植物摇摆、叶片飘动、动物耳朵/尾巴回应；约 4 秒抬头。
- A 键主动呼唤/起风，B 键投喂。按键 40 ms 去抖，仅按下沿触发。
- 摸摸：闭眼蹭动、身体轻晃、爱心；喂食：低头咀嚼，食物逐渐减少；来访：抬头停留后放下，配合微风。点击底部按钮立即触发，摸摸模式也可直接触摸动物。
- 浇水按钮立即显示水壶、下落水滴、湿润地面和花朵轻动；再点植物可改变浇水位置，不需要先命中窄草地区域。该效果持续约 4 秒，暂不保存长期植物状态。点天空可手动起风。
- 点应用顶部显示天气详情，其中“联网与城市”打开系统配置；其他位置轻点关闭。平台 Home 保持在应用画布之外。
- 5 FPS 更新目标，仍有每入口 50,000 条 WASM 指令和 4 页线性内存上限；实机帧率须单独测量。
- 0.1.3 包含白天/夜晚两段合成背景音乐及摸摸/喂食/浇水/来访/起风音效；天气详情可关闭音乐、调音量或静音，并提高了整体响度。详见 [GARDEN_AUDIO.md](GARDEN_AUDIO.md)。
- 当前没有养成存档、动物自主寻路、灯光、天气预警或重计算功能。

## 硬件依据

用户提供的 `SCH_SCH_ESP-Mosaico_Interaction_V0_2_2026-09-10.pdf`：

| 信号 | 左侧插槽 | 右侧插槽（当前照片） | 用法 |
|---|---|---|---|
| AS312 PIR_DAT | GPIO4 | GPIO5 | 输入、高电平活动 |
| SW1 KEY_L | GPIO13 | GPIO11 | 输入上拉、按下接地 |
| SW2 KEY_R | GPIO12 | GPIO10 | 输入上拉、按下接地 |

使用 `bsp_subboard_map_gpio()` 映射，不改 EEPROM。不初始化光敏、RGB、红外或音频，不借用旧版按键灯板驱动。两个按键按用户提供的 SW1/SW2 原理图按机械开关实现；之前 DeskSprite 工程中的电容触摸驱动不是本次依据。

`CONFIG_PLATFORM_INTERACTION_V02=y` 启用该板，`CONFIG_PLATFORM_INTERACTION_LEFT=y` 改为左槽。默认 Kconfig 不启用；本机已按用户照片启用右槽。

PIR 启动等待 30 秒，活动上升沿最短间隔 10 秒。暖机期间的高电平不会作为新到访，需先恢复低电平再出现新上升沿。PIR 只能反映活动，不能确定朝向或持续静坐存在。

## 天气配置与边界

设备端入口：`Home → Settings → Wi-Fi / Weather`，或生态舱顶部天气详情 → **联网与城市**。进入配置会停止当前应用，配置完成后从 Home 重新打开。

0.4.2 起，Wi-Fi 页打开即异步扫描附近网络，显示最多 20 个去重后的网络及信号强弱，支持刷新。点选网络名称进入连接页，加密网络自动展开屏幕键盘和密码框；输入 8–63 字节密码后点“连接”。开放网络无需密码，仍须用户点击连接。暂不支持企业认证、WEP 或门户网页登录，也不展示隐藏 SSID。列表保留实际 SSID 字节用于连接；字体子集未覆盖的中文 SSID 字符可能显示缺字。

输入框固定为白底深色字、44 像素高、有边框；键盘明确使用顶部坐标定位，避免 LVGL 默认底部对齐叠加坐标导致落在屏幕外。密码默认隐藏，可用“显示/隐藏”按钮查看，提交或离开后清空。Wi-Fi 凭据由系统处理，不传入 WASM；成功配置后重启自动连接。

天气城市页提供广州、深圳、北京、上海、杭州、成都，以及自定义经纬度/英文名称。屏幕配置统一使用 UTC+8；其他时区可使用下列串口命令。城市坐标为城市中心近似值，不代表室内温度。不会自动替用户选择城市。

串口命令：

```text
location LATITUDE LONGITUDE UTC_OFFSET_MINUTES ASCII_CITY_NAME
environment
inputs
wifi_scan
wifi_list
input motion
input a
input b
run com.example.garden
stop
```

例如广州配置（仅示例，不自动选择用户城市）：

```text
location 23.1291 113.2644 480 Guangzhou
```

城市名目前支持 1–31 个英文字母、下划线或短横线。经纬度与时差持久化到 NVS；不做 IP 定位。UTC 偏移不自动处理夏令时，搬家/夏令时变更需重配。

数据源为 Open-Meteo Forecast API（https://open-meteo.com/en/docs）。固定 HTTPS 端点，证书校验开启、不跟随重定向、最多 8 KiB 响应。后台任务成功后 15 分钟刷新，失败后 2 分钟重试，不占用 WASM 调用线程；配置变化时旧请求不能覆盖新城市数据。

查询温度、天气码、is_day、日出日落。天气时间戳需接近已校准系统时钟；1 小时以上显示缓存待更新，24 小时以上不再作为有效温度。无有效昼夜信息保留中性白天色板，并在详情页明确提示未知。极昼/极夜产生缺失日出日落时本版拒绝该次更新；需后续增强。设备所在网络对该服务的可达性尚须实测。正式商业发布前需按服务条款选用相应方案并保留来源署名。

## 平台扩展

- `scene(commands,count,background)`：`display` 权限；一次提交完整帧，每入口仅一次且不能与旧绘图 API 混用；最多 96 条、总包围盒 1,000,000 像素。固定 64 字节指令，无宿主指针，整个批次复制校验后绘制。
- `environment(out,capacity)`：`environment` 权限；复制 56 字节只读快照。WASM 不能指定远端 URL、改城市或读取原始 GPIO。
- 事件 3：`input` 权限；参数 a=1 PIR 活动、2 A 键、3 B 键，b 保留。
- `settings()`：仅请求打开宿主联网配置页面并停止应用，不授予读写网络凭据的能力；自 0.4.1 提供。
- 应用包验签、目录解析和权限详情页均识别新增权限。Garden 0.1.3 通过 `platform_min=0.4.4` 拒绝安装到音量范围不兼容的旧固件。
- 画面使用约 375–400 KiB（随行对齐变化） 的宿主 PSRAM RGB565 画布；应用仅保存有限图形指令。关闭/切换画布释放缓冲；不得据此宣称已实现强故障隔离。
- Noto Sans SC 字体子集（ASCII + 应用中文）按 OFL 分发，许可证 `GARDEN_FONT_OFL.txt`；字体源来自固定 LVGL 9.5.0 依赖。`tools/build_garden_font.mjs` 可重建。

## 构建与验证

```powershell
python tools/build_c_app.py apps/garden
node tools/garden_test.mjs
python tools/test_scene_host.py
python tools/test_network_ui.py
python tools/preview_garden.py
./Run.ps1 Test
./Run.ps1 Build
python tools/install_usb.py dist/garden.app --port COM4
```

`dist/garden-preview.html` 直接运行同一 `garden.wasm`，浏览器仅模拟 host 接口和数据；不是另一份场景业务实现。

电脑端已覆盖天气缺失/实时/过期、触摸、A/B、移动冷却、动画到期、毫秒回绕、动作姿态随时间变化、立即浇水和配置入口，共 985 个组合帧（最多 87/96 指令）。实际 C 渲染器通过无效参数、配额和 5,000 个带边界哨兵的形状测试。Node 测试不证明设备 WAMR 指令预算、显示性能或实际 GPIO 输入已通过；这些仍需实机验证。

`tools/test_network_ui.py` 使用实际产品界面 C 与 LVGL 9.5.0，在 WASI 主机上验证网络列表、密码框尺寸、键盘边界及真实键盘回调输入、密码显示/提交清空、城市设置、开放网络和空/失败扫描。PNG 截图写入 `local/network-ui-host`；仅无线网络后端被模拟，不能据此宣称真实 Wi-Fi 连接已验证。
