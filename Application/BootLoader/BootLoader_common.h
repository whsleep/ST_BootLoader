#ifndef __BOOTLOADER_COMMON_H
#define __BOOTLOADER_COMMON_H

#include "BootLoader.h"
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_VER_MAJOR 1
#define BOOT_VER_MINOR 0
#define BOOT_VER_PATCH 0

#define VERSION_INFO_SIZE 64
#define VERSION_INFO_ADDR (END_ADDRESS - VERSION_INFO_SIZE + 1)

/**
 * @brief 版本信息结构体（紧凑布局，无填充）
 */
typedef struct __attribute__((packed)) {
  uint16_t app_ver_major; /*!< APP 主版本号 */
  uint16_t app_ver_minor; /*!< APP 次版本号 */
  uint16_t app_ver_patch; /*!< APP 补丁版本号 */
  uint16_t reserved;      /*!< 保留字段 */
  uint32_t app_end_addr; /*!< APP 实际结束地址（APP_ADDRESS + 固件大小） */
  uint32_t crc32; /*!< APP 固件 CRC32 校验值（可选，0 表示无效） */
} version_info_t;

/* ======================= 函数定义（内联实现） ======================= */

/**
 * @brief  从 Flash 读取版本信息
 * @param  info : 输出参数，指向存放版本信息的结构体
 * @note   该函数仅进行内存拷贝，不涉及 Flash 擦写，可安全用于 Bootloader 和
 * APP。
 */
static inline void Version_Read(version_info_t *info) {
  if (info == NULL)
    return;

  memcpy(info, (const void *)VERSION_INFO_ADDR, sizeof(version_info_t));
}

/**
 * @brief  写入版本信息到 Flash（目标区域必须已处于擦除态）
 * @param  info : 指向要写入的版本信息结构体
 * @return 0 表示成功，非 0 表示失败（错误码可由外部定义，如 BL_WRITE_ERROR）
 * @note   此函数不执行擦除操作，调用前需确保目标 Flash 区域已被擦除。
 */
static inline int Version_Write(const version_info_t *info) {
  if (info == NULL)
    return -1;

  /* 检查地址是否合法且 4 字节对齐 */
  if (VERSION_INFO_ADDR & 0x3UL)
    return -2;

  /* 检查版本信息区是否会越界（超出 Flash 末尾） */
  if (VERSION_INFO_ADDR + sizeof(version_info_t) - 1 > FLASH_END)
    return -3;

  HAL_StatusTypeDef status;
  uint32_t addr = VERSION_INFO_ADDR;
  uint32_t *pSrc = (uint32_t *)info;
  uint32_t words = (sizeof(version_info_t) + 3) / 4;

  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR |
                         FLASH_FLAG_PGSERR);

  __disable_irq(); /* 写入期间关闭中断，防止 Flash 操作冲突 */

  for (uint32_t i = 0; i < words; i++) {
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, *pSrc);
    if (status != HAL_OK) {
      __enable_irq();
      HAL_FLASH_Lock();
      return -4;
    }

    /* 回读校验 */
    if (*(volatile uint32_t *)addr != *pSrc) {
      __enable_irq();
      HAL_FLASH_Lock();
      return -5;
    }

    addr += 4;
    pSrc++;
  }

  __enable_irq();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR |
                         FLASH_FLAG_PGSERR);
  HAL_FLASH_Lock();

  return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* __BOOTLOADER_COMMON_H */