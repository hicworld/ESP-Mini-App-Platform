# OTA 部署与验收

## 发布流程

板上“设置 → 系统更新”从 COS 的 `system/latest.json` 读取已发布版本，界面只允许安装比当前版本新的镜像。仓库中的 `deploy/system/latest.json` 目前指向已验收的 0.4.9，该指针已上传且公网 GET 返回 200。0.4.11 是本地开发板修正版，尚未作为线上更新发布。以后发布新固件时，先上传并核对版本化镜像，再运行 `python tools/publish_system_index.py --dry-run` 预览，确认后运行 `python tools/publish_system_index.py` 上传指针并从公网核对；该命令同样读取 `TENCENTCLOUD_SECRET_ID` 和 `TENCENTCLOUD_SECRET_KEY` 环境变量。先完成下述发布与设备验收，再修改 JSON 中的 `version` 和 `file` 并上传指针。不要把尚未验收的镜像设为最新版本。更新界面会在下载前核对镜像的项目名和版本；安装成功后自动重启，由现有 OTA 启动健康检查确认或回滚。

上传 COS 过去是手工操作，现已脚本化为三步（暂存 → 打包 → 上传核对）。全程不接触设备，可随时中断；只有最后的设备验收才会改变板子状态。

1. **升版本号**：`firmware/main/platform.h` 的 `PLATFORM_VERSION` 与 `Run.ps1` 的 `$ProjectVersion` 必须一致，`stage` 会校验两者，不一致直接失败。
2. **构建**：`./Run.ps1 Build`。产物复制到 `dist/firmware` 并生成 `SHA256SUMS.txt`。
3. **暂存**：`python tools/stage_ota_release.py --version X.Y.Z --base-url https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com`（默认前缀为 `/firmware`，历史版本在桶根目录，发布时传根 URL 保持一致）。
4. **打包**：`python tools/make_release_bundle.py --version X.Y.Z --notes local/release-notes-X.Y.Z.md`。生成 `SHA256SUMS-X.Y.Z.txt`、`RELEASE-X.Y.Z.txt` 与 `ota-deploy-X.Y.Z.zip`，并重新打开压缩包核对固件与独立文件逐字节一致。
5. **上传**：设置 `TENCENTCLOUD_SECRET_ID` / `TENCENTCLOUD_SECRET_KEY` 后运行 `python tools/publish_cos.py --version X.Y.Z`。脚本上传后**自动从公网重新下载逐字节核对**，并把结果写入 `local/cos-X.Y.Z-verification.json`。凭据只从环境变量读取，不落盘、不进日志。

辅助命令：

```powershell
python tools/publish_cos.py --version X.Y.Z --dry-run     # 只打印上传计划，不联网
python tools/publish_cos.py --verify-only --version X.Y.Z  # 只做公网核对，无需凭据
python tools/publish_cos.py --verify-only --version X.Y.Z --output local/cos-X.Y.Z-verification.json
python tools/publish_cos.py --verify-only --url <URL> --sha256 <摘要>
```

`--verify-only` 不需要凭据（存储桶公有读），是随时复查线上对象是否仍与本地一致的手段。它同时拒绝重定向与缺失的 `Content-Length`：设备期望的是明确的 200 与确定长度，这两者即使字节碰巧相同也算发布缺陷。

> 发布不等于上线。上传并核对后，仍需在测试设备上确认 `status` 中的版本、分区与 `ota_state=2`，才能称该版本已验收。

## 当前 COS 已发布版本：0.4.9（在线 OTA 与回滚验收完成）

- `dist/ota/esp_mini_platform-0.4.9.bin`：2,105,264 字节，SHA-256 为 `5cc83cbb2ac4c617382fb6a2f07285dbb73a32c50cef17abba824a98edcc63f7`。
- `dist/ota/SHA256SUMS-0.4.9.txt`：版本化校验清单，SHA-256 为 `d41e14500fb43da015e3b921d451b88428255c3df8d97c534b896f51613bd16b`。
- `dist/ota-deploy-0.4.9.zip`：1,323,415 字节，SHA-256 为 `e7485e9eecf7fddabfe93012ee9053aab1cc92e5b624e3cfc7f653a6bc0c3521`。

三个文件已上传到 COS 桶根目录；`publish_cos.py --verify-only` 从公网重新下载并检查 HTTP 200、明确 Content-Length、长度和 SHA-256，3/3 与本地逐字节一致。公网核对记录为 `local/cos-0.4.9-verification.json`，SHA-256 `70f4d172b6d7c3a20635a6c10803aa90e309480a23bd50746f9bd7dae599374f`。

开发板从 `0.4.9 / ota_0` 通过 HTTPS 写入并启动 `0.4.9 / ota_1`，启动健康检查完成六应用验签与 NAND 读写后确认镜像；板上自检 26/26。随后 0.2.5 故障镜像写入 `ota_0` 并触发自动回滚，最终状态为 `version=0.4.9 partition=ota_1 ota_state=2 previous=ota:forced-rollback phase=ready`。六个应用 6/6、在线目录、缓存和中文设置均保留。日志 `local/device-ota-0.4.9-acceptance.log`，SHA-256 `600a56f72cc0934115ab20f935ae0a4c599567d952b53a8d59f9c9fa30ef4426`。

