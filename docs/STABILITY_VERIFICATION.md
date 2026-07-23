# PSDK + MQTT 稳定性验证清单

在目标硬件上按下列步骤验证（与 `mqtt-psdk-stability` 计划一致）。

## 1. MQTT 日志开/关

- [ ] `MQTT_DEBUG_AT_MQTT_DRIVER_LOG=1`：确认 AT/MQTT 日志正常。
- [ ] `MQTT_DEBUG_AT_MQTT_DRIVER_LOG=0`：订阅 `SolarClean/devices/<deviceId>/status`，确认约 1Hz `state` 仍上报。

## 2. PSDK Widget

- [ ] `CONFIG_MODULE_SAMPLE_WIDGET_ON` 保持开启：飞机识别、Widget 启动与 MQTT 状态上报同时工作，无异常复位。

## 3. routeDownload

- [ ] 下发 `routeDownload`：HTTP 下载期间系统不卡死；下载完成后 MQTT 自动恢复订阅与状态上报。
- [ ] 下载过程中 `downloadProgress` 在 HTTP 结束后至少有一条补发（若下载中曾产生进度）。

## 4. 长稳（建议 ≥30min）

- [ ] 观察调试串口：`tcp/err`、`uart7_line_err`（心跳日志开启时）、堆剩余（必要时 `uxTaskGetStackHighWaterMark`）。
- [ ] 无 HardFault / malloc failed / stack overflow 打印。

## 5. 故障注入（可选）

- [ ] 人为触发栈溢出或非法访问时，USART1 上出现 `CFSR/HFSR/BFAR/MMFAR` 或 `stack overflow` / `malloc failed` 一行以上诊断信息。
