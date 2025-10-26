/* SPDX-License-Identifier: Apache-2.0 */

#include <mpix/custom_api.h>
#include <mpix/genlist.h>
#include <mpix/operation.h>
#include <mpix/pipeline.h>
#include <mpix/print.h>
#include <mpix/ring.h>

int mpix_params_nb(enum mpix_op_type type)
{
	switch (type) {
#define CASE_MPIX_OP_PARAMS_NB(X, x) \
	case MPIX_OP_##X: \
		return mpix_params_nb_##x;
MPIX_FOR_EACH_OP(CASE_MPIX_OP_PARAMS_NB);
	case MPIX_OP_END:
		return 0;
	default:
		return -EINVAL;
	}
}

int mpix_pipeline_add(struct mpix_image *img, enum mpix_op_type type, const int32_t *params,
		      size_t params_nb)
{
	switch (type) {
#define CASE_MPIX_OP_ADD(X, x) \
	case MPIX_OP_##X: \
		MPIX_DBG("Adding %s to the pipeline", #X); \
		return mpix_params_nb_##x != params_nb ? -EBADMSG : mpix_add_##x(img, params);
MPIX_FOR_EACH_OP(CASE_MPIX_OP_ADD)
	case MPIX_OP_END:
		return 0;
	default:
		MPIX_ERR("unknown type %u", type);
		return -ENOTSUP;
	}
}

int mpix_pipeline_add_array(struct mpix_image *img, int32_t array[], size_t size)
{
	enum { TYPE, PARAMS };
	int err;

	for (size_t n = 0; PARAMS <= size; n++) {
		int params_nb;

		params_nb = mpix_params_nb(array[TYPE]);
		if (params_nb < 0) {
			MPIX_ERR("Invalid number operation type %u", array[TYPE]);
			return -EINVAL;
		}

		if ((size_t)params_nb > size) {
			MPIX_ERR("No room left for %u parameters", params_nb);
			return -ENOSPC;
		}

		err = mpix_pipeline_add(img, array[TYPE], &array[PARAMS], params_nb);
		if (err) {
			MPIX_ERR("Failed to add operation #%zu (%s): %s",
				n, mpix_str_op[array[TYPE]], strerror(-err));
			return err;
		}

		MPIX_INF("Added operation %s with %u parameters",
			mpix_str_op[array[TYPE]], params_nb);

		array += PARAMS + params_nb;
		size -= PARAMS + params_nb;
	}

	if (size != 0) {
		MPIX_ERR("Invalid size for operation buffer, %u integers left", size);
		return -EINVAL;
	}

	return 0;
}

int mpix_pipeline_run_once(struct mpix_base_op *op)
{
	int err;

	/* Start the counter of the next operation */
	op->start_time_us = mpix_port_get_uptime_us();

	switch (op->type) {
#define CASE_MPIX_OP_RUN(X, x) \
	case MPIX_OP_##X: \
		/* -EAGAIN is the expected behavior when running out of input */ \
		return (err = mpix_run_##x(op)) == -EAGAIN ? 0 : err;
MPIX_FOR_EACH_OP(CASE_MPIX_OP_RUN)
	case MPIX_OP_END:
		return 0;
	default:
		MPIX_ERR("unknown type %u", op->type);
		return -ENOTSUP;
	}
}

int mpix_pipeline_run_loop(struct mpix_base_op *op)
{
	size_t prev_size;
	size_t curr_size;
	int err;

	/* Run the pipeline until it stops consuming input or until there is an error */
	do {
		prev_size = mpix_ring_used_size(&op->ring);
		err = mpix_pipeline_run_once(op);
		curr_size = mpix_ring_used_size(&op->ring);
	} while (err == 0 && prev_size != curr_size);

	/* EAGAIN is expected on the last iteration when all input is consumed */
	if (err == -EAGAIN) return 0;

	/* not enough data to run the operation: need to load more data later */
	if (err) {
		MPIX_ERR("'%s' at the first [op] of this list:", strerror(-err));
		mpix_print_pipeline(op);
	}

	return err;
}

int mpix_pipeline_alloc(struct mpix_base_op *first)
{
	int err;

	for (struct mpix_base_op *op = first; op != NULL; op = op->next) {
		err = mpix_ring_alloc(&op->ring, first->mem_source);
		if (err) return err;
	}

	return 0;
}

void mpix_pipeline_free(struct mpix_base_op *first)
{
	for (struct mpix_base_op *next, *op = first; op != NULL; op = next) {
		enum mpix_mem_source mem_source = op->mem_source;
		next = op->next;
		mpix_ring_free(&op->ring);
		memset(op, 0x00, sizeof(*op));
		mpix_port_free(op, mem_source);
	}
}

int mpix_pipeline_process(struct mpix_base_op *op, const uint8_t *buffer, size_t size)
{
	int err;

	/* If not set already, connect the buffer to the read-only input of the pipeline */
	op->ring.buffer = (uint8_t *)buffer;
	op->ring.size = size;
	mpix_ring_write(&op->ring, size);

	/* Allocate all the buffers not already alloated */
	err = mpix_pipeline_alloc(op);
	if (err) return err;

	/* Run the first operation in loop until all the lines were fed into the pipeline */
	return mpix_pipeline_run_loop(op);
}

int mpix_pipeline_set_palette(struct mpix_base_op *first_op, struct mpix_palette *palette)
{
	bool found;

	for (struct mpix_base_op *op = first_op; op != NULL; op = op->next) {
		if (op->type ==  MPIX_OP_PALETTE_DECODE &&
		    op->fmt.fourcc == palette->fourcc) {
			mpix_palette_decode_set_palette(op, palette);
			found = true;
		}
		if (op->type == MPIX_OP_PALETTE_ENCODE &&
		    op->next != NULL && op->next->fmt.fourcc == palette->fourcc) {
			mpix_palette_encode_set_palette(op, palette);
			found = true;
		}
	}

	return found ? 0 : -ENOENT;
}

int mpix_pipeline_get_palette_fourcc(struct mpix_base_op *first_op, struct mpix_palette *palette)
{
	palette->fourcc = 0;

	/* Get the first palette format, assuming there is only one for the pipeline */
	for (struct mpix_base_op *op = first_op; op != NULL; op = op->next) {
		if (op->type == MPIX_OP_PALETTE_ENCODE) {
			if (op->next == NULL) {
				break;
			}
			palette->fourcc = op->next->fmt.fourcc;
		}
		if (op->type == MPIX_OP_PALETTE_DECODE) {
			palette->fourcc = op->fmt.fourcc;
		}
	}

	return (palette->fourcc == 0) ? -ENOENT : 0;
}