## 0.4.6 本地候选发布

- `dist/ota/esp_mini_platform-0.4.6.bin`：2,103,504 字节，SHA-256 为 `f208d9d3339fe7ce958d2f55f4757c9853a06d37e9a3e54a3e766a1fa1ec30ec`。
- `dist/ota/SHA256SUMS-0.4.6.txt`：版本化校验清单。
- `dist/ota-deploy-0.4.6.zip`：1,322,082 字节，SHA-256 为 `839a1ce604fff09a6bbec66bdca0c934955f1afcbca7272f51cc5fd984187e33`。

压缩包已在本机重新打开，内部固件长度和 SHA-256 与独立文件一致。0.4.6 已完成完整直刷、26/26 板上自检、中文 / English 切换和重启持久化验收；尚未上传 COS，也未执行该版本的设备在线 OTA。

## 0.4.5 历史发布（未执行该版本在线 OTA 验收）

- `dist/ota/esp_mini_platform-0.4.5.bin`：2,091,264 字节，SHA-256 为 `d0531ea6bb0a155c17e826aa4ff1a8aae6c5b63c7756033dce662ed2d4058edc`。
- 固件：`https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/esp_mini_platform-0.4.5.bin`
- 校验清单：`https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/SHA256SUMS-0.4.5.txt`
- 发布包：`https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/ota-deploy-0.4.5.zip`
- 发布包：1,315,225 字节，SHA-256 为 `d97479455e211665902fe94507fcf62dc21cd44102a118ae214dff28cce09f8e`。

以上三个文件已于 2026-09-29 由用户通过腾讯云控制台上传到 COS 桶根目录。随后运行 `python tools/publish_cos.py --version 0.4.5 --verify-only`，从公网重新下载的 3/3 对象与本地文件逐字节一致；日志为 `local/cos-0.4.5-verification.log`，日志 SHA-256 为 `c059df522d4f06671fd972daf18d5bf777df806a82196cac26d042f8f9f3df0b`。

0.4.5 已完成直刷、26/26 板上自检和主机完整回归，但没有从上述公网 URL 执行该版本的设备在线 OTA、换槽确认与回滚验收。后续 0.4.9 已完成整套公网 OTA/回滚验收。

## 0.3.0 本地候选发布

- `dist/ota/esp_mini_platform-0.3.0.bin`：1,934,512 字节，SHA-256 为 `3204067309de612f9fc20a1a9219ec5f9f189f4914c4cae5d27755c622ce966f`。
- `dist/ota/SHA256SUMS-0.3.0.txt`：版本化校验清单。
- `dist/ota-deploy-0.3.0.zip`：1,226,578 字节，包含固件、清单与 `RELEASE.txt`；压缩包 SHA-256 为 `d5ac1e2aebe2274876b0e46e1046f33ba1f7d2e673251ae2939c67d60dd919ca`。

压缩包已在本机重新打开，内部固件长度和 SHA-256 与独立文件一致。0.3.0 已完成直刷、NAND HTTPS 下载缓存、COS 正常安装、404 失败清理、Snake 运行、重启恢复和 23/23 板上自检；尚未上传 COS，也未执行该版本的在线 OTA/回滚验收，因此当前公网正式版仍为 0.2.7。

## 0.2.9 历史本地候选

- `dist/ota/esp_mini_platform-0.2.9.bin`：1,932,192 字节，SHA-256 为 `7d82b550c70cfe9e5dd999efb10220438c75a8426834e6723ca7437d2544bb5a`。
- `dist/ota/SHA256SUMS-0.2.9.txt`：版本化校验清单。
- `dist/ota-deploy-0.2.9.zip`：1,246,500 字节，包含固件、清单与 `RELEASE.txt`；压缩包 SHA-256 为 `590139b39911531526a5379e87ef06a71198edf55fd6d55848bbabae1410f4c9`。

压缩包已在本机重新打开，内部固件长度和 SHA-256 与独立文件一致。0.2.9 已完成直刷、NAND 应用迁移、三应用启动、Snake 更新、重启保持和 23/23 板上自检；尚未上传 COS，也未执行公网文件核验和在线 OTA/回滚验收，因此当前公网正式版仍为 0.2.7。

## 0.2.7 历史发布（已完成在线 OTA/回滚验收）

- `dist/ota/esp_mini_platform-0.2.7.bin`：1,891,120 字节，SHA-256 为 `658abb41d21e65a737ba67e748171535fceb53bd0d4ddcd35a773bf25c037826`。
- 固件：`https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/esp_mini_platform-0.2.7.bin`
- 校验清单：`https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/SHA256SUMS-0.2.7.txt`
- 发布包：`https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/ota-deploy-0.2.7.zip`

