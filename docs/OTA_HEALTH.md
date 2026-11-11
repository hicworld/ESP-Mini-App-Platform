# OTA 启动健康检查（0.3.3）

0.3.3 在 HTTPS 固件下载前执行 `ota_preflight`：当前 OTA 镜像必须已确认；平台检查当前应用存储容量、逐个验签并核对安装记录；NAND 可用时还会写入、回读并删除临时测试文件。任一检查失败时不会开始写入备用 OTA 槽。串口可用 `ota_check` 单独运行相同检查。

新镜像处于 ESP-IDF `PENDING_VERIFY` 状态时，启动还需满足应用注册表可用、网络模块初始化成功及上述本地检查，才调用确认接口。这里不要求 Wi-Fi 当前联网，避免路由器暂时不可用时把健康固件错误回滚。检查失败会请求启动器回滚；若回滚接口本身失败，则明确报错。强制回滚测试镜像仍保留独立测试开关。

验证边界：

- 主机测试编译实际 `firmware/main/ota_health.c`，模拟成功和存储、应用、NAND、确认及回滚接口失败：正常分支 23/23、强制回滚分支 11/11。
- 备用槽的 USB 写入只是启动验证。只有设备实际报告新版本和 `ota_state=2`，并保留应用与 NAND 数据，才能声称板上确认通过。
- HTTPS 下载、TLS、云端固件发布、传输中断和真实断电仍需各自验证；本阶段的 USB 槽注入不代表这些路径已通过。

2026-09-23 的备用槽写入曾因 USB 消失而无法确认。2026-09-24 重新连接后的只读诊断确认设备实际恢复为厂商 `edge_agent` 0.2.1 及单 factory 分区布局，因此没有继续修改旧启动记录，而是完整写入项目 bootloader、双 OTA 分区表、初始 OTA 数据和 0.3.3。最终设备报告 `version=0.3.3 partition=ota_0 ota_state=2`；NAND 恢复后应用检查 3/3、OTA 预检通过，三个应用和 23/23 自检在重启前后通过。证据见 `local/device-0.3.3-final-acceptance.log`。

本地发布文件位于 `dist/ota/esp_mini_platform-0.3.3.bin`，旁边的 `.sha256` 和 `.plan.json` 记录大小、摘要和拟上传 URL。计划文件中的 `published`、`device_ota_verified` 均为 `false`：完整 USB 烧录已验证，但尚未上传公网，也尚未完成 HTTPS OTA 换槽验收。

当前板上基线为 0.3.3/`ota_0`、valid、NAND 已挂载且应用 3/3。后续 HTTPS OTA 验收应以此状态为升级前基线，安装到 `ota_1` 后再次运行 `status`、`list`、`apps_check`、`nand`、`cache`、`ota_check` 和 `selftest`，并重启复查。
