#include "BootLoader.h"

static uint32_t write_cache = 0;
static uint8_t cache_bytes = 0;
static uint32_t current_addr = APP_ADDRESS; // 设定起始地址

/**
 * @brief 检查当前 Flash 中的 APP 是否有效
 * @return true 有效，false 无效
 * @note 用户需根据实际硬件实现（CRC、签名等）
 */
bool CheckAppValid(void)
{
    uint32_t magic = *(volatile uint32_t *)MAGIC_ADDR;
    return (magic == APP_VALID_MAGIC);
}

/**
 * @brief 跳转到应用程序
 * @note 该函数在验证 APP 有效后调用，用于跳转到应用程序入口
 */
void BootJumpAPP(void)
{
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

    // 校验 PC 在 APP Flash 范围内
    if (pc < APP_ADDRESS || pc >= (APP_ADDRESS + APP_SIZE))
        return;

    // 复位 RCC 时钟配置：切回 HSI，关闭 PLL/HSE，恢复复位默认值
    HAL_RCC_DeInit();
    // 2. 反初始化所有 HAL 外设
    HAL_DeInit();

    // 3. 关闭所有外设中断并清除挂起位
    for (int i = 0; i < 2; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    // 4. 完全复位 SysTick
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    // 5. 清除系统异常挂起位
    SCB->ICSR |= SCB_ICSR_PENDSVCLR_Msk | SCB_ICSR_PENDSTCLR_Msk;

    // 6. 关全局中断 + 指令同步
    __disable_irq();
    __DSB();
    __ISB();

    // 7. 重定向中断向量表
    SCB->VTOR = APP_ADDRESS;

    // 8. 设置 MSP，绑定跳转入口
    __set_MSP(sp);
    Jump_To_Application = (pFunction)pc;

    // 9. 函数指针跳转
    __enable_irq();
    Jump_To_Application();

    // 兜底：跳转失败则死循环（正常不会执行到这里）
    while (1)
        ;
}

/*
 *@brief  获取指定地址所在的 Flash 扇区号
 *@param  addr: Flash 地址
 *@retval 扇区号（0~5）
 */
static inline uint32_t GetSector(uint32_t addr)
{
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
 * @brief 写入单个 32 位字到 Flash
 * @param addr: 目标地址（必须 4 字节对齐）
 * @param data: 32 位待写入数据
 * @return 返回写入状态
 */
static uint8_t Flash_WriteWord(uint32_t addr, uint32_t data)
{
    // 32 位字写入要求 4 字节对齐
    if (addr & 0x3UL)
        return BL_WRITE_ERROR;
    // 地址越界检查（预留 3 字节余量，保证单字写入不超 Flash 范围）
    if (addr > (FLASH_END - 3UL))
        return BL_WRITE_ERROR;

    // 写入前清空所有错误标志
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    // 单次 32 位字写入
    HAL_StatusTypeDef status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, data);
    if (status != HAL_OK)
        return BL_WRITE_ERROR;

    // 回读校验
    if (*(volatile uint32_t *)addr != data)
        return BL_WRITE_ERROR;

    return BL_OK;
}

/*
 * @brief 初始化 Bootloader
 * @return 返回初始化状态
 */
uint8_t Bootloader_Init(void)
{
    /* 使能 Flash 接口时钟（HAL 已自动处理），只需解锁清除错误标志 */
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    HAL_FLASH_Lock();
    return BL_OK;
}

/*
 * @brief 擦除应用程序区域的 Flash
 * @return 返回擦除状态
 */
uint8_t Bootloader_Erase(void)
{
    FLASH_EraseInitTypeDef erase_init = {0};
    uint32_t sector_error = 0;

    HAL_FLASH_Unlock();

    /* 配置擦除参数：从扇区 2 开始，擦除 4 个扇区（2,3,4,5） */
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Sector = APP_SECTOR_START;
    erase_init.NbSectors = APP_SECTOR_COUNT;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3; /* F401 固定为 3 */

    /* 关闭全局中断（防止擦除期间中断触发） */
    __disable_irq();

    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase_init, &sector_error);
    // 解锁后立即清除所有错误标志 + EOP 标志，确保状态寄存器干净
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
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
uint8_t Bootloader_FlashBegin(void)
{
    current_addr = APP_ADDRESS;
    write_cache = 0;
    cache_bytes = 0;

    HAL_FLASH_Unlock();
    // 解锁后立即清除所有错误标志 + EOP 标志，确保状态寄存器干净
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    return BL_OK;
}

/*
 *@brief 写入数据到 Flash（以 8 字节为单位写入）
 *@param data: 指向要写入的数据缓冲区
 *@param len: 数据长度（字节数）
 *@return 返回写入状态
 *@note 该函数会缓存数据，直到缓存满 8 字节才执行实际写入操作
 */
uint8_t Bootloader_FlashWriteBuffer(uint8_t *data, uint16_t len)
{
    uint16_t i = 0;

    if (len == 0)
        return BL_OK;

    /* 检查地址是否越界（预留 8 字节余量） */
    if (current_addr > (FLASH_END - 8))
    {
        HAL_FLASH_Lock();
        return BL_WRITE_ERROR;
    }

    __disable_irq(); /* 写入期间关闭中断 */

    for (i = 0; i < len; i++)
    {
        /* 将新字节存入缓存（小端模式） */
        ((uint8_t *)&write_cache)[cache_bytes] = data[i];
        cache_bytes++;

        /* 凑满 4 字节，执行一次单字写入 */
        if (cache_bytes == 4)
        {
            if (Flash_WriteWord(current_addr, write_cache) != BL_OK)
            {
                __enable_irq();
                HAL_FLASH_Lock();
                Error_Handler();
                return BL_WRITE_ERROR;
            }
            current_addr += 4;
            write_cache = 0;
            cache_bytes = 0;
        }
    }

    __enable_irq();
    return BL_OK;
}

/*
 * @brief 结束 Flash 写入操作，处理剩余缓存
 * @return 返回状态
 */
uint8_t Bootloader_FlashEnd(void)
{
    if (cache_bytes > 0)
    {
        // 剩余不足 4 字节时，高位填充 0xFF
        uint32_t word_to_write = 0xFFFFFFFFUL;
        // 有效字节复制到低位（小端模式）
        for (uint8_t i = 0; i < cache_bytes; i++)
        {
            ((uint8_t *)&word_to_write)[i] = ((uint8_t *)&write_cache)[i];
        }

        __disable_irq();
        if (Flash_WriteWord(current_addr, word_to_write) != BL_OK)
        {
            __enable_irq();
            HAL_FLASH_Lock();
            return BL_WRITE_ERROR;
        }
        current_addr += 4;
        cache_bytes = 0;
        __enable_irq();
    }

    /* 锁定 Flash */
    HAL_FLASH_Lock();
    return BL_OK;
}
