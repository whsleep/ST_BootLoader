#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#include "stm32f4xx_hal.h"
#include "stdbool.h"
#include "ulog.h"

/* 错误码 */
enum eBootloaderErrorCodes
{
    BL_OK = 0,
    BL_NO_APP,
    BL_SIZE_ERROR,
    BL_CHKS_ERROR,
    BL_ERASE_ERROR,
    BL_WRITE_ERROR,
    BL_DECODE_ERROR,
    BL_OBP_ERROR
};

/* ======================= BootLoader 配置 ======================= */
#define BOOT_SIZE 0x8000U /* 32 KB */
#define APP_SIZE 0x38000U /* 224 KB */

// 验证尺寸是否合法
#if (BOOT_SIZE + APP_SIZE) != FLASH_END - FLASH_BASE + 1
#error "BOOT_SIZE + APP_SIZE must equal total Flash size (256 KB)"
#endif

#define BOOT_ADDRESS (FLASH_BASE)            /* Bootloader 起始地址 */
#define APP_ADDRESS (FLASH_BASE + BOOT_SIZE) /* 应用程序起始地址 */
#define END_ADDRESS (FLASH_END)              /* 应用程序结束地址 */

// APP魔数地址
#define LAST_IRQn SPI4_IRQn                                // 最后一个中断向量
#define VECT_TABLE_SIZE ((1 + 16 + ((LAST_IRQn) + 1)) * 4) // 408
#define MAGIC_ADDR (FLASH_BASE + BOOT_SIZE + VECT_TABLE_SIZE)
#define APP_VALID_MAGIC 0x5A5AU

typedef void (*pFunction)(void);

/* ======================= Flash 布局定义 ======================= */
/**
 * 注：STM32F401CCU6 Flash 组织（单 Bank，共256KB）
 *   扇区0 : 0x0800 0000 - 0x0800 3FFF  16 KB
 *   扇区1 : 0x0800 4000 - 0x0800 7FFF  16 KB
 *   扇区2 : 0x0800 8000 - 0x0800 BFFF  16 KB  <-- APP 从此开始
 *   扇区3 : 0x0800 C000 - 0x0800 FFFF  16 KB
 *   扇区4 : 0x0801 0000 - 0x0801 FFFF  64 KB
 *   扇区5 : 0x0802 0000 - 0x0803 FFFF 128 KB
 * 应用程序区 = 扇区2 ~ 扇区5，共 224 KB
 */

/* Flash 扇区范围定义（STM32F401xC，单位：字节） */
#define SECTOR_0_BASE 0x08000000UL
#define SECTOR_0_END 0x08003FFFUL
#define SECTOR_0_SIZE (SECTOR_0_END - SECTOR_0_BASE + 1) /* 16 KB */

#define SECTOR_1_BASE 0x08004000UL
#define SECTOR_1_END 0x08007FFFUL
#define SECTOR_1_SIZE (SECTOR_1_END - SECTOR_1_BASE + 1) /* 16 KB */

#define SECTOR_2_BASE 0x08008000UL
#define SECTOR_2_END 0x0800BFFFUL
#define SECTOR_2_SIZE (SECTOR_2_END - SECTOR_2_BASE + 1) /* 16 KB */

#define SECTOR_3_BASE 0x0800C000UL
#define SECTOR_3_END 0x0800FFFFUL
#define SECTOR_3_SIZE (SECTOR_3_END - SECTOR_3_BASE + 1) /* 16 KB */

#define SECTOR_4_BASE 0x08010000UL
#define SECTOR_4_END 0x0801FFFFUL
#define SECTOR_4_SIZE (SECTOR_4_END - SECTOR_4_BASE + 1) /* 64 KB */

#define SECTOR_5_BASE 0x08020000UL
#define SECTOR_5_END 0x0803FFFFUL
#define SECTOR_5_SIZE (SECTOR_5_END - SECTOR_5_BASE + 1) /* 128 KB */

/* 应用程序扇区数量（2,3,4,5 共 4 个） */
#define APP_SECTOR_START FLASH_SECTOR_2
#define APP_SECTOR_COUNT 4

uint8_t Bootloader_Init(void);
uint8_t Bootloader_Erase(void);
uint8_t Bootloader_FlashBegin(void);
uint8_t Bootloader_FlashWriteBuffer(uint8_t *data, uint16_t len); /* 供回调调用 */
uint8_t Bootloader_FlashEnd(void);

bool CheckAppValid(void);
void BootJumpAPP(void);
uint32_t ReturnCurrentAddr(void);

#endif /* __BOOTLOADER_H */