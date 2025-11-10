/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @defgroup mpix_operation_h mpix/operation.h
 * @brief Utilities to implement new operations.
 * @{
 */
#ifndef MPIX_OP_H
#define MPIX_OP_H

#include <assert.h>
#include <stdint.h>

#include <mpix/formats.h>
#include <mpix/port.h>
#include <mpix/ring.h>
#include <mpix/types.h>
#include <mpix/pipeline.h>

/**
 * @brief Register an operation globally in the library.
 *
 * The declaration is collected by generated.py which adds it to the MPIX_FOR_EACH_OP() macro
 * used to generate code.
 *
 * @param name The identifier assigned to the operation
 * @param ... A list of parameter names in their correct order.
 */
#define MPIX_REGISTER_OP(name, ...) MPIX_REGISTER_NB(name, ## __VA_ARGS__, P_NB)
#define MPIX_REGISTER_NB(name, ...) enum { __VA_ARGS__ }; const size_t mpix_params_nb_##name = P_NB

/* Declaration of all the parameter numbers defined in the operation files */
#define MPIX_OP_PARAMS_NB(X, x) extern const size_t mpix_params_nb_##x;
MPIX_FOR_EACH_OP(MPIX_OP_PARAMS_NB)

/* Declaration of all the functions to add an operation to a pipeline */
#define MPIX_OP_ADD(X, x) int mpix_add_##x(struct mpix_image *img, const int32_t *params);
MPIX_FOR_EACH_OP(MPIX_OP_ADD)

/* Declaration of all the functions to run an operation of a pipeline */
#define MPIX_OP_RUN(X, x) int mpix_run_##x(struct mpix_base_op *op);
MPIX_FOR_EACH_OP(MPIX_OP_RUN)

/** Call @ref mpix_op_input_lines() and return the error if any */
#define MPIX_OP_INPUT_LINES(...) { int e = mpix_op_input_lines(__VA_ARGS__); if (e) return e; }

/** Call @ref mpix_op_input_bytes() and return the error if any */
#define MPIX_OP_INPUT_BYTES(...) { int e = mpix_op_input_bytes(__VA_ARGS__); if (e) return e; }

/** Call @ref mpix_op_input_done() and return the error if any */
#define MPIX_OP_INPUT_DONE(...) { int e = mpix_op_input_done(__VA_ARGS__); if (e) return e; }

/** Call @ref mpix_op_input_peek() and return the error if any */
#define MPIX_OP_INPUT_PEEK(...) { int e = mpix_op_input_peek(__VA_ARGS__); if (e) return e; }

/** Call @ref mpix_op_input_flush() and return the error if any */
#define MPIX_OP_INPUT_FLUSH(...) { int e = mpix_op_input_flush(__VA_ARGS__); if (e) return e; }

/** Call @ref mpix_op_output_line() and return the error if any */
#define MPIX_OP_OUTPUT_LINE(...) { int e = mpix_op_output_line(__VA_ARGS__); if (e) return e; }

/** Call @ref mpix_op_output_done() and return the error if any */
#define MPIX_OP_OUTPUT_DONE(...) { int e = mpix_op_output_done(__VA_ARGS__); if (e) return e; }

/** Call @ref mpix_op_output_peek() and return the error if any */
#define MPIX_OP_OUTPUT_PEEK(...) { int e = mpix_op_output_peek(__VA_ARGS__); if (e) return e; }

/** Call @ref mpix_op_output_flush() and return the error if any */
#define MPIX_OP_OUTPUT_FLUSH(...) { int e = mpix_op_output_flush(__VA_ARGS__); if (e) return e; }

static inline int mpix_op_input_lines(struct mpix_base_op *op, const uint8_t **src, size_t num)
{
	/* Reset the peek position to clear any previous call from this function */
	mpix_ring_reset_peek(&op->ring);

	/* Fill every line into */
	for (size_t i = 0; i < num; i++) {
		src[i] = mpix_ring_peek(&op->ring, mpix_format_pitch(&op->fmt));
		if (src[i] == NULL) return -EAGAIN;
	}

	return 0;
}

static inline int mpix_op_input_bytes(struct mpix_base_op *op, const uint8_t **src, size_t bytes)
{
	/* Read out the specified amount of data without clearing it from the buffer */
	*src = mpix_ring_peek(&op->ring, bytes);

	return (*src == NULL) ? -EAGAIN : 0;
}

static inline int mpix_op_input_done(struct mpix_base_op *op, size_t lines)
{
	/* Shift the line counter forward */
	op->line_offset += lines;

	/* Clear the requested number of lines from the input buffer */
	const uint8_t *src = mpix_ring_read(&op->ring, mpix_format_pitch(&op->fmt) * lines);

	return (src == NULL) ? -EIO : 0;
}

static inline int mpix_op_input_peek(struct mpix_base_op *op, const uint8_t **src, size_t *sz)
{
	/* Query the utilized buffer size */
	*sz = mpix_ring_used_size(&op->ring);

	/* Read this amount of data without clearing it out from the input buffer */
	*src = mpix_ring_peek(&op->ring, *sz);

	return (*src == NULL) ? -EIO : 0;
}

static inline int mpix_op_input_flush(struct mpix_base_op *op, size_t bytes)
{
	/* Clear out the specified number of bytes from the input buffer */
	const uint8_t *src = mpix_ring_read(&op->ring, bytes);

	return (src == NULL) ? -EIO : 0;
}

static inline void mpix_op_input_all(struct mpix_base_op *op, const uint8_t **src, size_t *sz)
{
	/* Query the utilized buffer size */
	*sz = mpix_ring_used_size(&op->ring);

	/* Read this amount of data and clear it out from the input buffer */
	*src = mpix_ring_read(&op->ring, *sz);

	/* Reset the ring buffer */
	op->ring.head = op->ring.tail = op->ring.peek = 0;
}

static inline int mpix_op_output_peek(struct mpix_base_op *op, uint8_t **dst, size_t *sz)
{
	if (op->next == NULL) return -ENODEV;

	/* Get the entire available buffer without clearing it */
	*sz = mpix_ring_free_size(&op->next->ring);
	*dst = &op->next->ring.buffer[op->next->ring.head];

	return (*dst == NULL) ? -EIO : 0;
}

static inline int mpix_op_output_done(struct mpix_base_op *op)
{
	if (op->next == NULL) return -ENODEV;

	/* Stop the counter of the current pipeline */
	op->total_time_us += mpix_port_get_uptime_us() - op->start_time_us;

	/* Run the rest of the pipeline */
	int err = mpix_pipeline_run_once(op->next);

	/* Start the counter again when resuming to his element */
	op->start_time_us = mpix_port_get_uptime_us();

	return err;
}

static inline int mpix_op_output_line(struct mpix_base_op *op, uint8_t **dst)
{
	if (op->next == NULL) return -ENODEV;

	/* Get one line from the output buffer */
	*dst = mpix_ring_write(&op->next->ring, mpix_format_pitch(&op->next->fmt));

	return (*dst == NULL) ? -ENOBUFS : 0;
}

static inline int mpix_op_output_flush(struct mpix_base_op *op, size_t bytes)
{
	if (op->next == NULL) return -ENODEV;

	/* Clear out the specified number of bytes from the output buffer */
	uint8_t *dst = mpix_ring_write(&op->next->ring, bytes);

	return (dst == NULL) ? -ENOBUFS : 0;
}

/**
 * @brief Allocate a new operation and add it to an image pipeline
 *
 * @param op_type Type of the new operation.
 * @param op_sz Size of the operation struct, allocated immediately.
 * @param buf_sz Size of the buffer, allocated when calling @ref mpix_pipeline_alloc().
 */
static inline void *mpix_op_append(struct mpix_image *img, enum mpix_op_type op_type, size_t op_sz,
				   size_t buf_sz)
{
	struct mpix_base_op *op;

	op = mpix_port_alloc(op_sz, MPIX_MEM_SOURCE_DEFAULT);
	if (op == NULL) {
		return NULL;
	}
	memset(op, 0x00, op_sz);

	op->type = op_type;
	op->fmt = img->fmt;
	op->ring.size = buf_sz;
	op->ring.mem_source = img->mem_source;

	if (img->last_op != NULL) {
		img->last_op->next = op;
	}
	if (img->first_op == NULL) {
		img->first_op = op;
	}
	img->last_op = op;

	return op;
}

#endif /** @} */
