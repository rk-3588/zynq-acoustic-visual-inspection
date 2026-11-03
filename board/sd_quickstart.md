# SD最小启动流程

预编译启动文件、压缩根文件系统和写卡步骤已统一放入 [reproduce/README.md](../reproduce/README.md)。从源码构建见 [reproduce/build_from_source.md](../reproduce/build_from_source.md)。

公开根文件系统保留最终程序，已清理原登录信息、SSH密钥、历史抓拍和调试数据，使用独立演示账号；原卡备份未修改。文件系统检查和应用哈希比对通过，新卡冷启动验收待完成。
