#pragma once
#include <stddef.h>
#include <stdint.h>
/* Only one uncompressed IN question is accepted; malformed packets are dropped. */
size_t hisu_setup_dns_reply(const uint8_t *query, size_t length,
                          const uint8_t ip[4], uint8_t *out, size_t capacity);
