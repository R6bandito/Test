# STM32F407 调试串口：UART + DMA + 空闲中断 — 问题与结论

实验平台：正点原子探索者 F407（USART1 → USB 转 TTL，PA9 TX / PA10 RX）。  
工程路径：`Drivers/SYSTEM/usart`，裸机 + 双缓冲 + IDLE 断帧。

---

## 1. 现象回顾

| 阶段 | 现象 |
|------|------|
| 初期 | `SR` 长期 `0xC0`（仅 TXE+TC），`NDTR` 不变，PC 发数无响应 |
| 修 PAR 后 | 能收**第一包**，之后 `NDTR` 不再减少 |
| 修 IDLE 重装流程后 | 连续收包正常；两缓冲都满时覆盖当前 `BackBuf`（符合设计） |

---

## 2. 根因一：DMA 的 `PAR` 必须写 DR 的**地址**

```c
/* 错误：把 DR 里读到的“数据”写进 PAR */
PAR = USART1->DR;

/* 正确：外设数据寄存器地址 */
PAR = (uint32_t)&USART1->DR;
```

- `PAR` 表示 DMA 从哪个**外设地址**取数。
- 不写 `&` 时，赋给 `PAR` 的是 DR 的**当前值**（常为 0），DMA 不会从 USART 收数据。
- 表现：`NDTR` 一直等于缓冲长度，RX 侧像“全死”；与 PC 是否发数无关。

这是**收不到任何有效数据**的首要根因（在 RE、引脚、波特率都正常的前提下）。

---

## 3. 根因二：异步口误用 `HAL_USART`（CR2 `CLKEN`）

调试口是**异步 UART**，应使用 **`HAL_UART_Init` + `UART_HandleTypeDef`**，而不是 `HAL_USART_Init`。

`HAL_USART` 面向同步 USART，初始化时会配置 **CR2 的 `CLKEN`（时钟输出）** 等位；异步 8N1 不需要 SCLK。  
此外若只填 `BaudRate/Mode/...` 而未清零整个 `Init`，**未初始化字段**会写入 CR2，行为不可预期。

| 项目 | `HAL_USART` | `HAL_UART` |
|------|-------------|------------|
| 用途 | 同步 USART（带 CLK） | 异步 UART |
| CR2 `CLKEN` | 默认会按同步模式配置 | 初始化时 **清除 `CLKEN`** |
| 与 CH340 调试口 | 不匹配 | 匹配 |

**`CLKEN` 会不会直接导致 RX 完全不通？**  
多数情况下不是“单独一个 CLKEN 位就关死 RX”，而是 **同步模式相关配置 + Init 垃圾值** 与异步链路组合，容易出现 **TX 尚能工作、RX/DMA 异常**。  
换用 `HAL_UART` 并 `memset` 后再填 `Init`，是正确做法。

---

## 4. 根因三：只收第一包 — `DMAR` 与 DMA Stream `EN` 必须成对重装

**疑问：** 关 DMA 流的 `EN` 时，`CR3.DMAR` 会不会被硬件关掉？  
**结论：** 一般不会因关 DMA 而**自动**清 `DMAR`；寄存器读回仍可能为 1。

**那为什么只 toggling DMA `EN` 只能收一包？**  
现象上：第一包 `DMAR=1` 且 `DMA EN=1` 时 `NDTR` 正常递减；IDLE 里只关再开 `EN`、不动 `DMAR` 时，第二包起 **`NDTR` 常冻结**。  
这与 **ST 要求的 peripheral–DMA 开关顺序不一致** 时的表现一致（见下），但 **不等于** ST 公开了 F407 上 USART 与 DMA 之间的内部状态机细节。

### 4.1 ST 官方能引用什么（AN DM00046011，F2/F4/F7 DMA）

应用笔记 **§4.1 关闭 DMA** 要求：

1. 先关 DMA stream 的 `EN`，并等到读回 `EN=0`；
2. 之后才能安全关外设；
3. **外设控制寄存器里的 DMA request enable 应清 0**（USART 即 **`CR3.DMAR`**），以保证 **外设侧 pending 的 DMA 请求被清除**。

**§4.3 开启 DMA** 要求：

1. 配置好 DMA stream；
2. **使能 DMA stream（`EN=1`）**；
3. **再**使能外设的 DMA request（**`DMAR=1`**）。

**§4.2** 还建议：新开传输前清除 **LIFCR/HIFCR** 中相关 stream 标志。

因此本工程 IDLE 里的顺序：

```text
清 IDLE → 关 DMAR → 停 DMA 流 → 读 NDTR/换缓冲 → 写 NDTR/M0AR
→ 清 LIFCR → 开 DMA EN → 再开 DMAR
```

是在按 AN 的 **disable/enable 成对序列** 重装接收链路，而不是“多此一举”。  
**「不要只关 stream 不关/不重配 DMAR」有 AN 依据**；社区与 HAL 实践与此一致。

### 4.2 文档边界：内部电路「具体发生了什么」

**公开资料通常不会写：**

- USART 内部 DMA 请求发生器在「`EN=0` 且 `DMAR=1`」时停在何种状态；
- 请求是否 latched、与 `NDTR`/TCIF 如何交叉；
- F407 上「仅首包有效」的**唯一**硬件路径。

