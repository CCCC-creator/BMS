# 9 串锂电池管理系统（BMS）

基于 **STM32F103 + TI BQ76940** 的 9 串锂电池管理系统，FreeRTOS 多任务架构，覆盖 AFE 采样、保护判据、充放电控制、被动均衡与 SOC 估算；通信侧提供 Modbus RTU 与 CANopen 两种从机方案（分属不同分支）。

上位机演示（CANopen 分支）：`docs/canopen_gui.png`、`docs/demo_canopen.mp4`

---

## 功能

| 模块 | 实现 |
|---|---|
| AFE 采样 | BQ76940 寄存器级驱动，I2C 带 CRC 分帧，单体电压 / 库仑计电流 / NTC 温度 |
| 保护 | OV / UV / OCD / SCD / OT / UT，带滞回与时间窗；MOS 状态以回读 SYS_CTRL2 为准 |
| 均衡 | 被动均衡，按掩码选择开启节，静置窗口内才允许，故障立即停止 |
| SOC | OCV 11 点查表锚定 + 安时积分；中位数取电压、两拍确认抗坏通道 |
| 电压补偿 | 逐节路径电阻补偿，补偿后同步抬高硬件 OV_TRIP 阈值 |
| 通信 | Modbus RTU 从机 / CANopen 从机（CANopenNode V4，OD 由 CANopenEditor 生成） |
| 可靠性 | IWDG 看门狗、I2C 互斥量、通信失败保持上一拍值 |

## 硬件与参数

| 项 | 值 |
|---|---|
| MCU | STM32F103（128 KB Flash），Keil MDK 5 工程 |
| AFE | TI BQ76940，I2C1，7 位地址 0x08 |
| 电池配置 | 当前 9 串（由 `cell_used` 掩码控制），平台支持 9~15 串 |
| CAN | 500 kbps（APB1 36 MHz，Prescaler 4，BS1 15TQ，BS2 2TQ，SJW 1TQ） |
| 调试串口 | USART1，9600 8N1 |
| 看门狗 | IWDG，由 MonitorTask 周期喂狗 |

## 软件架构

6 个 FreeRTOS 静态任务（`App/app_init.c`）：

| 任务 | 职责 | 周期 |
|---|---|---|
| `SampleTask` | AFE 采样，电压 / 电流 / 温度入库 | 250 ms |
| `PortectTask` | 保护判据判断与故障处理 | 跟随采样 |
| `SOCTask` | OCV 锚定与安时积分 | 1 s |
| `BalancingTask` | 被动均衡选择与开关 | 250 ms |
| `MonitorTask` | 喂狗、状态汇总 | 1 s |
| `LEDTask` | 运行指示 | 500 ms |

采集与保护之间通过结构体标志同步，避免保护任务在首轮数据未就绪时误判。

## 分支说明

三个分支共用同一套采样 / 保护 / 均衡 / SOC 代码，**区别只在通信层与调试输出**：

| 分支 | 内容 | 说明 |
|---|---|---|
| `canopen` | CANopen 从机全链路 + python 上位机 | 该分支 README 为完整接口说明 |
| `modbus-rtu` | Modbus RTU 从机 | 该分支 README 为寄存器表说明 |
| `debug` | 调试版本，带串口打印与诊断输出 | 无通信层 |

## 目录结构

```
App/         任务创建、业务调度
Bsp/         BQ76940 驱动、保护、均衡、SOC
Core/        CubeMX 生成的外设初始化
Middleware/  FreeRTOS、CANopenNode
MDK-ARM/     Keil 工程（tem.uvprojx）
docs/        截图与演示视频
tools/       上位机脚本
```

## 编译与下载

1. Keil MDK 5 打开 `MDK-ARM/tem.uvprojx`
2. 选择对应 target，执行 **Rebuild**（不要用 Build，避免残留 .o 导致链接错误）
3. J-Link 下载，复位运行

## 已知限制

- 当前按 9 串配置，扩展串数需同步修改 `cell_used` 掩码与 `TotalCells`
- 路径电阻补偿系数为一次性标定，未做在线自学习
- 容量为固定值，**未实现容量标定与 SOH 估算**
- 温度仅一路 NTC，无均衡回路温度监测
- 未使用 RTC 与 Flash 存储，SOC 掉电不保持