以上三个文件已在 2026-09-17 上传 COS，并从公网重新下载逐字节核对通过，记录见 `local/cos-0.2.7-verification.json`。0.2.7 已完成直刷、23/23 板上自检、数据清除命令、完整设备生命周期，以及在线 OTA 升级/回滚验收。

设备从 0.2.7/`ota_0` 在线换槽到 0.2.7/`ota_1` 并确认，23/23 自检通过；随后安装 0.2.5 故障镜像，自动回滚至 0.2.7/`ota_1`。最终 `previous=ota:forced-rollback phase=ready`，Hello、Touch Paint、Snake 的完整列表与在线目录均保留。记录：`local/device-ota-0.2.7-acceptance.log`。

```powershell
python tools/device_ota_acceptance.py --port COM4 `
  --production-url https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/esp_mini_platform-0.2.7.bin `
  --rollback-url https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/esp_mini_platform-0.2.5-rollback-test.bin `
  --expected-version 0.2.7 `
  --output local/device-ota-0.2.7-acceptance.log
```

## 0.2.6 历史发布

0.2.6 发布产物：

- `dist/ota/esp_mini_platform-0.2.6.bin`：1,889,648 字节。已通过烧录 Hash、22/22 板上自检、三应用安装/篡改拒绝/重启保留，以及 COS 目录刷新和安装。
- SHA-256：`879fd9e57725dcfd809a0e1e5746de8656f5090760370c849b78ffa23833661e`
- 固件：`https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/esp_mini_platform-0.2.6.bin`
- 校验清单：`https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/SHA256SUMS-0.2.6.txt`
- 发布包：`https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/ota-deploy-0.2.6.zip`

以上三个文件已在 2026-09-17 上传 COS，重新下载逐字节核对通过，记录见 `local/cos-0.2.6-verification.json`。发布包只有正式固件、清单、发布说明，不含回滚测试固件。包内说明在 OTA 验收前生成，最终验收状态以本文件和设备日志为准。

回滚验收复用已发布的故障镜像：

- `dist/ota/esp_mini_platform-0.2.5-rollback-test.bin`
- SHA-256：`1f769390d61188260c03ffedf97392397d90dc067df402ce0fefb1f2c213e530`
- URL：`https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/esp_mini_platform-0.2.5-rollback-test.bin`

该镜像启用了 `CONFIG_PLATFORM_OTA_TEST_FAIL_BOOT`，在待确认状态主动标记镜像无效并重启，只用于验证回到先前已确认的固件。这里复用较低版本的测试镜像，并非测试防降级机制；当前未开启 Secure Boot/eFuse 防降级。不得作为生产固件推送。此镜像也已重新下载核对。

腾讯 COS 使用公有读、私有写。设备使用 ESP-IDF CA 证书包验证 TLS；本次公网对象返回 HTTP 200、明确的 Content-Length、无重定向。原 0.2.4 发布文件继续保留。

2026-09-17 的 0.2.6 验收已通过：通过 HTTPS 从 `ota_0` 换槽到 `ota_1` 并确认（`ota_state=2`），自检 22/22；随后安装 0.2.5 故障测试镜像，自动回滚至 0.2.6/`ota_1`。最终 `previous=ota:forced-rollback phase=ready`，Hello、Touch Paint、Snake 的完整列表在两个阶段均与基线一致，在线目录刷新成功。记录：`local/device-ota-0.2.6-acceptance.log`。

完整真机验收命令：

```powershell
python tools/device_ota_acceptance.py --port COM4 `
  --production-url https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/esp_mini_platform-0.2.6.bin `
  --rollback-url https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/esp_mini_platform-0.2.5-rollback-test.bin `
  --expected-version 0.2.6 `
  --output local/device-ota-0.2.6-acceptance.log
```

2026-09-16 的 0.2.4 OTA 升级/回滚实机验收已通过：设备从 0.2.4/`ota_0` 升级到 0.2.4/`ota_1`，22/22 自检通过；随后启动 0.2.5 测试镜像并自动回滚到 0.2.4/`ota_1`。回滚后在线目录仍可刷新，Hello、Touch Paint、Snake 三个应用均保留。完整记录见 `local/device-ota-0.2.4-acceptance.log`。COS 上的固件、校验清单和版本化部署包均已重新下载核对。

正常生产发布只需要上传新的正式镜像并执行 `ota HTTPS_URL`。每次发布都应先校验公网文件 SHA-256，并在测试设备上确认 `status` 中的版本、分区和 `ota_state=2`。不要把回滚测试配置合入正式构建。

在发布前可用已公开的应用索引和一个不存在的对象验证失败路径：

```powershell
python tools/device_ota_failure_acceptance.py --port COM4 `
  --invalid-image-url https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/store-deploy/index.json `
  --missing-image-url https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/does-not-exist-ota-test.bin `
  --output local/device-ota-failure-0.2.6.log
```

两次请求都应返回错误，设备不得重启、切换 OTA 槽或改变应用列表；0.2.6 实机验收通过并记录了 `phase=ota:failed`，日志见 `local/device-ota-failure-0.2.6.log`。
