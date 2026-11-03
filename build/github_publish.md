# 仓库维护与源码归档

项目按 `src/`、`sim/`、`build/`、`board/`、`data/`、`skill/`、`report/`、`reproduce/` 组织。

## 文件检查

更新源码和文档后，核对逐文件清单并执行：

```sh
python3 build/audit_release.py
```

脚本检查路径、文件哈希、凭据模式、IP依赖、应用配置及复现文件，另对压缩根文件系统进行解压尺寸和SHA256核验。按改动范围运行 `build/test_software.py` 或RTL／HLS仿真。

## 生成归档

```sh
python3 build/package_source.py
```

脚本在 `work/` 生成源码ZIP及SHA256校验文件，并核对包内内容。正式归档包含 `reproduce/` 中的启动文件与压缩根文件系统；工作缓存、工具安装目录和原卡私人备份由归档规则排除。

发布文件标识见 `board/release_manifest.json`、`reproduce/manifest.json` 和 `build/files_sha256.json`。组件来源及许可范围见 `third_party.md` 与 `license_scope.md`。
