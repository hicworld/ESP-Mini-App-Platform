# 从示例开始开发 ESP Mini App

先读 [第三方应用代码规范与构建指南](../docs/THIRD_PARTY_CODE_GUIDE.md)。所有目录都是平台应用源码，包含 `manifest.json` 和语言入口；示例 ID 仅供学习，投稿前必须替换。

| 目录 | 语言 | 学习点 | 最低平台版本 |
| --- | --- | --- | --- |
| `c_color_pad` | C | `app_init`、触摸事件、屏幕绘图 | 0.1.0 |
| `c_scene_timer` | C | 完整场景绘图、50 ms 定时事件、毫秒回绕 | 0.4.0 |
| `c_optional_input` | C | 必需/可选外设、能力查询、按键/人体感应与触摸降级 | 0.4.8 |
| `c_first_batch` | C | 手势、中文与 RGB565 图片、IMU/姿态/磁场/电量和反馈的可选降级 | 0.5.0 |
| `rust_saved_counter` | Rust | `no_std`、键值存储、写入额度返回值 | 0.2.7 |

开发者 ZIP 里还有 `templates/third_party_app`（最简 C 模板）、`apps/bounce`（C++）及 `apps/rust_bounce`（Rust 场景绘图）供进阶参考。

在开发者包根目录，完成工具链准备后可直接运行：

```powershell
python tools/build_c_app.py examples/c_color_pad --unsigned
python tools/check_submission.py examples/c_color_pad
python tools/build_rust_app.py examples/rust_saved_counter --unsigned
python tools/check_submission.py examples/rust_saved_counter
```

本地工作台的“示例应用”页可按功能浏览每个示例，打开详情查看操作说明和完整源码，再点“复制到我的项目”。也可在“我的项目”页导入本地示例文件夹。两种方式都会创建副本，原示例文件不会被修改。

综合示例的参数和单位见 [第一批开放能力](../docs/FIRST_BATCH_CAPABILITIES.md)。