**因此不宜在 blog 里断言：**「因为内部 XX 状态机进入 YY，所以 ZZ 信号送不出去」——除非引用 errata/勘误（较新系列如 U5/H5 上曾有 **DMAR 重开不出请求** 的 workaround，甚至社区标为 **undocumented**，需 toggle `UE` 等，与 F407 不能直接等同）。

**严谨表述建议：**

| 层次 | 能写什么 |
|------|----------|
| **可引用** | AN §4.1/4.3：关 stream 后清外设 DMA request；开 stream 后再开 `DMAR`；重装前清 LIFCR |
| **可复现** | 本板：仅 toggling `EN` → 第二包起 `NDTR` 不动；按 AN 顺序重装 `DMAR`+`EN`+LIFCR → 正常 |
| **工程推断（非证明）** | stream 停而 `DMAR` 仍置位时，peripheral 与 DMA 控制器侧握手可能未按 AN 方式复位；重装需走完整 disable/enable |
| **不宜断言** | 未在 RM/AN 中给出的内部微架构结论 |

与 **§2 `PAR` 未取地址**、**§3 误用 USART HAL** 不同：后两者是 **配置/编程错误**，后果可直接从 RM 推导；**§4 属于「手册给了流程、未给实现」** 一类——fix 有 AN 背书，**「内部发生了什么」没有足够公开资料做更进一步结论**。

---

## 5. 为什么要清 `DMA2->LIFCR`（Stream2）

（依据见 **§4.1** AN §4.2：新开传输前建议清除 stream 相关标志。）

`LIFCR` 用于清除 **Stream0~3** 上的挂起标志：`TCIF`、`HTIF`、`TEIF`、`DMEIF`、`FEIF` 等。

重装 DMA 前若 **传输完成/错误类标志仍挂着**，个别情况下会影响 **再次使能 `EN`** 后的行为。  
在 IDLE 里每次重装前 **按 Stream 号写对应的 `CxIF` 位** 是 AN 推荐的可靠重启做法，属于 **与 §4 配套的加固**，而非 `PAR`/HAL 类根因的唯一问题。

本工程使用 **DMA2 Stream2**（USART1 RX），故：

```c
DMA2->LIFCR = DMA_LIFCR_CTCIF2 | DMA_LIFCR_CHTIF2 | DMA_LIFCR_CTEIF2
            | DMA_LIFCR_CDMEIF2 | DMA_LIFCR_CFEIF2;
```

---

## 6. 其它已踩坑（简表）

| 点 | 说明 |
|----|------|
| 初始化顺序 | `UART Init` → 配 DMA（`PAR`/`M0AR`/`NDTR`）→ `DMAR` → `DMA EN` → 清 RX/ORE → `IDLEIE` → 最后 `printf` |
| 上电 `len==0` 的 IDLE | 开 `IDLEIE` 时线路已空闲，可能进一次空 IDLE；`len==0` 只重装 DMA 即可 |
| `printf` 与 IDLE | 每发一字可能触发一次 IDLE（若 `IDLEIE` 已开）；与 RX 有效收包不是同一回事 |
| 双缓冲 | `1`=缓冲空闲；交帧后置 `0`，主循环拷贝后清回 `1`；两格都满则计溢出并在当前缓冲上覆盖 |
| `fputc` | 用 `TXE` + 写 `DR`，勿再对同一口混用 `HAL_UART_Transmit` 状态机 |

---

## 7. 推荐接收链路（最终）

```text
PC ──115200 8N1──► USART1 RX (PA10)
                      │
                      ▼ DMAR + DMA2 Stream2
                 双缓冲 512×2
                      │
                      ▼ IDLE 中断：停 DMAR/DMA → 算长 → 换指针 → 清 LIFCR → 开 DMA → 开 DMAR
                      │
                      ▼ 主循环：读标志 → 拷贝 → 清标志 → 协议解析（后续）
```

---

## 8. 自检清单（以后换口/换板可复用）

1. `PAR == (uint32_t)&USARTx->DR`  
2. 异步口用 **UART HAL**，`CR2.CLKEN == 0`  
3. `CR1.RE`、`CR3.DMAR`、DMA `EN` 均为 1  
4. 发 1 字节并停顿：`NDTR` 减 1，进 IDLE  
5. 连发两包：第二包 `NDTR` 仍变化；否则检查 IDLE 里 **DMAR 重装 + LIFCR**  
6. 长时间不收：查 `ORE`，必要时 `读 SR 再读 DR` 清错误  

---

## 9. 与后续课题的关系

- 本口为 **Debug Console + 后续自定义帧**（`AA 55 | LEN | CLASS | CMD | ...`），缓冲可缩至约 **272×2** 以省 SRAM。  
- **lwIP / ETH DMA** 缓冲须放在 **0x20000000** 段 SRAM；CCM 不可给 DMA 用。  
- 业务协议与 SPI/I2C 命令在 **主循环** 处理，IDLE 里只做 DMA/缓冲管理。

---

*文档对应工程调试过程整理，便于博客发布；细节以当前 `usart.c` / `stm32f4xx_it.c` 为准。*
