#include "boot_decode.h"
#include "heatshrink_decoder.h"

// 初始化解码器
static heatshrink_decoder hsd;

// 流式解压的临时输出缓冲（sink 回调前的中转）
static uint8_t stream_out[OUT_CAP];

/*
 *@brief 初始化解码器
 */
void boot_decode_init(void) { heatshrink_decoder_reset(&hsd); }

/*
 *@brief 流式解压：将压缩数据送入解码器，解压出的字节通过 sink 回调输出
 *@param comp 输入流，压缩数据
 *@param comp_len 输入流长度
 *@param sink 输出回调，接收解压出的字节
 *@return 成功返回 0，失败（解码错误/回调未消费）返回 (size_t)-1
 *@note 边解压边输出，无需一次性提供大块输出缓冲，避免输出溢出
 */
size_t boot_decode_stream(const uint8_t *comp, size_t comp_len,
                          boot_decode_sink_fn sink) {
  if (sink == NULL)
    return (size_t)-1;

  size_t in_pos = 0;
  while (in_pos < comp_len) {
    size_t sunk = 0;
    if (heatshrink_decoder_sink(&hsd, (uint8_t *)(comp + in_pos),
                                comp_len - in_pos, &sunk) < 0)
      return (size_t)-1;
    in_pos += sunk;

    HSD_poll_res pres;
    do {
      size_t polled = 0;
      pres = heatshrink_decoder_poll(&hsd, stream_out, sizeof(stream_out),
                                     &polled);
      if (pres < 0)
        return (size_t)-1;
      if (polled > 0 && sink(stream_out, polled) != polled)
        return (size_t)-1;
    } while (pres == HSDR_POLL_MORE);
  }

  return 0;
}

/*
 *@brief 结束流式解压：冲刷末尾残留输出
 *@param sink 输出回调，接收解压出的字节
 *@return 成功返回 0，失败返回 (size_t)-1
 */
size_t boot_decode_stream_finish(boot_decode_sink_fn sink) {
  if (sink == NULL)
    return (size_t)-1;

  HSD_finish_res fres;
  do {
    fres = heatshrink_decoder_finish(&hsd);
    if (fres < 0)
      return (size_t)-1;

    HSD_poll_res pres;
    do {
      size_t polled = 0;
      pres = heatshrink_decoder_poll(&hsd, stream_out, sizeof(stream_out),
                                     &polled);
      if (pres < 0)
        return (size_t)-1;
      if (polled > 0 && sink(stream_out, polled) != polled)
        return (size_t)-1;
    } while (pres == HSDR_POLL_MORE);
  } while (fres == HSDR_FINISH_MORE);

  return 0;
}