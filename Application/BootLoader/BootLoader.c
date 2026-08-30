#include "BootLoader.h"
#include "boot_decode.h"

static uint32_t write_cache = 0;
static uint8_t cache_bytes = 0;
static uint32_t current_addr = APP_ADDRESS; // 设定起始地址

static uint32_t fw_size = 0; /* 解压后固件大小（首包 4 字节头） */
static uint32_t decoded_total = 0; /* 已解压写入 Flash 的字节数 */
static uint8_t is_first = 1;
static uint8_t flash_write_failed = 0; /* Flash 写入失败标志 */

/**
 * @brief 检查当前 Flash 中的 APP 是否有效
 * @return true 有效，false 无效
 * @note
 */
bool CheckAppValid(void) {
  uint32_t magic = *(volatile uint32_t *)MAGIC_ADDR;
  return (magic == APP_VALID_MAGIC);
}

/**
 * @brief 跳转到应用程序
 * @note 该函数在验证 APP
 */
void BootJumpAPP(void) {
  if (!CheckAppValid())
    return;
  uint32_t sp, pc;
  pFunction Jump_To_Application; // 局部函数指针，不做全局

  sp = *(volatile uint32_t *)APP_ADDRESS;
  pc = *(volatile uint32_t *)(APP_ADDRESS + 4);

  if ((sp & 0x7UL) != 0)
    return;
  if (sp < 0x20000000UL || sp > 0x20010000UL)
    return;

  // 校验 PC 指针是否在 APP Flash 范围
  if (pc < APP_ADDRESS || pc >= (APP_ADDRESS + APP_SIZE))
    return;

  HAL_RCC_DeInit();

  HAL_DeInit();

  for (int i = 0; i < 2; i++) {
    NVIC->ICER[i] = 0xFFFFFFFFUL;
    NVIC->ICPR[i] = 0xFFFFFFFFUL;
  }

  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;

  SCB->ICSR |= SCB_ICSR_PENDSVCLR_Msk | SCB_ICSR_PENDSTCLR_Msk;

  __disable_irq();
  __DSB();
  __ISB();

  SCB->VTOR = APP_ADDRESS;

  __set_MSP(sp);
  Jump_To_Application = (pFunction)pc;

  // 函数指针跳转
  __enable_irq();
  Jump_To_Application();

  while (1)
    ;
}

/**
 * @brief 返回待写入地址
 */
uint32_t ReturnCurrentAddr(void) { return current_addr; }

/*
 *@brief  获取指定地址所在的 Flash 扇区
 *@param  addr: Flash 地址
 *@retval 扇区号（0~5)
 */
static inline uint32_t GetSector(uint32_t addr) {
  if (addr <= SECTOR_0_END)
    return 0;
  if (addr <= SECTOR_1_END)
    return 1;
  if (addr <= SECTOR_2_END)
    return 2;
  if (addr <= SECTOR_3_END)
    return 3;
  if (addr <= SECTOR_4_END)
    return 4;
  return 5;
}

/*
 * @brief 写入单个 32 位进入 Flash
 * @param addr: 目标地址（必须 4 字节对齐)
 * @param data: 32 位待写入数据
 * @return 返回写入状态
 */
static uint8_t Flash_WriteWord(uint32_t addr, uint32_t data) {
  // 32 位字写入要求 4 字节对齐
  if (addr & 0x3UL)
    return BL_WRITE_ERROR;

  if (addr > (FLASH_END - 3UL))
    return BL_WRITE_ERROR;

  // 写入前清空所有错误标
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR |
                         FLASH_FLAG_PGSERR);

  // 单次 32 位字写入
  HAL_StatusTypeDef status =
      HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, data);
  if (status != HAL_OK)
    return BL_WRITE_ERROR;

  // 回读校验
  if (*(volatile uint32_t *)addr != data)
    return BL_WRITE_ERROR;

  return BL_OK;
}

/*
 * @brief 将单个字节送入 4 字节缓存，凑满后写入
 * Flash 单字
 * @param byte: 待写入的字节
 * @return 写入状态
 */
static uint8_t Flash_WriteByte(uint8_t byte) {
  /* 将新字节存入缓存（小端模式） */
  ((uint8_t *)&write_cache)[cache_bytes] = byte;
  cache_bytes++;

  /* 凑满 4 字节，执行一次单字写 */
  if (cache_bytes == 4) {
    if (Flash_WriteWord(current_addr, write_cache) != BL_OK) {
      ULOG_ERROR("Failed to write word to Flash at address 0x%08lX",
                 (unsigned long)current_addr);
      return BL_WRITE_ERROR;
    }
    current_addr += 4;
    write_cache = 0;
    cache_bytes = 0;
  }
  return BL_OK;
}

/*
 * @brief 解压输出回调：将解压出的字节写入 Flash
 * @param out: 解压输出数据
 * @param len: 数据长度
 * @return 实际处理的字节数
 */
