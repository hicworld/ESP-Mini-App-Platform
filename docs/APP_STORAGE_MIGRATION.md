# 应用包 NAND 迁移

## 目标

平台 0.2.9 将签名 `.app` 包的主要存储从 16 MB NOR 中的 SPIFFS 迁移到外部 NAND FAT 文件系统。迁移不能让现有三个应用消失，也不能在复制成功前修改 NVS Registry。

## 兼容模型

Registry schema 保持版本 1，`app_info.path` 仍使用 `/store/APP_ID.SLOT.app`。该路径现在是稳定的逻辑标识：

- 0.2.9 将它映射为 `/nand/apps/APP_ID.SLOT.app` 并优先读取。
- NAND 文件不存在时回退读取原 `/store` SPIFFS 文件。0.3.2 起要求 NOR 已挂载，且旧包通过验签并与 Registry 的身份字段一致；版本不符的旧包不会启动。
- 旧固件仍能理解 Registry，不会因未知 schema 或 `/nand` 路径而拒绝启动。

首次启动和 `apps_migrate` 命令会遍历 Registry。每个当前活动包经过以下步骤：

1. 检查 NAND 目标是否已有签名及身份字段一致的包。
2. 如果没有，从 NOR 读取当前逻辑路径并完成签名与身份校验。
3. 写入 NAND，执行 flush、fsync、close。
4. 重新读取并与源包逐字节比较，再验签和比对 ID、名称、版本、权限、WASM 大小。
5. 保留 NOR 原文件，Registry 不变。

新安装和更新在 NAND 可用时直接使用 NAND 的双文件槽事务；NAND 不可用时才回退 NOR。卸载先提交 Registry，成功后再删除 NAND 和 NOR 的两个文件槽。

## 实机结果

0.2.9 首次启动后，`apps_migrate` 报告：

```text
APP MIGRATION PASS copied=0 already=3 legacy_preserved=3
```

这里 `already=3` 表示启动自动迁移已经完成。NAND 剩余容量由 121,044,992 字节变为 120,995,840 字节，与三个 16 KiB FAT 分配单元一致。Hello、Touch Paint、Snake 均从优先读取路径启动到 `runtime=2`。

重装 Snake 后，剩余容量变为 120,979,456 字节，随后迁移检查仍为 `copied=0 already=3`，证明更新已直接写入 NAND 的另一个槽。软件重启后三个应用、NAND 容量和迁移状态保持，板上自检为 23/23。

## 回滚边界

迁移时不删除 NOR 原件，因此升级前已有应用仍有旧固件可读取的副本。0.2.9 之后新安装或更新的包以 NAND 为准；它们不保证在不支持 NAND 的旧固件中呈现相同版本。系统 OTA 自动回滚应在安装或更新应用前完成验收。长期方案是在确认 NAND 版本稳定后升级 Registry schema，并提供显式导出或降级迁移工具。

## 后续工作

- HTTPS 应用下载已经在 0.3.0 改为 NAND 临时文件流式接收。
- 将大型共享资源放入 `/nand/assets`，并为应用增加受限文件数据目录。
- 做下载中断、写入中断、物理满盘和断电故障注入。
- 在稳定版本中决定何时回收 NOR 旧包和旧 `apps` 分区。

## 0.3.2 诊断修正

上面的 `legacy_preserved=3` 是 0.2.9 的历史输出。旧实现只打印 Registry 数量，不能据此证明旧副本仍存在。0.3.2 改为逐包验签并比对身份后输出 `legacy_verified`，同时输出 `legacy_mounted`。新增 `storage` 和 `apps_check` 诊断，详见 [STORAGE_HEALTH.md](STORAGE_HEALTH.md)。
