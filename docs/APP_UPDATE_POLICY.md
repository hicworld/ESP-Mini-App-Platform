# 应用更新策略

0.3.1 候选版在刷新在线目录后，将每个目录项与当前已安装的同 ID 应用比较，并在 UI、串口列表和安装入口使用同一套判断。

| 目录状态 | 界面按钮 | `store_list` | 在线安装 |
| --- | --- | --- | --- |
| 未安装同 ID 应用 | Install | `action=Install` | 允许 |
| 目录版本更高 | Update | `action=Update` | 允许 |
| 版本相同 | Reinstall | `action=Reinstall` | 允许 |
| 目录版本更低 | Older（禁用） | `action=Older` | 下载前返回 `ESP_ERR_INVALID_VERSION` |

版本必须是 `X.Y.Z` 三个十进制字段，按数值逐段比较。因此 `0.10.0` 高于 `0.9.0`，不会按字符串顺序误判。目录解析仍会拒绝格式错误的版本。

该策略只阻止在线目录意外降级。USB 安装和显式 HTTPS 包地址仍保留给受信任开发者做恢复、测试和有意回退；两条路径都继续执行包签名、负载哈希、Manifest 和应用身份校验。

## 当前验证

- 固件目录解析自检覆盖正常目录、8 类拒绝输入和四种更新状态。
- 主机测试直接执行固件 `store.c`，验证 Install、Update、Reinstall、Older，以及旧版本在任何应用下载请求之前被拒绝。
- 完整主机回归记录：`local/test-update-policy-0.3.1.log`。
- ESP-IDF 6.1 构建与合并记录：`local/build-update-policy-0.3.1.log`。
- 0.3.1 已烧录到开发板。实机验证了 Reinstall、较低已安装版本对应 Update、较高已安装版本对应 Older、`ESP_ERR_INVALID_VERSION` 下载前拒绝、缓存保持为空和原包恢复；记录为 `local/device-update-policy-0.3.1-final.log`。
- 最终重启后 COS 三项均为 Reinstall，三个应用版本和集合保持不变。用户已在屏幕上确认当前 Reinstall 列表、页面布局和真实手指触摸无异常；Update/Older 的专用屏幕状态未在最终三项均为 Reinstall 的现场状态下重复构造。

以后更新固件或目录后，可重复运行：

```powershell
python tools/device_update_policy_acceptance.py --port COM4 --output local/device-update-policy-0.3.1.log
```

脚本要求目标示例的已安装版本、公开目录版本和本地恢复包版本起初一致。它临时安装一个较低的签名版本以验证 Update，再安装一个较高的签名版本以验证 Older 和下载前拒绝，最后通过 USB 恢复原包。若中途失败，脚本会尽力恢复原包并保留完整日志。当前 Reinstall 列表和真实手指操作已经人工确认；如需发布级截图，再单独构造 Update/Older 屏幕状态。
