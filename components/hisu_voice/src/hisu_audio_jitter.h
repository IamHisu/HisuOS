#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define HISU_JITTER_PREBUFFER_MS 180U
#define HISU_JITTER_BYTES_PER_MS 48U

typedef struct {
    uint8_t *storage;
    size_t capacity, head, tail, used, peak;
    uint64_t rx_bytes, discarded_bytes;
    uint32_t underruns, overflows, rx_gaps;
    int64_t last_rx_us, max_rx_gap_us;
    bool received;
} hisu_audio_jitter_t;

static inline bool hisu_jitter_init(hisu_audio_jitter_t *buffer, void *storage, size_t capacity)
{
    *buffer = (hisu_audio_jitter_t){0};
    if (!storage || capacity < HISU_JITTER_PREBUFFER_MS * HISU_JITTER_BYTES_PER_MS || capacity % 2) return false;
    buffer->storage = storage; buffer->capacity = capacity;
    return true;
}

static inline bool hisu_jitter_write(hisu_audio_jitter_t *buffer, const void *pcm, size_t bytes)
{
    if (bytes % 2 || bytes > buffer->capacity - buffer->used) { ++buffer->overflows; return false; }
    size_t first = buffer->capacity - buffer->head;
    if (first > bytes) first = bytes;
    if (first) memcpy(buffer->storage + buffer->head, pcm, first);
    if (bytes > first) memcpy(buffer->storage, (const uint8_t *)pcm + first, bytes - first);
    buffer->head = (buffer->head + bytes) % buffer->capacity;
    buffer->used += bytes;
    if (buffer->used > buffer->peak) buffer->peak = buffer->used;
    return true;
}

static inline size_t hisu_jitter_read(hisu_audio_jitter_t *buffer, void *pcm, size_t bytes)
{
    if (bytes > buffer->used) bytes = buffer->used;
    bytes &= ~(size_t)1;
    size_t first = buffer->capacity - buffer->tail;
    if (first > bytes) first = bytes;
    if (pcm && first) memcpy(pcm, buffer->storage + buffer->tail, first);
    if (pcm && bytes > first) memcpy((uint8_t *)pcm + first, buffer->storage, bytes - first);
    buffer->tail = (buffer->tail + bytes) % buffer->capacity;
    buffer->used -= bytes;
    if (!pcm) buffer->discarded_bytes += bytes;
    return bytes;
}

static inline void hisu_jitter_unwrite(hisu_audio_jitter_t *buffer, size_t bytes)
{
    buffer->head = (buffer->head + buffer->capacity - bytes) % buffer->capacity;
    buffer->used -= bytes;
}

static inline void hisu_jitter_rx(hisu_audio_jitter_t *buffer, size_t bytes, int64_t now_us)
{
    if (!bytes) return;
    if (buffer->received) {
        int64_t gap = now_us - buffer->last_rx_us;
        if (gap > buffer->max_rx_gap_us) buffer->max_rx_gap_us = gap;
        if (gap > HISU_JITTER_PREBUFFER_MS * 1000) ++buffer->rx_gaps;
    }
    buffer->received = true; buffer->last_rx_us = now_us; buffer->rx_bytes += bytes;
}

static inline bool hisu_jitter_ready(size_t bytes, bool complete)
{
    return complete || bytes >= HISU_JITTER_PREBUFFER_MS * HISU_JITTER_BYTES_PER_MS;
}
