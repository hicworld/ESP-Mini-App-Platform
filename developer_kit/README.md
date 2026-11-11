# ESP Mini App 第三方开发者包

本包只用于开发和投稿 WASM 应用；**不含平台签名私钥**，也不会让投稿者直接修改线上应用目录。当前支持 C、C++、Rust，Windows 下的 C/C++ 工具链固定为 WASI SDK 34.0。

第一次写应用，请先读 `docs/THIRD_PARTY_CODE_GUIDE.md`，调用 `platform_clear`、`platform_text` 等函数时查 `docs/PLATFORM_API.md`，再从 `examples/README.md` 选一个可编译示例。示例涵盖触摸绘图、场景定时、可选外设输入、0.5.0 传感器/图片/手势和 Rust 存储；`apps/bounce` 另展示 C++。

## 本地工作台

在 Windows 上，先**完整解压**本 ZIP，再双击根目录的 `启动本地工作台.cmd`。脚本会在解压目录查找 Python 3.10 或更新版本，启动只监听本机的工作台并自动打开浏览器；使用期间请保持命令窗口打开，结束时按 Ctrl+C。项目保存在解压目录的 `local/developer-studio`，重新解压到别的目录不会自动带走旧项目。首次构建前，再按工作台的“检查开发环境”提示准备 Python 依赖、Node.js 和 C/C++ 所需的 WASI SDK；写 Rust 还需 Rust target。

如果想从终端启动，也可在解压目录运行：

若从桌面启动后工作台找不到已安装的 Node.js，可先设置 `NODE_EXE` 为 `node.exe` 的绝对路径。

```powershell
python tools/developer_studio.py
```

浏览器会打开只监听本机的工作台。导航分为“我的项目”“示例应用”“开发文档”“投稿包初审”四个入口：项目页管理副本并检查工具链；示例页说明功能、操作和完整源码，可直接复制成项目；文档页阅读平台函数、第一批能力、编码规范、Manifest 与投稿流程。进入某个项目后，在项目开发页编辑 Manifest 和文本源码、构建并导出投稿 ZIP；电脑预览和应用详情各有独立页面。点击“导入本地源码”可选择包含 `manifest.json` 的文件夹，同名时自动加序号。项目旁的“移除”会将工作台副本和构建结果放入 `local/developer-studio/removed-projects`，可在“最近移除”恢复；原始导入文件夹与已导出的投稿 ZIP 不受影响。默认项目位于 `local/developer-studio/projects`。修改代码后重新构建。预览是浏览器模拟环境，不代表真机验收。“投稿包初审”可选择 ZIP 检查归档、Manifest、WASM 和能力声明；通过后仍需人工复核、重建、真机测试与签名，不能直接安装到正式固件。

更新工作台文件或重新解压开发者包后，先停止旧的 `python tools/developer_studio.py` 进程再启动。只刷新浏览器会更新页面脚本，但不会更新已经运行的 Python 服务；若两者版本不匹配，页面会提示重启。

在解压后的目录打开 PowerShell，准备 Python、Node.js，然后执行：

```powershell
python -m pip install -r requirements-app-sdk.txt
./tools/setup_sdk.ps1
Copy-Item -Recurse templates/third_party_app apps/my_first_app
# 修改 apps/my_first_app/manifest.json 中的 ID、名称、版本和能力声明
python tools/build_c_app.py apps/my_first_app --unsigned
python tools/check_submission.py apps/my_first_app
```

上面只生成 `dist/submission/my_first_app.wasm`，不会生成可安装的 `.app`。将 `apps/my_first_app` 的源码、Manifest、WASM、自检输出和按 `templates/third_party_app/投稿信息.md` 填好的说明交给平台维护者。不要上传其他人的源码或素材，也不要使用示例 ID 原样投稿。

如果使用 Rust，先安装 rustup 和 `wasm32-unknown-unknown` target，参照 `apps/rust_bounce`，再运行 `python tools/build_rust_app.py apps/你的应用 --unsigned`。详细规则见 `docs/SDK.md` 与 `docs/THIRD_PARTY_APPS.md`。
