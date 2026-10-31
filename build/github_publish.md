# 仓库维护与源码归档

项目仓库：[rk-3588/zynq-acoustic-visual-inspection](https://github.com/rk-3588/zynq-acoustic-visual-inspection)。

仓库按 `src/`、`sim/`、`build/`、`board/`、`data/`、`skill/`、`report/` 组织。工作缓存与本地二进制资产由 `.gitignore` 排除；启动文件及板端程序的冻结哈希保存在 `board/release_manifest.json` 和 `build/frozen_assets_sha256.json`。

## 更新检查

1. 检查本次源码和文档差异，更新逐文件清单 `build/files_sha256.json`。
2. 运行 `python3 build/audit_release.py`，检查文件清单、路径、凭据模式、IP依赖和最终配置。
3. 按改动范围运行软件回归、RTL/HLS仿真或完整系统构建，并更新相应验证记录。
4. 检查暂存文件，再提交和推送。

`audit_release.py --publish` 另行检查第三方许可确认状态；授权待确认时返回2。该状态与技术审计分开记录，来源和许可文档随仓库保留。

## 生成源码归档

```sh
python3 build/package_source.py
```

脚本在 `work/` 生成源码ZIP与SHA256校验文件，并核对包内逐文件内容。`work/`、`release_assets/`、工具缓存和SD镜像不进入源码归档。

## 二进制资产

bit、XSA、DCP、LTX、BOOT、ELF、image.ub和rootfs.ext4与源码分开保存，按各自许可处理。使用前核对板型、硬件接口和文件哈希；普通Linux应用更新按上板文档执行。
