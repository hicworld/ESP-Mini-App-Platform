# 第三方应用投稿（第一版）

当前开放的是**投稿与人工审核**，不是任何人都能直接修改在线目录。开发者不需要、也不能获得平台签名私钥或 COS 正式目录写权限。提交的 WASM 不是可安装的 `.app`；审核通过后由平台维护者重新构建、签名并发布。

维护者可运行 `python tools/make_developer_kit.py` 生成 `dist/ESP-Mini-App-Developer-Kit.zip` 发给投稿者。包内只有公开 SDK、工具、示例和投稿说明，不包含平台私钥、固件或已签名应用。开发者不需要克隆整份固件工程。

作者可从 [代码规范与构建指南](THIRD_PARTY_CODE_GUIDE.md) 和 [入门示例](../examples/README.md) 开始；示例 ID 只能用于本地学习，投稿必须换成自己的 ID。

本地工作台入口为 `python tools/developer_studio.py`（先安装 Python、Node.js 和相应语言工具链）。“我的项目”负责创建、导入和整理；“示例应用”展示功能与完整源码，并可复制为项目；“开发文档”提供平台函数与编码规范。进入项目开发页后可通过表单配置 Manifest（含公开作者名和简介）、编辑源码、构建和校验 WASM、导出待审核 ZIP；应用详情和电脑预览各有独立页面。项目保存在 `local/developer-studio/projects`。电脑预览只模拟部分宿主接口，不代替设备验收。导出的 ZIP 仍走下文人工审核流程。

## 开发者：准备投稿

1. 阅读 [SDK.md](SDK.md) 和 [app_format.md](app_format.md)。初次开发可把 `templates/third_party_app` 复制到 `apps/你的应用`，也可从 `apps/snake`（C）、`apps/bounce`（C++）或 `apps/rust_bounce`（Rust）复制示例，修改源码与 `manifest.json`。C/C++ 用固定的 WASI SDK 34；Rust 需 `wasm32-unknown-unknown` target。不要提交 `local/`、私钥、`target/` 或整份工程构建缓存。
2. 使用自己的反向域名风格应用 ID；现有示例里的 `com.example.*` 只是占位，正式投稿必须改为自己的 ID。一个 ID 只归属一位作者；后续更新保持 ID 不变，递增三段式版本号。声明实际需要的最小权限、必需硬件和可选硬件，不要把可选外设写为必需。
3. 在工程根目录执行相应的无私钥构建与自检：

```powershell
# C / C++
python tools/build_c_app.py apps/你的应用 --unsigned

# 或 Rust
python tools/build_rust_app.py apps/你的应用 --unsigned

python tools/check_submission.py apps/你的应用
```

产物为 `dist/submission/你的应用.wasm`，不会生成签名 `.app`，也不会读取 `local/dev_private.pem`。`check_submission.py` 检查 Manifest、WASM 大小和设备 ABI，并打印 WASM SHA256；它**不**证明应用在实机上可用或代码安全。

4. 投稿时提供：完整应用源码、`manifest.json`、该 WASM、上述自检输出；按 `templates/third_party_app/投稿信息.md` 填写作者/联系方式、用途、操作方式、目标硬件、权限用途、截图、源码与素材的授权或许可证。请不要只交二进制文件。作者应说明是否依赖联网、音频、传感器等能力，以及缺少可选外设时的表现。投稿渠道可先用代码仓库 PR 或私下文件提交；此工程目前没有公开投稿网站或自动审批接口。

投稿不保证收录。不要向维护者索要签名密钥，也不要将投稿文件上传到 COS 正式 `store-deploy/` 路径。

## 维护者：审核与发布

收到工作台导出的 ZIP 后，可以在本地工作台的“投稿包初审”页选择文件，或运行 `python tools/review_submission.py 路径/投稿包.zip`。工具只读取 ZIP、不解压执行源码，不使用签名私钥；会检查归档路径与体积、Manifest、WASM 与 SHA256、ABI、导入接口所需权限和外设，生成 `local/submission-reviews/` 下的 JSON/Markdown 报告。`REJECTED` 表示需退回修正；`MANUAL_REVIEW` 表示只通过了机器可检项目，**不是审核通过**。自动检查无法证明源码与 WASM 一致、源码安全、素材授权、作者归属或真机体验。

1. 将投稿放入隔离目录，不运行来源不明的脚本。在 [APP_ID_REGISTRY.md](APP_ID_REGISTRY.md) 登记作者与应用 ID 的归属；同 ID 更新必须核对原作者。检查源码、依赖和素材授权，以及 Manifest 权限/硬件声明。审核不能仅凭 WASM 静态检查完成。
2. 使用可信工具链，从审核后的源码重新构建；先运行 `--unsigned` 和 `check_submission.py`，对照作者提供的哈希，差异要调查（构建不一定完全可复现，不能仅凭哈希不同判恶意）。在实机上检查启动、退出、操作、资源消耗和缺少可选外设的情况。
3. 审核通过后，在**可信本机**执行原有带签名的构建命令，使用原平台私钥生成 `.app`：

```powershell
python tools/build_c_app.py apps/你的应用
# Rust 则用：python tools/build_rust_app.py apps/你的应用
python tools/app_package.py verify dist/你的应用.app --key firmware/main/trusted_key.pem
```

签名私钥 `local/dev_private.pem` 只在维护者机器上保存；不要放进 PR、投稿包、网页后端、COS 或分发包。更换私钥意味着旧设备内置的公钥不能验新包。

4. 用**现有全部在架应用**的 `.app` 路径调用 `tools/build_store.py`，不要只传新应用，否则生成的索引会漏掉旧应用。工具会验签并拒绝重复 ID；当前目录最多 12 个应用、`index.json` 最多 20 KiB。上传新的 `apps/<SHA256>.app` 后，最后更新 `index.json`；暂时保留旧哈希文件。详见 [STORE_DEPLOYMENT.md](STORE_DEPLOYMENT.md)。
5. 在设备上刷新在线目录并安装新应用，确认名称、版本、权限、运行和更新行为。未通过时不要修改正式索引。

## 后续扩展边界

如果投稿量增加，再建登录、私有待审核区、限权临时上传凭证、自动校验和人工批准流程。正式目录仍只能由发布流程写入，不能把 COS 长期密钥或平台签名私钥交给开发者。要超过 12 个应用，需要同步修改目录生成器和固件容量/界面并做兼容性测试；第一版不宣称支持无限量投稿。
