# SolarClean 固件升级工具

客户只需要这个文件：

```text
SolarCleanFirmwareUpdater.exe
```

使用方法：

1. 双击 `SolarCleanFirmwareUpdater.exe`。
2. 连接设备。
3. 选择固件。
4. 点击开始升级。

## 重新打包

```powershell
cd serial_ota
build.bat
```

打包脚本只会在当前目录生成：

```text
SolarCleanFirmwareUpdater.exe
```

`_build_tmp`、`_dist_tmp`、`.spec` 等中间产物会自动删除。
