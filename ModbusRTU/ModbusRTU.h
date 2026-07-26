#ifndef __MODBUS_RTU_H__
#define __MODBUS_RTU_H__

#include <stdint.h>
#include <stddef.h>

// 定义返回状态枚举
typedef enum
{
    MB_OK = 0x00,       // 处理成功，已生成正常响应
    MB_ERR_CRC = 0x01,  // CRC校验失败（静默丢弃）
    MB_ERR_LEN = 0x02,  // 帧长度错误（静默丢弃）
    MB_ERR_ADDR = 0x03, // 从机地址不匹配（静默丢弃）
    MB_ERR_FUNC = 0x04, // 功能码不支持（将生成异常响应）
    MB_ERR_EXCEPTION = 0x05
} MODBUS_Status;

// 定义 Modbus 从机设备结构体
typedef struct
{
    uint8_t slave_id;       // 本机从机地址 (1~247)
    uint16_t *holding_regs; // 保持寄存器数组指针（外部提供）
    uint16_t *read_count;   // 指向“读取次数”数组的指针（可选，若为NULL则不统计）
    uint16_t *write_count;  // 指向“写入次数”数组的指针（可选，若为NULL则不统计）
    uint16_t reg_count;     // 保持寄存器总数量（用于边界检查）
} MODBUS_Device;

/* 公共接口函数 */

/**
 * @brief 初始化 Modbus 设备结构体
 * @param dev       设备结构体指针
 * @param slave_id  本机从机地址
 * @param holding_regs 外部寄存器数组指针
 * @param read_count 读取次数数组指针
 * @param write_count 写入次数数组指针
 * @param reg_count  寄存器数组长度（单位：16位寄存器）
 */
void MODBUS_Init(MODBUS_Device *dev,
                 uint8_t slave_id,
                 uint16_t *holding_regs,
                 uint16_t *read_count,
                 uint16_t *write_count,
                 uint16_t reg_count);
/**
 * @brief 处理一帧完整的 Modbus RTU 数据（核心函数）
 * @param dev          设备结构体指针
 * @param frame        接收到的原始帧数组（完整一帧）
 * @param frame_len    帧长度（字节数）
 * @param response     输出响应帧缓冲区（由调用者提供）
 * @param response_len 输出响应帧实际长度（若为0表示无需回复）
 * @return MODBUS_Status 处理状态码
 */
MODBUS_Status MODBUS_Process_Frame(MODBUS_Device *dev,
                                   const uint8_t *frame,
                                   uint16_t *frame_len,
                                   uint8_t *response,
                                   uint16_t *response_len);

#endif
