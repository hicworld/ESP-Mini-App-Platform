# ESP Mini App Platform

面向 ESP-Mosaico／ESP32-S31 的微型应用平台：系统固件负责界面、网络、存储和硬件能力，独立的 WebAssembly 应用通过受限 API 运行，并以签名 `.app` 包安装。项目目前用于学习、开发和原型验证，**不是通用 ESP32-S3 固件，也不是量产安全方案**。

> 这是公开的源码快照，不包含原项目的 Git 历史、设备日志、签名私钥或构建缓存。仓库**暂未授予项目整体的开源许可证**；公开可见不等于允许复制、修改或再分发。第三方组件与字体各自保留随附的许可证。许可状态如需变更，以后会在仓库中明确公布。

## 可以做什么

- 在 ESP-Mosaico 上安装、运行、更新和卸载独立 WASM 应用；在线目录通过 HTTPS 分发应用包。
- 用 C、C++ 或 Rust 编写应用，声明权限和外设，编译为受限 WASM。应用运行时不能直接访问原生 GPIO、任意网络地址或系统文件。
- 使用本地开发者工作台创建项目、编辑 Manifest、无签名构建、浏览器预览并导出**投稿 ZIP**。投稿不是自动上架；维护者复核、重新构建、测试并签名后才可发布。
- 查看 Hello WASM、Touch Paint、Snake、Pocket Garden 与 Bounce 等示例。Pocket Garden 的中文界面名为“风来小院”。

当前源码包含 0.4.9 发布后的继续开发内容；**0.4.9 Beta 是此前完成公开目录与 OTA 验收的基线**。源码中的更新和新增能力不等于已经发布或在所有设备上完成验收。应用目录与平台版本约束以具体 Manifest、固件和部署文档为准。

## 开发自己的应用：先用本地工作台

环境：Windows、Python 3.10+、Node.js。C/C++ 构建还需要 WASI SDK 34.0；Rust 需要 rustup 与 `wasm32-unknown-unknown` target。

```powershell
python -m pip install -r requirements-app-sdk.txt
./tools/setup_sdk.ps1
python tools/developer_studio.py
```

也可以双击 `启动本地工作台.cmd`，再按工作台的“检查开发环境”提示安装缺少的工具。工作台只监听本机；项目数据位于被 Git 忽略的 `local/developer-studio/`。预览是电脑模拟，不能代替真机验证。[代码指南](docs/THIRD_PARTY_CODE_GUIDE.md)、[API 速查](docs/PLATFORM_API.md)、[示例目录](examples/README.md)、[投稿流程](docs/THIRD_PARTY_APPS.md)。

## 从源码构建系统固件

固件使用 ESP-IDF 6.1、WAMR 2.4.5 和特定 ESP-Mosaico BSP；还需要真实的目标硬件与相应工具链。[硬件要求](docs/HARDWARE_REQUIREMENTS.md)、[SDK 约束](docs/SDK.md)、[应用包格式](docs/app_format.md)。

**签名身份必须先处理。**仓库只提供现有发行身份的公钥 `firmware/main/trusted_key.pem`，不提供对应私钥。直接运行默认签名构建或 `tools/bootstrap.ps1` 会因缺少 `local/dev_private.pem` 而失败。仅在自己的开发副本和测试设备上，可显式生成一套新密钥：

```powershell
python tools/app_package.py keygen --private local/dev_private.pem --public local/dev_public.pem
Copy-Item -LiteralPath local/dev_public.pem -Destination firmware/main/trusted_key.pem
./tools/bootstrap.ps1
./Run.ps1 Test
./Run.ps1 Build
```

替换公钥后，该副本生成的固件与应用使用**新的开发身份**：不能更新原发行设备，也不能安装由原发行身份签名的 COS 在线应用。`firmware/main/trusted_key.pem` 在该开发副本中会显示为 Git 修改，**不要把新公钥误提交为官方发行身份**。烧录会改写设备数据；只有明确选择兼容板卡并确认备份与分区后再操作。这个仓库不包含原私钥，也不会替你恢复它。

## 验证源码快照

以下是无需签名私钥的轻量测试；不包含完整 ESP-IDF 构建或实机验收。

```powershell
python -m pip install -r requirements-dev.txt
Push-Location tools
npm install --ignore-scripts --no-package-lock
Pop-Location
node tools/build_apps.mjs
python -m pytest -q tests
```

## 发布和安全边界

在线目录最多容纳 12 个应用。正式发布必须由维护者审核并签名，随后先上传新的 `apps/<SHA256>.app`，最后更新 `index.json`；不要给投稿者正式 COS 写权限或平台私钥。[静态目录部署](docs/STORE_DEPLOYMENT.md)、[OTA 部署](docs/OTA_DEPLOYMENT.md)、[应用 ID 归属](docs/APP_ID_REGISTRY.md)。

WAMR 等外部依赖由项目脚本按固定版本获取，生成的 `external/`、`dist/`、`local/`、固件构建目录和密钥均不应提交。项目尚未启用完整的量产级 Secure Boot／eFuse 防护；公开源码不构成硬件兼容、数据安全或商用稳定性承诺。

## 第三方许可

仓库根目录目前没有项目整体的 `LICENSE`。`firmware/components/esp-mosaico-bsp/`、`firmware/components/esp_lcd_co5300/` 等第三方组件保留其 Apache-2.0 许可文件；花园字体的许可说明位于 [GARDEN_FONT_OFL.txt](docs/GARDEN_FONT_OFL.txt)。不要把第三方组件的许可证误认为覆盖整份仓库。
