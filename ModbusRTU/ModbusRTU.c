#include "ModbusRTU.h"

// -------------------- 内部静态函数声明 --------------------
static uint16_t CRC16_Modbus(const uint8_t *data, uint16_t len);
static MODBUS_Status Check_Frame_Integrity(const MODBUS_Device *dev, const uint8_t *frame, uint16_t *len);
static MODBUS_Status Build_Response(MODBUS_Device *dev, const uint8_t *req, uint16_t *req_len, uint8_t *rsp, uint16_t *rsp_len);
static MODBUS_Status Handle_Read_Registers(MODBUS_Device *dev, const uint8_t *req, uint8_t *rsp, uint16_t *rsp_len);
static MODBUS_Status Handle_Write_Register(MODBUS_Device *dev, const uint8_t *req, uint8_t *rsp, uint16_t *rsp_len);

// -------------------- 内部静态函数实现 --------------------

/**
 * @brief 计算 Modbus RTU CRC16 (多项式 0x8005, 初值 0xFFFF)
 */
static uint16_t CRC16_Modbus(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
            {
                crc = (crc >> 1) ^ 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief 检查帧完整性：最小长度、CRC校验、从机地址匹配
 * @return MB_OK 表示通过，其他表示错误类型（需静默丢弃）
 */
static MODBUS_Status Check_Frame_Integrity(const MODBUS_Device *dev, const uint8_t *frame, uint16_t *len)
{
    // 1. 最小帧长度检查：地址(1) + 功能码(1) + 数据(至少2) + CRC(2) = 最少4字节但03/06请求最少分别是8字节
    // 这里保守检查：至少 4 字节，具体功能码长度由上层 Handler 再次校验
    if (*len < 4)
    {
        return MB_ERR_LEN;
    }
    // 2. CRC 校验（计算除最后2字节外的CRC，与最后2字节比较）
    uint16_t crc_received = (uint16_t)(frame[*len - 1] << 8) | frame[*len - 2];
    uint16_t crc_calc = CRC16_Modbus(frame, *len - 2);
    if (crc_received != crc_calc)
    {
        return MB_ERR_CRC;
    }

    // 3. 从机地址匹配（广播地址 0x00 不做回复，视为匹配但不回复，这里过滤掉）
    if (frame[0] == 0x00 || frame[0] != dev->slave_id)
    {
        return MB_ERR_ADDR;
    }
    return MB_OK;
}

/**
 * @brief 处理读保持寄存器 (0x03)
 */
static MODBUS_Status Handle_Read_Registers(MODBUS_Device *dev, const uint8_t *req, uint8_t *rsp, uint16_t *rsp_len)
{
    // 请求帧格式: [地址] [0x03] [起始高] [起始低] [数量高] [数量低] [CRC低] [CRC高]
    // 最少 8 字节
    uint16_t start_addr = (uint16_t)(req[2] << 8) | req[3];
    uint16_t reg_num = (uint16_t)(req[4] << 8) | req[5];

    // 校验寄存器数量（必须 >=1 且 不超过 125，Modbus最大允许125个寄存器）
    if (reg_num < 1 || reg_num > 125)
    {
        return MB_ERR_EXCEPTION;
    }
    // 校验地址范围
    if (start_addr + reg_num > dev->reg_count)
    {
        return MB_ERR_EXCEPTION;
    }
    // 数据拷贝的同时增加读取计数
    if (dev->read_count != NULL)
    { // 允许不开启统计（传入NULL）
        for (uint16_t i = 0; i < reg_num; i++)
        {
            dev->read_count[start_addr + i]++; // 每个被读到的寄存器，次数+1
        }
    }
    // 构建正常响应：地址 + 功能码 + 字节数 + 数据... + CRC
    rsp[0] = dev->slave_id;
    rsp[1] = 0x03;
    rsp[2] = (uint8_t)(reg_num * 2); // 数据字节数

    uint16_t idx = 3;
    for (uint16_t i = 0; i < reg_num; i++)
    {
        uint16_t reg_val = dev->holding_regs[start_addr + i];
        rsp[idx++] = (uint8_t)(reg_val >> 8); // 高字节在前
        rsp[idx++] = (uint8_t)(reg_val & 0xFF);
    }

    // 计算 CRC (从 rsp[0] 到 rsp[idx-1])
    uint16_t crc = CRC16_Modbus(rsp, idx);
    rsp[idx++] = (uint8_t)(crc & 0xFF);
    rsp[idx++] = (uint8_t)(crc >> 8);
    *rsp_len = idx;

    return MB_OK;
}

/**
 * @brief 处理写单个寄存器 (0x06)
 */
static MODBUS_Status Handle_Write_Register(MODBUS_Device *dev, const uint8_t *req, uint8_t *rsp, uint16_t *rsp_len)
{
    // 请求帧格式: [地址] [0x06] [地址高] [地址低] [数据高] [数据低] [CRC低] [CRC高]
    // 最少 8 字节
    uint16_t reg_addr = (uint16_t)(req[2] << 8) | req[3];
    uint16_t reg_val = (uint16_t)(req[4] << 8) | req[5];

    // 校验地址范围
    if (reg_addr >= dev->reg_count)
    {
        return MB_ERR_EXCEPTION;
    }
    // 写入数据的同时增加写入计数
    if (dev->write_count != NULL)
    {
        dev->write_count[reg_addr]++; // 该寄存器被写入次数+1
        // 写入后读取清零
        dev->read_count[reg_addr] = 0;
    }
    // 写入寄存器
    dev->holding_regs[reg_addr] = reg_val;

    // 构建正常响应：写单个寄存器的响应 = 原样回显请求（标准 Modbus 规定）
    // 直接复制请求前 6 字节（地址+功能码+地址+数据），然后加上 CRC
    rsp[0] = req[0];
    rsp[1] = req[1];
    rsp[2] = req[2];
    rsp[3] = req[3];
    rsp[4] = req[4];
    rsp[5] = req[5];

    uint16_t crc = CRC16_Modbus(rsp, 6);
    rsp[6] = (uint8_t)(crc & 0xFF);
    rsp[7] = (uint8_t)(crc >> 8);
    *rsp_len = 8;

    return MB_OK;
}

/**
 * @brief 根据功能码分发处理
 */
static MODBUS_Status Build_Response(MODBUS_Device *dev, const uint8_t *req, uint16_t *req_len, uint8_t *rsp, uint16_t *rsp_len)
{
    uint8_t func_code = req[1];
    // 针对不同功能码校验最小帧长
    switch (func_code)
    {
    case 0x03:
        if (*req_len != 8)
        { // 固定长度
            return MB_ERR_LEN;
        }
        return Handle_Read_Registers(dev, req, rsp, rsp_len);

    case 0x06:
        if (*req_len != 8)
        { // 固定长度
            return MB_ERR_LEN;
        }
        return Handle_Write_Register(dev, req, rsp, rsp_len);

    default:
        return MB_ERR_EXCEPTION;
    }
}

// -------------------- 公共接口函数实现 --------------------

void MODBUS_Init(MODBUS_Device *dev,
                 uint8_t slave_id,
                 uint16_t *holding_regs,
                 uint16_t *read_count,
                 uint16_t *write_count,
                 uint16_t reg_count)
{
    dev->slave_id = slave_id;
    dev->holding_regs = holding_regs;
    dev->read_count = read_count;
    dev->write_count = write_count;
    dev->reg_count = reg_count;
}

MODBUS_Status MODBUS_Process_Frame(MODBUS_Device *dev,
                                   const uint8_t *frame,
                                   uint16_t *frame_len,
                                   uint8_t *response,
                                   uint16_t *response_len)
{
    // 默认无响应
    *response_len = 0;

    // 1. 完整性检查（CRC、地址、长度）
    MODBUS_Status status = Check_Frame_Integrity(dev, frame, frame_len);
    if (status != MB_OK)
    {
        *frame_len = 0;
        // 对于 CRC/地址/长度错误，直接丢弃，不生成任何响应
        return status;
    }

    // 2. 构建响应（正常或异常）
    status = Build_Response(dev, frame, frame_len, response, response_len);

    // 如果 Build_Response 返回了 MB_ERR_EXCEPTION，说明 response 中已填充异常帧
    // 如果是 MB_OK，说明填充了正常帧
    // 其他错误（如 MB_ERR_LEN 在本层已被 Check 拦截，但以防万一）
    *frame_len = 0;
    return status;
}
