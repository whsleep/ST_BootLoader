# Boot_B — STM32F401 IAP 引导加载程序

基于 STM32F401CCU6 的双区 IAP（In-Application Programming）Bootloader，支持通过 Modbus RTU + XMODEM-CRC 协议进行固件在线升级。

---

## 硬件平台

| 项目 | 参数 |
|------|------|
| **MCU** | STM32F401CCU6（Cortex-M4F，UFQFPN48） |
| **Flash** | 256 KB（0x08000000 - 0x0803FFFF） |
| **SRAM** | 64 KB（0x20000000 - 0x2000FFFF） |
| **主频** | 84 MHz（HSI → PLL） |
| **调试接口** | SWD（PA13 / PA14） |
| **通信接口** | USART1 @ 115200 8N1（PA9 TX / PA10 RX） |
| **状态指示** | PC13 板载 LED（LD0），不同频率闪烁表示当前状态 |
| **外部晶振** | HSE 25 MHz / LSE 32.768 kHz（已焊接，时钟方案见下方说明） |

---

## 功能特性

- **Modbus RTU 从站**（从站地址 19），支持功能码 0x03（读保持寄存器）和 0x06（写单个寄存器）
- **XMODEM-CRC** 固件接收，支持 128 字节和 1024 字节数据包
- **Flash 双区布局**：Bootloader 占用前 32 KB，APP 占用后 224 KB
- **APP 有效性校验**：通过魔数（0x5A5A）验证应用程序是否完整
- **有限状态机（FSM）驱动**：状态切换清晰，行为可预测
- **超时自动跳转**：5 秒内未收到升级指令，自动跳转至已有 APP
- **uLog 日志系统**：分级日志输出（DEBUG / INFO / WARNING / ERROR），通过 USART1 打印
- **LED 状态指示**：不同闪烁频率反映当前 FSM 状态

---

## 系统架构

### Flash 内存布局

```
0x08000000 ┌──────────────────────┐
           │   Bootloader (32KB)  │  扇区 0-1（16KB × 2）
           │   本工程所在区域      │
0x08008000 ├──────────────────────┤  ← APP_ADDRESS
           │                      │
           │   Application        │  扇区 2-5
           │   用户应用程序        │  （16KB + 16KB + 64KB + 128KB）
           │   (224 KB)           │
           │                      │
0x08040000 └──────────────────────┘
```

- `APP_VALID_MAGIC（0x5A5A）` 存放于 `APP_ADDRESS + 408` 字节处，即 APP 向量表之后
- Bootloader 编译时通过分散加载文件确保代码不超出 32 KB 边界

### 模块组成

```
Application/
├── BootLoader/    # Flash 擦写、APP 校验、跳转至 APP
├── Fsm/           # 通用有限状态机引擎 + 各状态行为实现
├── ModbusRTU/     # Modbus RTU 从站协议栈（CRC16、FC03/FC06）
├── Xmodem/        # XMODEM-CRC 接收器（FIFO 缓冲、ACK/NAK、CRC 校验）
└── ULOG/          # 嵌入式日志库（订阅/发布模式）

Core/
├── Inc/           # HAL 配置、外设头文件
└── Src/           # main.c、中断服务、外设初始化
```

### 状态机流转

```
          ┌─────────────────┐
          │ STATE_MODBUS_   │  等待 Modbus 指令（5 秒超时）
          │ RECV            │  LED 快闪（50ms）
          └───────┬─────────┘
                  │
       收到合法 0x06 指令      超时 + APP 有效
     (holding_reg[0]==0x5B5B)    │
                  │              ▼
                  │      ┌─────────────────┐
                  │      │ STATE_JUMP_APP  │  跳转至应用程序
                  │      │ LED 慢闪（500ms）│
                  │      └─────────────────┘
                  ▼
          ┌─────────────────┐
          │ STATE_PROG_     │  擦除 APP 区 → XMODEM 接收固件
          │ UPGRADE         │  LED 中速闪（100ms）
          └───────┬─────────┘
                  │
         烧写完成 + APP 有效
                  │
                  ▼
          ┌─────────────────┐
          │ STATE_JUMP_APP  │
          └─────────────────┘
```

---

## 工作流程

### 正常启动（已有有效 APP）

1. 上电 → 初始化外设 → 进入 `STATE_MODBUS_RECV`
2. 等待 5 秒，未收到升级指令
3. 检测到 APP 有效（魔数 0x5A5A 存在）→ 跳转至 APP

### 固件升级流程

1. **触发升级**：上位机通过 Modbus FC06 写入触发寄存器（`holding_reg[0] = 0x5B5B`），同时提供读写计数器
2. **进入升级状态**：FSM 切换至 `STATE_PROG_UPGRADE`，擦除扇区 2-5
3. **XMODEM 传输**：Bootloader 发送 `'C'` 字符，启动 XMODEM-CRC 接收
4. **逐包烧写**：每收到一个完整数据包（128B 或 1024B），通过 32 位字写入 Flash（带回读校验）
5. **传输完成**：收到 EOT → 写入剩余缓冲 → 校验 APP 有效性
6. **跳转执行**：重新初始化系统 → 设置 VTOR → 跳转至 APP 复位向量

### Modbus 触发寄存器说明

| 寄存器 | 含义 | 触发升级的要求 |
|--------|------|---------------|
| `holding_regs[0]` | 触发命令字 | 必须等于 `0x5B5B` |
| `write_regs[0]` | FC06 写入计数器 | > 0 |
| `read_regs[0]` | FC03 读取计数器 | > 0 |

三个条件**同时满足**时，才会触发状态切换，防止误触发。

---

## 目录结构

