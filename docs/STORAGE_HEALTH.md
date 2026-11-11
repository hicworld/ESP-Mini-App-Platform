# 存储健康检查与回退边界（0.3.2）

当前应用包优先使用 NAND。NOR 中的 SPIFFS 是旧版本兼容存储，未挂载不等于 NAND 应用不可用，也不代表旧副本存在。

## 诊断命令

- `storage`：输出 NOR 挂载错误、分区地址及大小。未挂载时只读扫描分区，`erased=1` 表示全部字节为 0xff，`erased=0` 表示存在数据；后者不能单独判断为文件系统损坏。不会自动格式化。
- `nand`：输出 NAND 挂载状态和容量。
- `apps_check`：逐项读取应用包、验签并核对 Registry 中的 ID、名称、版本、权限和 WASM 大小；任何一项失败，整体返回失败。不启动应用，不修改包或 Registry。
- `apps_migrate`：已有 NAND 包符合记录时保持不变；否则只有挂载的 NOR 中存在验签及身份匹配的包才可迁移。`legacy_verified` 只统计实际通过校验的旧副本，`legacy_mounted` 表示 NOR 是否挂载。

`status` 的历史字段 `storage=0` 仅表示 NOR 未挂载。须结合 `nand=1` 和 `apps_check` 判断当前应用是否健康。

## 读取规则

1. NAND 可用时优先读取对应文件，校验签名与 Registry 身份字段。
2. NAND 文件缺失或 NAND 未挂载时，仅允许从已挂载的 NOR 读取符合相同校验条件的副本。
3. NAND 文件存在但读取失败、验签失败或身份不符时直接报告错误，不隐藏为旧包回退。
4. 已挂载 NOR 上的旧版本即使签名有效，也不能冒充 Registry 记录的新版本运行。

Registry schema 仍为 1，不持久化包的完整摘要。因此当前身份核对不是“同版本不同构建”的逐字节一致性保证，也不提供完整的防重放机制。

## 验证

主机存储套件 48/48，新增覆盖匹配旧包回退、版本/ID/权限不匹配、两个后端不可用、损坏 NAND 包不能被旧包掩盖、NAND 单独可用等场景。主机测试使用真实事务代码和临时文件，NVS 与验签器为测试替身，不能代替板上验签或硬件断电测试。

板上验收入口：

```powershell
python tools/device_storage_health_acceptance.py --port COM4 --output local/device-storage-health-0.3.2.log
```

该脚本检查诊断、应用验签、三个应用启动/停止、自检及重启保持；不格式化、不安装或卸载应用。自检会使用并清理专用测试数据。

## 本板诊断结果（2026-09-22）

0.3.2 应用镜像写入 `ota_0`，数据 Hash 校验通过。`storage` 报告 NOR 挂载结果 `ESP_FAIL`，分区偏移 `0xa20000`、大小 6,160,384 字节，`erased=0 first_nonblank=0x0`。这证明分区并非全擦除状态；仅凭该结果不能确定历史失效原因。没有格式化 NOR，也没有将原始内容作为有效旧副本使用。

NAND 挂载正常，`apps_check` 为 3/3，迁移报告 `copied=0 already=3 legacy_verified=0 legacy_mounted=0`。诊断记录：`local/device-storage-probe-0.3.2.log`。

完整板测记录 `local/device-storage-health-0.3.2.log`：重启前后包检查均为 3/3，Hello、Touch、Snake 启动/停止均通过，两次自检均为 23/23。在线更新回归记录 `local/device-update-policy-0.3.2.log`：Update、Reinstall、Older 下载前拒绝通过，原包恢复为 0.1.0。