static size_t Bootloader_DecodeSink(const uint8_t *out, size_t len) {
  for (size_t i = 0; i < len; i++) {
    /* 已写满预期大小，丢弃填充产生的多余输
     */
    if (decoded_total >= fw_size)
      break;

    if (Flash_WriteByte(out[i]) != BL_OK) {
      flash_write_failed = 1;
      return i;
    }
    decoded_total++;
  }
  return len;
}

/*
 * @brief 初始 Bootloader
 * @return 返回初始化状态
 */
uint8_t Bootloader_Init(void) {
  /* 使能 Flash 接口时钟（HAL
   * 已自动处理），只需解锁清除错误标志
   */
  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR |
                         FLASH_FLAG_PGSERR);
  HAL_FLASH_Lock();
  return BL_OK;
}

/*
 * @brief 擦除应用程序区域 Flash
 * @return 返回擦除状态
 */
uint8_t Bootloader_Erase(void) {
  ULOG_DEBUG("Erasing APP Flash sectors 2~5...");
  FLASH_EraseInitTypeDef erase_init = {0};
  uint32_t sector_error = 0;

  HAL_FLASH_Unlock();

  erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase_init.Sector = APP_SECTOR_START;
  erase_init.NbSectors = APP_SECTOR_COUNT;
  erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3; /* F401 固定�? 3 */

  __disable_irq();

  HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase_init, &sector_error);

  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR |
                         FLASH_FLAG_PGSERR);
  __enable_irq();

  HAL_FLASH_Lock();

  if (status != HAL_OK || sector_error != 0xFFFFFFFF)
    return BL_ERASE_ERROR;

  return BL_OK;
}

/*
 * @brief 开始 Flash 写入操作
 * @return 返回状态
 */
uint8_t Bootloader_FlashBegin(void) {
  ULOG_DEBUG("Starting Flash write operation...");
  current_addr = APP_ADDRESS;
  write_cache = 0;
  cache_bytes = 0;

  fw_size = 0;
  decoded_total = 0;
  is_first = 1;

  boot_decode_init();

  HAL_FLASH_Unlock();

  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR |
                         FLASH_FLAG_PGSERR);
  return BL_OK;
}

/*
 *@brief 写入数据到 Flash（以 8 字节为单位写入）
 *@param data: 指向要写入的数据缓冲区
 *@param len: 数据长度
 *@return 返回写入状态
 */
uint8_t Bootloader_FlashWriteBuffer(uint8_t *data, uint16_t len) {
  ULOG_DEBUG("Writing data to Flash and the length is %d", len);

  if (len == 0)
    return BL_OK;

  __disable_irq(); /* 写入期间关闭中断 */

  if (is_first) {
    if (len < 4) {
      __enable_irq();
      return BL_SIZE_ERROR;
    }
    // 读取4字节小端长度
    fw_size = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
              ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);

    // 合法性检查：解压后大小必须落在 APP 区域
    if (fw_size == 0 || fw_size > APP_SIZE) {
      __enable_irq();
      return BL_SIZE_ERROR;
    }

    data += 4;
    len -= 4;
    is_first = 0;
  }

  flash_write_failed = 0;
  if (boot_decode_stream(data, len, Bootloader_DecodeSink) == (size_t)-1) {
    __enable_irq();
    HAL_FLASH_Lock();
    return flash_write_failed ? BL_WRITE_ERROR : BL_DECODE_ERROR;
  }

  __enable_irq();
  return BL_OK;
}

/*
 * @brief 结束 Flash 写入操作，处理剩余缓存
 * @return 返回状态
 */
uint8_t Bootloader_FlashEnd(void) {
  ULOG_DEBUG("Ending Flash write operation...");

  /* 冲刷解压器末尾残留输出并写入 Flash */
  flash_write_failed = 0;
  __disable_irq();
  if (boot_decode_stream_finish(Bootloader_DecodeSink) == (size_t)-1) {
    __enable_irq();
    HAL_FLASH_Lock();
    return flash_write_failed ? BL_WRITE_ERROR : BL_DECODE_ERROR;
  }
  __enable_irq();

  // 处理剩余缓存数据(不足 4 字节时，高位填充 0xFF)
  if (cache_bytes > 0) {
    // 剩余不足 4 字节时，高位填充 0xFF
    uint32_t word_to_write = 0xFFFFFFFFUL;
    // 有效字节复制到低位（小端模式)
    for (uint8_t i = 0; i < cache_bytes; i++) {
      ((uint8_t *)&word_to_write)[i] = ((uint8_t *)&write_cache)[i];
    }

    __disable_irq();
    if (Flash_WriteWord(current_addr, word_to_write) != BL_OK) {
      __enable_irq();
      HAL_FLASH_Lock();
      return BL_WRITE_ERROR;
    }
    current_addr += 4;
    cache_bytes = 0;
    __enable_irq();
  }

  /* 校验解压后大小 */
  if (decoded_total != fw_size) {
    ULOG_ERROR("Decoded size mismatch: expected %lu, got %lu",
               (unsigned long)fw_size, (unsigned long)decoded_total);
    HAL_FLASH_Lock();
    return BL_SIZE_ERROR;
  }

  /* 锁定 Flash */
  HAL_FLASH_Lock();
  return BL_OK;
}
