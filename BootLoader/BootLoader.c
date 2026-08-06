#include "BootLoader.h"

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