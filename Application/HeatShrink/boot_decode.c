#include "boot_decode.h"
#include "heatshrink_decoder.h"

// 初始化解码器
static heatshrink_decoder hsd;

/*
 *@brief 初始化解码器
 */
void boot_decode_init(void) { heatshrink_decoder_reset(&hsd); }

/*
 *@brief 解码输入流，返回输出字节数
 *@param comp 输入流，压缩数据
 *@param comp_len 输入流长度
 *@param out 输出流，解压数据
 *@param out_cap 输出流缓冲区大小
 */
size_t boot_decode_feed(const uint8_t *comp, size_t comp_len, uint8_t *out, size_t out_cap) {
  size_t out_pos = 0; // 输出流当前写入位
  size_t in_pos = 0;  // 输入流当前读取位
  while (in_pos < comp_len) {
    size_t sunk = 0; // sunk 是 sink 函数返回的已读取字节
    if (heatshrink_decoder_sink(&hsd, (uint8_t *)(comp + in_pos),
                                comp_len - in_pos, &sunk) < 0)
      return (size_t)-1;
    in_pos += sunk;

    HSD_poll_res pres;
    do {
      if (out_pos >= out_cap)
        return (size_t)-1; /* 输出缓冲不够 */
      size_t polled = 0;
      pres = heatshrink_decoder_poll(&hsd, out + out_pos, out_cap - out_pos,
                                     &polled);
      if (pres < 0)
        return (size_t)-1;
      out_pos += polled;
    } while (pres == HSDR_POLL_MORE);
  }

  return out_pos;
}

/*
 * 结束输入，冲刷末尾残留的输出，返回解压字节数�?
 */
size_t boot_decode_finish(uint8_t *out, size_t out_cap) {
  size_t out_pos = 0;

  HSD_finish_res fres;
  do {
    fres = heatshrink_decoder_finish(&hsd);
    if (fres < 0)
      return (size_t)-1;

    HSD_poll_res pres;
    do {
      if (out_pos >= out_cap)
        return (size_t)-1; /* 输出缓冲不够 */
      size_t polled = 0;
      pres = heatshrink_decoder_poll(&hsd, out + out_pos, out_cap - out_pos,
                                     &polled);
      if (pres < 0)
        return (size_t)-1;
      out_pos += polled;
    } while (pres == HSDR_POLL_MORE);
  } while (fres == HSDR_FINISH_MORE);

  return out_pos;
}