```
Boot_B/
├── README.md
├── Boot_B.ioc                     # STM32CubeMX 工程配置
├── .mxproject                     # CubeMX 元数据
├── Application/
│   ├── BootLoader/
│   │   ├── BootLoader.c           # Flash 操作与跳转逻辑
│   │   └── BootLoader.h           # 内存布局、扇区定义
│   ├── Fsm/
│   │   ├── fsm_bootloader.c       # 通用 FSM 引擎
│   │   ├── fsm_bootloader.h       # 状态/事件枚举
│   │   ├── fsm_event.c            # 各状态的 Entry/Do/Exit 实现
│   │   └── fsm_event.h
│   ├── ModbusRTU/
│   │   ├── ModbusRTU.c            # Modbus RTU 从站协议
│   │   └── ModbusRTU.h            # 数据结构与 API
│   ├── ULOG/
│   │   ├── ulog.c                 # uLog 轻量级日志库
│   │   └── ulog.h
│   └── Xmodem/
│       ├── XMODEM.c               # XMODEM-CRC 接收器
│       └── XMODEM.h
├── Core/
│   ├── Inc/
│   │   ├── main.h
│   │   ├── stm32f4xx_hal_conf.h   # HAL 模块开关配置
│   │   ├── stm32f4xx_it.h
│   │   ├── gpio.h
│   │   ├── tim.h
│   │   └── usart.h
│   └── Src/
│       ├── main.c                 # 主程序入口
│       ├── stm32f4xx_hal_msp.c    # HAL 底层初始化
│       ├── stm32f4xx_it.c         # 中断向量表与 ISR
│       ├── system_stm32f4xx.c     # 系统时钟配置
│       ├── gpio.c
│       ├── tim.c                  # TIM4 10ms 时基
│       └── usart.c                # USART1 初始化 + printf 重定向
└── MDK-ARM/
    ├── Boot_B.uvprojx             # Keil MDK uVision5 工程
    ├── Boot_B.uvoptx
    └── startup_stm32f401xc.s      # 启动文件
```

---

## 关键参数

| 参数 | 值 | 定义位置 |
|------|-----|----------|
| BOOT 区大小 | 32 KB（0x8000） | `BootLoader.h` |
| APP 区大小 | 224 KB（0x38000） | `BootLoader.h` |
| APP 起始地址 | 0x08008000 | `BootLoader.h` |
| APP 魔数地址 | 0x08008198 | `BootLoader.h` |
| APP 有效魔数 | 0x5A5A | `BootLoader.h` |
| Modbus 从站地址 | 19 | `main.c` |
| 保持寄存器数量 | 8 | `main.c` |
| 串口波特率 | 115200 8N1 | `usart.c` |
| FSM 超时 | 5000 ms | `fsm_bootloader.h` |
| TIM4 时基 | 10 ms | `fsm_bootloader.h` |
| XMODEM 超时 | 500 ms | `main.c` |
| XMODEM 最大重试 | 5 次 | `main.c` |

---

## 构建与开发环境

### 工具链

- **IDE**：Keil MDK uVision 5（ARMCC V5.06，C99）
- **配置工具**：STM32CubeMX 6.12.0
- **HAL 库**：STM32Cube_FW_F4 V1.28.3
- **设备包**：Keil.STM32F4xx_DFP.2.17.1
- **辅助编辑**：VS Code + Keil Assistant 插件

### 编译步骤

1. 安装 Keil MDK 及 STM32F4 设备包
2. 安装 STM32Cube_FW_F4 V1.28.3（默认路径 `C:\Users\<用户名>\STM32Cube\Repository\STM32Cube_FW_F4_V1.28.3\`）
3. 打开 `MDK-ARM\Boot_B.uvprojx`
4. 编译生成 HEX 文件（`MDK-ARM\Boot_B\Boot_B.hex`）

> **⚠️ 注意**：工程中 HAL/CMSIS 源文件使用**绝对路径**引用 Cube 仓库。如果 Cube 包安装在其他位置，需要在 Keil 中重新指定文件路径。

### 预定义宏

- `USE_HAL_DRIVER` — 启用 HAL 库
- `STM32F401xC` — 目标芯片型号
- `ULOG_ENABLED` — 启用 uLog 日志输出

---

## 通信协议

### 第一阶段：Modbus RTU 指令检测

Bootloader 上电后默认为 Modbus RTU 从站模式（地址 19），监听 USART1：

| 功能码 | 说明 | 示例 |
|--------|------|------|
| **0x03** | 读保持寄存器 | 读取当前寄存器值 |
| **0x06** | 写单个寄存器 | 写入 `holding_reg[0] = 0x5B5B` 触发升级 |

触发条件：`holding_reg[0] == 0x5B5B` 且 `write_regs[0] > 0` 且 `read_regs[0] > 0`。

### 第二阶段：XMODEM-CRC 固件传输

进入升级状态后，Bootloader 发送 `'C'` 字符，启动 XMODEM-CRC 128 字节模式传输：

- 上位机发送 XMODEM 数据包（128B 或 1024B），每包带 CRC16 校验
- Bootloader 对每个正确数据包回复 `ACK`，错误包回复 `NAK`
- 传输结束发送 `EOT`，Bootloader 回复 `ACK` 确认
- 支持自动重试，最大 5 次

---

## LED 状态指示

| FSM 状态 | 闪烁周期 | 含义 |
|----------|---------|------|
| `STATE_MODBUS_RECV` | 50 ms | 快闪 — 等待 Modbus 指令 |
| `STATE_PROG_UPGRADE` | 100 ms | 中速闪 — 正在接收固件 |
| `STATE_JUMP_APP` | 500 ms | 慢闪 — 即将跳转 / 默认 |

