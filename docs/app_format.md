# EMAP v1 与 ABI

字节 0..15 为 `<4sIII` 小端头：魔数 `EMAP`、Manifest 字节数、WASM 字节数、DER 签名字节数。
之后依次为 UTF-8 Manifest、P-256 ECDSA DER 签名、WASM。
签名输入是原始 Manifest 字节，摘要 SHA256；Manifest 的 sha256 指向 WASM 负载。
Manifest 最大 2048 字节，签名 8..80 字节，WASM 最大 256 KiB。没有压缩或资产路径，避免不明确的解包语义。

必填：manifest_version=1、api_version=1、id、name、version、entry=main.wasm、permissions、hardware、wasm_size、sha256。新包打包时自动补入 platform_min=0.1.0 和 optional_hardware=[]；兼容早期未携带这两个字段的已签名包。`platform_min` 高于当前平台版本的应用会在安装/启动验签阶段拒绝。版本使用三段十进制整数，各段 0..65535，总长不超过 19 字符。
可选的 `author` 是公开显示的作者名（最多 80 个 UTF-8 字节），`description` 是公开应用简介（最多 500 个 UTF-8 字节）。本地工作台和静态网页详情页显示这两个字段；平台 0.4.13（Mosaico）/ 0.4.12（Korvo-1）起，真机应用详情页也显示 `author`。未填写的旧应用显示“未填写”；要显示作者名，需在 Manifest 中填写并重新签名安装应用。它们不包含私人联系方式，也不代替投稿审核中的作者身份核对。
ID 使用至少三段小写字母/数字名称，各段首字符字母。权限接受 display/touch/storage/environment/input/audio/sensors/feedback。外设接受 display、touch、speaker、motion、buttons、wifi、camera、microphone、temperature、humidity、light、imu、magnetometer、battery、haptic、led。`hardware` 是必需外设，缺少时不执行 WASM；`optional_hardware` 是可选外设，缺少时提示但允许启动。两组不能重叠。重复字段、重复项和未知能力会被拒绝。
包尾不能有额外字节。生产发布索引可另外记录完整包 SHA256；不能把全包哈希写回包内造成自引用。

ABI import module 为 `platform`；clear(i32)、rect(i32,i32,i32,i32,i32)、text(i32,i32,i32,i32,i32)、millis()->i32、hardware()->i32。
`hardware()` 返回设备当前可用外设位图，供应用对可选能力进行降级处理。位定义在 C 与 Rust SDK 中；它只暴露能力，不提供原生 GPIO 或驱动句柄。
text 参数为 x、y、WASM 线性内存偏移、UTF-8 字节数、RGB，最大 160 字节。没有原生地址、句柄或 libc 文件接口。

ABI v1 只允许函数导入，不允许导入内存、表或全局变量。最多一块 32 位非共享线性内存；加载前检查声明的初始大小不超过 4 页（256 KiB），运行时将增长上限限制为 4 页。声明的最大页数可以更大，但不会扩大平台授予的运行额度。拒绝 start section 和自动构造函数导出。

每次 app_init / app_event 入口的执行预算为 50,000 条指令。平台主循环运行在 ESP-IDF pthread 中，匹配 WAMR 端口的线程身份要求。

启用 `storage` 权限后，应用可调用 `platform_kv_set` 和 `platform_kv_get`（WASM 导入名为 `kv_set` / `kv_get`）使用自己的键值空间。键名为 1..15 个 ASCII 字母、数字、下划线或短横线，禁止 `__` 开头；单值最多 128 字节，每应用最多 8 个键。应用 ID 会映射到 NVS 命名空间，并用保留的 `__id` 校验归属；哈希冲突时拒绝打开。平台 0.2.7 起，用户可在应用详情页通过 Clear Data 二次确认清空该命名空间，也可发送 `clear APP_ID`；应用必须已安装、已停止并声明 `storage` 权限。卸载仍默认保留数据，便于以后重新安装恢复状态。

整个运行时每个 uptime 整秒窗口最多接受 4 次发生变化的写入，停止或切换应用不会重置计数。写入前要求 NVS 至少剩余 128 个条目，以保留系统空间；额度不足返回 -3。相同内容的重复写入不触发闪存写操作。读取返回字节数，缺失返回 0，缓冲区不足返回 -2，后端错误返回 -1。无权限、非法键和写入指针越界会触发 WASM 异常。卸载保留应用数据。

## USB 安装协议

USB CDC 使用 ASCII 行命令，主机工具为 `tools/install_usb.py`。

1. `upload_abort` 清理已有会话，返回 `UPLOAD ABORTED`。
2. `upload_begin SIZE` 分配有界缓冲，返回 `UPLOAD READY SIZE`；应用运行时拒绝。
3. `upload_chunk OFFSET HEX` 顺序发送每块最多 128 字节，返回 `UPLOAD ACK 下一偏移`。
4. `upload_commit` 要求数据完整，通过相同的签名、兼容性、落盘回读和 Registry 提交流程后，才返回 `UPLOAD DONE ESP_OK`。

偏移、长度、十六进制格式错误返回 `UPLOAD ERROR ...`。会话空闲 30 秒自动释放；未完成上传不会修改已安装应用。签名或安装错误不会刷新当前应用画布。安装支持同 ID 更新，使用两个文件槽；NVS 提交失败时保留两份已验签文件并禁止继续修改 Registry，需重启加载持久状态。0.2.6 增加回读包与输入包逐字节一致性检查，并在加载 Registry 时检查权限位、WASM 大小、版本和非空名称。0.2.7 的应用管理器故障模拟通过 28/28，应用数据命名空间测试通过 7/7；物理断电恢复尚未完成故障注入验证，范围见 STORAGE_TESTING.md。

## 平台 0.4.0 扩展

`scene`、`environment` 与输入事件 ABI 见 `sdk/include/scene.h` 和 [POCKET_GARDEN.md](POCKET_GARDEN.md)。使用这些接口的应用必须声明 `platform_min: 0.4.0`。整帧绘图不能与旧绘图在同一入口混用；每批最多 96 条且占满当次绘图配额。环境快照需 environment 权限，输入事件需 input 权限；没有导出任意 URL 请求或原生 GPIO。
