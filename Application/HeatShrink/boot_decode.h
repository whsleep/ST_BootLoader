#ifndef BOOT_DECODE_H
#define BOOT_DECODE_H

#include <stddef.h>
#include <stdint.h>


#define READ_CHUNK 64 // 
#define OUT_CAP 1024 //

/* 解压输出回调：out 为解压出的字节，len 为长度；
 * 返回实际处理的字节数（返回小于 len 表示中止解压）。 */
typedef size_t (*boot_decode_sink_fn)(const uint8_t *out, size_t len);

void boot_decode_init(void);
size_t boot_decode_stream(const uint8_t *comp, size_t comp_len, boot_decode_sink_fn sink);
size_t boot_decode_stream_finish(boot_decode_sink_fn sink);

#endif