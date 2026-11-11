# 静态应用目录部署

第三方应用先按 [THIRD_PARTY_APPS.md](THIRD_PARTY_APPS.md) 投稿、审核、由维护者签名；不要开放正式 COS 目录给投稿者直接写入。

当前目录已部署到腾讯云 COS，开发板已完成 HTTPS 目录读取、远程应用安装、运行和重启持久化验收。

当前设备索引：

```text
https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/store-deploy/index.json
```

## 生成文件

```powershell
python tools/build_store.py dist/hello.app dist/touch.app dist/snake.app dist/garden.app
```

给 COS 发布可单独保存和转发的网页时，使用完整 HTTPS 链接重新生成页面：

```powershell
python tools/build_store.py dist/hello.app dist/touch.app dist/snake.app dist/garden.app --output dist/store-cos-upload --base-url https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/store-deploy
```

此时 `index.html`、`details/*.html` 的链接会直接指向 COS；即使把 HTML 单独下载到电脑，点击应用详情或应用包也不会跳到本地 `file:///apps/`。将页面分别上传到 COS 的 `store-deploy/index.html` 和 `store-deploy/details/`。

输出 dist/store/index.html、index.json、details/ 下的应用详情页，以及 apps/ 下的四个签名应用包。详情页展示已签名 Manifest 中的公开作者名、简介、版本、权限和外设信息；旧包缺少作者字段时显示“未填写”。生成器只读取可信公钥进行验签，不复制开发私钥。应用文件名使用完整包 SHA256，因此更新索引时可保留旧文件，避免旧索引引用到被覆盖的内容。遇到坏包或重复应用 ID 时，在写入新目录前拒绝生成。

index.html 是可浏览、下载应用的静态页面；index.json 是设备使用的目录。无需数据库或服务端应用。

## 托管要求

1. 把整个 dist/store 目录上传到支持静态文件的 HTTPS 服务器，保持相对路径关系。
2. 使用开发板 ESP-IDF 证书包信任的公开 CA 证书。开发板必须联网并通过 SNTP 获得有效时间，不允许跳过证书验证。当前固件已启用 `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY`，可正常验证 COS 的 GlobalSign 交叉签名链。
3. index.json 和 .app 应返回 HTTP 200、正确 Content-Length；不要重定向、压缩为 gzip 或动态分块传输。
4. 更新时先上传新的 apps/*.app，最后替换 index.json。不要立即清理仍被缓存索引引用的旧包。
5. 不上传 local 目录、签名私钥或源码构建缓存。生成器保留旧的哈希包文件，部署者可在缓存过期后自行清理。

## 配置开发板

0.4.9 起，固件内置上述腾讯 COS 正式目录地址。全量烧录、擦除 NVS 或首次启动时，如果没有有效的自定义地址，在线应用页会自动使用该默认地址，不再要求先连接 USB 配置。

如需切换到其他目录，可回到首页，通过 COM4 发送：

```text
store https://你的域名/store/index.json
store_list
store_install 2
```

首次 store 命令成功下载并解析目录后才保存自定义地址；以后该地址优先于固件默认值。随后可在 App Library → Online → Refresh 刷新。每一项会按目录版本和已安装版本显示 Install、Update、Reinstall 或 Older；Older 项被禁用。保存的地址在重启后保留，列表按需刷新，不在启动时阻塞下载。

腾讯 COS 上传压缩包时会保留最外层 `store-deploy/` 目录，因此设备地址必须包含该前缀。COS 存储桶使用公有读、私有写；不要把开发私钥上传到存储桶。

USB 也可发送 store_refresh 使用已保存地址。目录地址不支持用户信息、查询参数和片段。包 file 必须是同目录下的安全相对路径，不支持绝对路径或 ..。

## 校验与容量

目录格式为 schema_version=1、apps 数组，最多 12 项，总体不超过 20 KiB。每项包含 id、name、version、permissions、file、size、sha256；生成器另带 platform_min，最终兼容性由已签名应用 Manifest 判定。

安装时先判断目录版本：未安装、较新和同版本分别允许 Install、Update 与 Reinstall；较旧版本在发起网络请求前返回 `ESP_ERR_INVALID_VERSION`。版本按三个十进制字段比较，例如 `0.10.0` 高于 `0.9.0`。随后才限制下载长度、检查完整包 SHA256、验证包签名及负载哈希，并核对应用 ID、名称、版本、权限与所选目录项一致，最后写入应用存储。目录不能扩大已签名应用的权限。

目录降级保护用于避免公开目录误发布旧包。受信任开发者仍可通过 USB 安装或显式 HTTPS 包地址执行恢复与回退，这两条路径继续执行完整签名和包身份校验。完整边界见 [APP_UPDATE_POLICY.md](APP_UPDATE_POLICY.md)。

刷新失败保留上一次内存中的有效列表；重启后需要重新刷新。安装期间需停止当前应用。系统 OTA 文件不包含在此应用目录里，OTA 仍需独立部署和回滚验收。
