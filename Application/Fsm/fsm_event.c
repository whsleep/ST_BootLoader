#include "fsm_event.h"

extern volatile uint16_t Modbus_read_regs[];
extern volatile uint16_t Modbus_write_regs[];
extern uint16_t Modbus_holding_regs[];
extern Modbus_CommContext mb_comm;
extern MODBUS_Device Modbus_dev0;
extern uint8_t rxdata;
extern XMODEM_Device xmodem;
extern version_info_t version;

/* �� Modbus �Ĵ�����ȡ�������� */
#define MODBUS_06_REC Modbus_write_regs[0]
#define MODBUS_03_REC Modbus_read_regs[0]
#define MODBUS_REC Modbus_holding_regs[0]

#define APP_VERSION_MAJOR Modbus_holding_regs[1]
#define APP_VERSION_MINOR Modbus_holding_regs[2]
#define APP_VERSION_PATCH Modbus_holding_regs[3]

/* ---------------- ״̬ STATE_MODBUS_RECV ---------------- */

/**
 * @brief ����ȴ�����״̬��������ʱ��ʱ��?
 */
void ModbusRecv_Entry(void) {
  ULOG_INFO("Entering STATE_MODBUS_RECV");
  StartTimeout();
}

/**
 * @brief �ȴ�����״̬�ĺ���ִ�У���������ʱ
 * @return �������¼��������¼��򷵻� EVENT_NONE
 */
BootEvent ModbusRecv_Do(void) {
  // ����Ƿ��յ��Ϸ���������������?
  if (MODBUS_REC == 0x5B5B && MODBUS_06_REC > 0 && MODBUS_03_REC > 0) {
    ULOG_INFO("Received legal 0x06 command, triggering upgrade");
    StopTimeout(); // �յ����ֹͣ��ʱ
    return EVENT_RECV_LEGAL_06H;
  }
  if (mb_comm.rx_len > 0) {
    uint16_t len = mb_comm.rx_len;
    mb_comm.rx_len = 0; // �����ͷű�־λ�������жϼ�¼��֡
    MODBUS_Status sta =
        MODBUS_Process_Frame(&Modbus_dev0, mb_comm.rx_buffer, &len,
                             mb_comm.tx_buffer, &mb_comm.tx_len);
    if (sta == MB_OK) {
      HAL_UART_Transmit(&huart1, mb_comm.tx_buffer, mb_comm.tx_len, 0xffff);
    }
    HAL_UARTEx_ReceiveToIdle_IT(&huart1, mb_comm.rx_buffer,
                                MODBUS_RX_BUFFER_SIZE);
  }
  // ��ʱ���?
  if (IsTimeout()) {
    ULOG_INFO("Timeout in STATE_MODBUS_RECV");
    StopTimeout();
    if (CheckAppValid())
      return EVENT_TIMEOUT_APP_VALID;
    else
      return EVENT_TIMEOUT_APP_INVALID;
  }

  return EVENT_NONE; // ���¼�������״̬
}

void ModbusRecv_Exit(void) {
  ULOG_INFO("Exiting STATE_MODBUS_RECV");
  HAL_UART_AbortReceive(&huart1); // ��ֹ��ǰ���գ���������IDLE�жϣ�
  HAL_UART_Receive_IT(&huart1, &rxdata, 1); // �������ֽڽ���
}

/* ---------------- ״̬ STATE_PROG_UPGRADE ---------------- */

/**
 * @brief ��������״̬��������ʱ��ʱ������д��ʱ������
 */
void ProgUpgrade_Entry(void) {
  ULOG_INFO("Entering STATE_PROG_UPGRADE");
  // ���� APP �������� 2~5��
  if (Bootloader_Erase() != BL_OK) {
    // ����ʧ�ܣ��ϱ����󣬿�����ѭ����ȴ����?
    Error_Handler();
  }
  Bootloader_FlashBegin(); // ��ʼ Flash д��
  XMODEM_StartReceive(&xmodem);
  StartTimeout();
}

/**
 * @brief ����״̬�ĺ���ִ�У�ģ����д���Ȼ��ⳬʱ
 * @return �������¼�
 * @note ʵ����Ŀ��Ӧ���? Flash_GetStatus() ��Ӳ��״̬
 */
BootEvent ProgUpgrade_Do(void) {

  XMODEM_Poll(&xmodem);
  // �ж��Ƿ��������?
  if (xmodem.eot_received == true) {
    uint8_t flash_end_ret = Bootloader_FlashEnd(); // ���� Flash д�룬����ʣ�໺��
    if (flash_end_ret != BL_OK) {
      ULOG_ERROR("FlashEnd failed with error code %d\n", flash_end_ret);
      return EVENT_BURN_COMPLETE_APP_INVALID;
    }
    version.app_end_addr = ReturnCurrentAddr();
    version.app_ver_major = APP_VERSION_MAJOR;
    version.app_ver_minor = APP_VERSION_MINOR;
    version.app_ver_patch = APP_VERSION_PATCH;
    if (Version_Write(&version) == 0) {
      ULOG_INFO("Version Info write success\n");
    } else {
      ULOG_INFO("Version Info write fail\n");
    }
    if (CheckAppValid()) // ���? APP ħ���Ƿ���Ч
    {
      ULOG_INFO("APP is valid");
      return EVENT_BURN_COMPLETE_APP_VALID;
    } else {
      ULOG_INFO("APP is invalid");
      return EVENT_BURN_COMPLETE_APP_INVALID;
    }
  }
  return EVENT_NONE;
}

void ProgUpgrade_Exit(void) {
  ULOG_INFO("Exiting STATE_PROG_UPGRADE");
  // �˳�����״̬ʱ������
}

/* ---------------- ״̬ STATE_JUMP_APP ---------------- */

/**
 * @brief ������ת״̬��ִ����ת��ͨ�������أ�
 */
void JumpApp_Entry(void) {
  ULOG_INFO("Entering STATE_JUMP_APP");
  // ʵ��Ӧ���� JumpToApp();  // �ú������᷵��
}

/**
 * @brief ��ת״̬�ĺ���ִ�У���״̬Ϊ��̬��һ���޲�����
 */
BootEvent JumpApp_Do(void) {
  BootJumpAPP();
  if (IsTimeout()) {
    ULOG_INFO("Timeout in STATE_JUMP_APP");
    return EVENT_NONE;
  }
  return EVENT_NONE;
}

void JumpApp_Exit(void) {
  // �޲���
}
