#ifndef BOOT_DECODE_H
#define BOOT_DECODE_H

#include <stddef.h>
#include <stdint.h>


#define READ_CHUNK 64 // 
#define OUT_CAP 1024 //

void boot_decode_init(void);
size_t boot_decode_feed(const uint8_t *comp, size_t comp_len, uint8_t *out, size_t out_cap);
size_t boot_decode_finish(uint8_t *out, size_t out_cap);

#endif