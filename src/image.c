/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <mpix/image.h>
#include <mpix/print.h>

void mpix_image_free(struct mpix_image *img)
{
	for (struct mpix_base_op *next, *op = img->ops.first; op != NULL; op = next) {
		next = op->next;

		if (op->is_heap) {
			mpix_port_free(op->ring.data);
		}
		memset(op, 0x00, sizeof(*op));
		mpix_port_free(op);
	}
	memset(&img->ops, 0x00, sizeof(img->ops));
}

int mpix_image_error(struct mpix_image *img, int err)
{
	if (err != 0 && img->err == 0) {
		mpix_image_free(img);
		img->err = err;
	}
	return err;
}

static void mpix_image_append(struct mpix_image *img, struct mpix_base_op *op)
{
	if (img->ops.last != NULL) {
		img->ops.last->next = op;
	}

	if (img->ops.first == NULL) {
		img->ops.first = op;
	}

	img->ops.last = op;
}

int mpix_image_append_op(struct mpix_image *img, const struct mpix_base_op *template,
			 size_t op_sz, size_t buffer_sz, size_t threshold)
{
	struct mpix_base_op *op;

	if (img->err) {
		return -ECANCELED;
	}

	if (template->fourcc_src != img->fourcc) {
		MPIX_ERR("Wrong format for this operation: image has %s, operation uses %s",
			MPIX_FOURCC_TO_STR(template->fourcc_src), MPIX_FOURCC_TO_STR(img->fourcc));
		return mpix_image_error(img, -EINVAL);
	}

	op = mpix_port_alloc(op_sz);
	if (op == NULL) {
		MPIX_ERR("Failed to allocate an operation");
		return mpix_image_error(img, -ENOMEM);
	}

	memcpy(op, template, op_sz);
	op->threshold = threshold;
	op->width = img->width;
	op->height = img->height;
	op->ring.data = NULL; /* allocated later */
	op->ring.size = buffer_sz;

	img->fourcc = op->fourcc_dst;

	mpix_image_append(img, op);

	return 0;
}

int mpix_image_append_uncompressed_op(struct mpix_image *img, const struct mpix_base_op *op,
				      size_t op_sz)
{
	size_t pitch = img->width * mpix_bits_per_pixel(img->fourcc) / BITS_PER_BYTE;
	size_t buf_sz = op->window_size * pitch;

	return mpix_image_append_op(img, op, op_sz, buf_sz, buf_sz);
}

int mpix_image_process(struct mpix_image *img)
{
	struct mpix_base_op *op;

	if (img->err) {
		return -ECANCELED;
	}

	if (img->buffer == NULL) {
		MPIX_ERR("No input buffer configured");
		return mpix_image_error(img, -ENOBUFS);
	}

	op = img->ops.first;
	if (op == NULL) {
		MPIX_ERR("No operation to perform on image");
		return mpix_image_error(img, -ENOSYS);
	}

	mpix_ring_init(&op->ring, img->buffer, img->size);
	mpix_ring_write(&op->ring, img->size);

	if (mpix_ring_tailroom(&op->ring) < op->ring.size) {
		MPIX_ERR("Not enough space (%u) in input buffer to run the first operation (%u)",
			mpix_ring_tailroom(&op->ring), op->ring.size);
		return mpix_image_error(img, -ENOSPC);
	}

	for (op = op->next; op != NULL; op = op->next) {
		if (op->ring.data == NULL) {
			op->ring.data = mpix_port_alloc(op->ring.size);
			if (op->ring.data == NULL) {
				MPIX_ERR("Failed to allocate a ring buffer");
				return mpix_image_error(img, -ENOMEM);
			}
			op->is_heap = true;
		}
	}

	for (struct mpix_base_op *op = img->ops.first; op != NULL; op = op->next) {
		MPIX_DBG("- %s %ux%u to %s, %s, threshold %u",
			MPIX_FOURCC_TO_STR(op->fourcc_src), op->width, op->height,
			MPIX_FOURCC_TO_STR(op->fourcc_dst), op->name, op->threshold);
	}

	mpix_op_run(img->ops.first);
	img->size = mpix_ring_tailroom(&img->ops.last->ring);

#if 0
	mpix_image_print_ops(img);
#endif

	mpix_image_free(img);

	return 0;
}

void mpix_image_from_buf(struct mpix_image *img, const uint8_t *buffer, size_t sz,
			 uint16_t width, uint16_t height, uint32_t fourcc)
{
	memset(img, 0x00, sizeof(*img));
	img->buffer = (uint8_t *)buffer;
	img->size = sz;
	img->width = width;
	img->height = height;
	img->fourcc = fourcc;
}

int mpix_image_to_buf(struct mpix_image *img, uint8_t *buffer, size_t sz)
{
	struct mpix_base_op *op;
	int ret;

	if (img->err) {
		return -ECANCELED;
	}

	op = mpix_port_alloc(sizeof(struct mpix_base_op));
	if (op == NULL) {
		MPIX_ERR("Failed to allocate an operation");
		return mpix_image_error(img, -ENOMEM);
	}

	memset(op, 0x00, sizeof(*op));
	op->name = __func__;
	op->threshold = sz;
	op->fourcc_src = img->fourcc;
	op->fourcc_dst = img->fourcc;
	op->width = img->width;
	op->height = img->height;
	op->ring.data = buffer;
	op->ring.size = sz;

	mpix_image_append(img, op);

	ret = mpix_image_process(img);

	img->buffer = buffer;
	mpix_ring_tailroom(&img->ops.last->ring);

	return ret;
}

#if 0
void mpix_image_hexdump(struct mpix_image *img)
{
	mpix_hexdump(img->buffer, img->size, img->width, img->height, img->fourcc);
}
#endif
