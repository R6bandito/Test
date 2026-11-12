#ifndef __SDIO_DRIVER_H__
#define __SDIO_DRIVER_H__


#include "stm32f4xx_hal.h"


#define SDIO_GPIO_DAT_PORT          (GPIOC)
#define SDIO_GPIO_DAT_0             (GPIO_PIN_8)
#define SDIO_GPIO_DAT_1             (GPIO_PIN_9)
#define SDIO_GPIO_DAT_2             (GPIO_PIN_10)
#define SDIO_GPIO_DAT_3             (GPIO_PIN_11)
#define SDIO_GPIO_DAT_PORT_EN()     do { __HAL_RCC_GPIOC_CLK_ENABLE(); } while(0)

#define SDIO_GPIO_CMD_PORT          (GPIOD)
#define SDIO_GPIO_CMD               (GPIO_PIN_2)
#define SDIO_GPIO_CMD_PORT_EN()     do { __HAL_RCC_GPIOD_CLK_ENABLE(); } while(0)

#define SDIO_GPIO_CLK_PORT          (GPIOC)
#define SDIO_GPIO_CLK               (GPIO_PIN_12)
#define SDIO_GPIO_CLK_PORT_EN()     do { __HAL_RCC_GPIOC_CLK_ENABLE(); } while(0) 


typedef enum {
    CARD_SPEED_24Mhz   = 0,
    CARD_SPEED_16Mhz   = 1,
    CARD_SPEED_12Mhz   = 2,
    CARD_SPEED_8Mhz    = 4,
    CARD_SPEED_DEFAULT = 99,
} CardSpeed_t;


typedef enum {
    CARD_STATE_INVALID      = 0,
    CARD_STATE_TRANSFER     = 1,
    CARD_STATE_SENDING_DATA = 2,
    CARD_STATE_RCVING_DATA  = 3,
    CARD_STATE_PROGRAMING   = 4,
    CARD_STATE_NOT_READY    = 99,
} CardState_t;


typedef struct {
    uint32_t ocr;
    uint32_t cid[4];  /* RESP1 到 RESP4，按高字到低字保存。 */
    uint32_t csd[4];  /* 同 CID。 */
    uint16_t rca;
    CardState_t state;  /* 最近查询的快照；不是协议 CURRENT_STATE 的原始编码。 */
    CardSpeed_t speed;  /* 当前主机时钟配置；DEFAULT 表示识别阶段低速，不是最大能力。 */
} SDCard;

void SDIO_HostInit( void );
/* 在 delay_init() 和 SDIO_HostInit() 后调用。timeout_ms 是每条命令的超时，须大于 0。
 * 开始重连时清除旧卡信息；HAL_OK 仅表示 CMD8 回显通过。 */
HAL_StatusTypeDef SDIO_CardConnection( SDCard *card, uint32_t timeout_ms );

/* 在 SDIO_CardConnection() 成功后调用。timeout_ms 是整个流程的预算。
 * 仅在成功选卡并确认 TRAN 后写入 card。失败后从 CMD0 重新开始。 */
HAL_StatusTypeDef SDIO_TransferUP( SDCard *card, uint32_t timeout_ms );

/* 单次 CMD13 查询，要求有效非零 RCA，调用者须串行访问控制器。
 * HAL_OK 表示查询成功，不代表可传输；仅 TRAN 且 READY 时映射为 TRANSFER。
 * 其他合法中间态映射为 NOT_READY；失败置 INVALID，允许用原 RCA 再查询。
 * 不等待卡变为 TRANSFER，timeout_ms 仅限制本次命令。 */
HAL_StatusTypeDef SDIO_GetCardState( SDCard *card, uint32_t timeout_ms );


#endif /* __SDIO_DRIVER_H__ */
