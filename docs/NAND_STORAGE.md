# NAND 存储

## 当前实现

平台 0.2.8 接入 ESP-Mosaico BSP 的 SPI NAND 驱动和 Espressif `spi_nand_flash_fatfs` 组件。实机已识别 121,325,568 字节的可用映射容量。2026-09-18 已完成首次 FAT 初始化；文件系统总容量为 121,159,680 字节。系统启动时只挂载已有 FAT 文件系统，挂载失败不会自动格式化、擦除或写入 NAND。

实机验收已覆盖初始化后的第一次读写、软件重启后的重新挂载与第二次读写、重复格式化拒绝、三个 NOR 应用保留，以及 23/23 平台自检。

挂载成功后平台确保以下目录存在：

```text
/nand/apps
/nand/appdata
/nand/cache
/nand/download
/nand/system
/nand/assets
/nand/temp
```

0.2.9 起，签名应用包优先由 `/nand/apps` 管理；现有活动包会自动复制并验签，旧 NOR 文件暂时保留。0.3.0 起，HTTPS 应用下载事务使用 `/nand/download`，失败和重启会清理未提交文件，详见 [DOWNLOAD_CACHE.md](DOWNLOAD_CACHE.md)。大型资源和应用文件数据仍待逐项迁移。

## 串口命令

```text
nand
nand_test
nand_format CONFIRM
cache
```

- `nand`：读取 NAND 几何信息；文件系统已挂载时同时显示总容量和剩余容量。没有 FAT 文件系统也会成功完成设备探测。
- `nand_test`：要求 NAND 已挂载。在 `/nand/temp` 写入 16 字节临时数据，关闭文件后重新打开，校验长度和内容，最后删除临时文件。
- `nand_format CONFIRM`：要求应用已停止且 NAND 尚未挂载。该命令为缺少或损坏文件系统的介质建立 FAT，原有文件系统内容会丢失，然后挂载并建立目录。已正常挂载时命令会拒绝执行，避免重复操作清空健康数据。
- `cache`：显示下载事务的 `app.part` 与 `app.ready` 是否存在以及各自字节数，不修改文件。

## 首次初始化验收

格式化是破坏性步骤，必须先确认外部 NAND 没有需要保留的数据。确认后依次执行：

```text
stop
nand_format CONFIRM
nand
nand_test
reboot
nand
nand_test
status
selftest
```

验收标准：格式化返回 `ESP_OK`；`nand` 显示 `mounted=1` 和 FAT 容量；两次 `nand_test` 均打印 `NAND SELFTEST PASS`；重启后仍可挂载；平台自检保持 23/23，三个现有应用仍可列出。

## 安全边界

- 正常启动永远使用 `format_if_mount_failed=false`。
- 格式化命令必须完整匹配 `nand_format CONFIRM`；运行中的应用或已正常挂载的 NAND 都会阻止操作。
- NOR 中的系统固件、NVS 与 `apps` 分区不属于外部 NAND 格式化范围。
- 应用包迁移和下载缓存已经使用 NAND；格式化仍只允许在 NAND 未挂载时显式执行，不会自动发生。
