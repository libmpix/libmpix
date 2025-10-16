/**
 * SPDX-License-Identifier: Apache-2.0
 * @internal
 */
#ifndef MPIX_RING_H
#define MPIX_RING_H

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <mpix/port.h>
#include <mpix/types.h>
#include <mpix/utils.h>

static inline void mpix_ring_free(struct mpix_ring *ring)
{
	// ensure we only free for memory managed by mpix_port_*
	if (ring->mem_source != MPIX_MEM_SOURCE_USER){
		mpix_port_free(ring->buffer, ring->mem_source);
		ring->buffer = NULL;
	}
}

static inline int mpix_ring_alloc(struct mpix_ring *ring, enum mpix_mem_source mem_source)
{
	/* If no buffer is provided, allocate one */
	if (ring->buffer == NULL) {
		ring->buffer = mpix_port_alloc(ring->size, mem_source);
		if (ring->buffer == NULL) {
			return -ENOMEM;
		}

		ring->mem_source = mem_source;
	}

	return 0;
}

static inline bool mpix_ring_is_full(struct mpix_ring *ring)
{
	return ring->full;
}

static inline bool mpix_ring_is_empty(struct mpix_ring *ring)
{
	return ring->head == ring->tail && !ring->full;
}

static inline size_t mpix_ring_free_size(struct mpix_ring *ring)
{
	if (ring->head < ring->tail) {
		/* [::::H      T::::]
		 *      ^^^^^^^
		 */
		return ring->tail - ring->head;
	}
	if (ring->tail < ring->head) {
		/* [    T::::::H    ]
		 *             ^^^^^
		 */
		return ring->size - ring->head;
	}
	if (mpix_ring_is_empty(ring)) {
		/* [          TH    ]
		 *             ^^^^^
		 */
		return ring->size - ring->head;
	} else {
		/* [::::::::::HT::::]
		 */
		return 0;
	}
}

static inline size_t mpix_ring_used_size(struct mpix_ring *ring)
{
	if (ring->head < ring->tail) {
		/* [::::H      T::::]
		 *             ^^^^^
		 */
		return ring->size - ring->tail;
	}
	if (ring->tail < ring->head) {
		/* [    T::::::H    ]
		 *      ^^^^^^^
		 */
		return ring->head - ring->tail;
	}
	if (mpix_ring_is_full(ring)) {
		/* [::::::::::HT::::]
		 *             ^^^^^
		 */
		assert(ring->head == ring->tail);
		return ring->size - ring->tail;
	} else {
		/* [          TH    ]
		 */
		assert(ring->head == ring->tail);
		return 0;
	}
}

static inline size_t mpix_ring_peek_size(struct mpix_ring *ring)
{
	if (ring->head < ring->tail && ring->tail <= ring->peek) {
		/* [::::H      T:P::]
		 *               ^^^
		 */
		return ring->size - ring->peek;
	}
	if (ring->peek < ring->head && ring->head < ring->tail) {
		/* [:P::H      T::::]
		 *   ^^^
		 */
		return ring->head - ring->peek;
	}
	if (ring->tail <= ring->peek && ring->peek < ring->head) {
		/* [    T:P::::H    ]
		 *        ^^^^^
		 */
		return ring->head - ring->peek;
	}
	if (mpix_ring_is_full(ring) && ring->tail <= ring->peek) {
		/* [::::::::::HT:P::]
		 *               ^^^
		 */
		assert(ring->head == ring->tail);
		return ring->size - ring->peek;
	}
	if (mpix_ring_is_full(ring) && ring->peek <= ring->head) {
		/* [::P:::::::HT::::]
		 *    ^^^^^^^^
		 */
		assert(ring->head == ring->tail);
		return ring->head - ring->peek;
	}
	return 0;
}

static inline size_t mpix_ring_total_used(struct mpix_ring *ring)
{
	if (ring->head < ring->tail) {
		/* [::::H      T::::]
		 *  ^^^^^      ^^^^^
		 */
		return ring->head + ring->size - ring->tail;
	}
	if (ring->tail < ring->head) {
		/* [    T::::::H    ]
		 *      ^^^^^^^^
		 */
		return ring->head - ring->tail;
	}
	if (mpix_ring_is_full(ring)) {
		/* [::::::::::HT::::]
		 *  ^^^^^^^^^^^^^^^^
		 */
		assert(ring->head == ring->tail);
		return ring->size;
	} else {
		/* [          TH    ]
		 */
		assert(ring->head == ring->tail);
		return 0;
	}
}

static inline size_t mpix_ring_total_free(struct mpix_ring *ring)
{
	return ring->size - mpix_ring_total_used(ring);
}

static inline void mpix_ring_reset_peek(struct mpix_ring *ring)
{
	ring->peek = ring->tail;
}

static inline uint8_t *mpix_ring_write(struct mpix_ring *ring, size_t size)
{
	uint8_t *buffer = ring->buffer + ring->head;

	if (mpix_ring_free_size(ring) < size) {
		MPIX_DBG("Not enough room (%zu) for %zu bytes", mpix_ring_free_size(ring), size);
		return NULL;
	}
	ring->head = (ring->head + size) % ring->size;
	ring->full = (ring->head == ring->tail);
	mpix_ring_reset_peek(ring);
	return buffer;
}

static inline uint8_t *mpix_ring_read(struct mpix_ring *ring, size_t size)
{
	uint8_t *buffer = ring->buffer + ring->tail;

	if (mpix_ring_used_size(ring) < size) {
		return NULL;
	}
	ring->tail = (ring->tail + size) % ring->size;
	ring->full = false;
	mpix_ring_reset_peek(ring);

	/* To avoid fragmentation, re-align the buffer if empty */
	if (mpix_ring_used_size(ring) == 0) {
		ring->tail = ring->head = ring->peek = 0;
	}

	return buffer;
}

static inline uint8_t *mpix_ring_peek(struct mpix_ring *ring, size_t size)
{
	uint8_t *buffer = ring->buffer + ring->peek;

	if (mpix_ring_peek_size(ring) < size) {
		return NULL;
	}
	ring->peek = (ring->peek + size) % ring->size;
	return buffer;
}

#endif
