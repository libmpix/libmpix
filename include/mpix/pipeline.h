/**
 * SPDX-License-Identifier: Apache-2.0
 * @defgroup mpix_pipeline_h mpix/pipeline.h
 * @brief Build and manage pipeline of operations
 * @{
 */
#ifndef MPIX_PIPELINE_H
#define MPIX_PIPELINE_H

#include <mpix/types.h>

/**
 * @brief Run a pipeline of operation until there is no more input to send
 *
 * It runs until all the input buffer is consumed.
 *
 * This is called by @ref mpix_image_process to run the operation chain until the input buffer
 * is empty, as well as in @ref mpix_op_done to run the next operation in the chain.
 *
 * @param op The first operation of the pipeline.
 * @return 0 on success or negative error code.
 */
int mpix_pipeline_run_loop(struct mpix_base_op *op);

/**
 * @brief Run a pipeline of operation on a single input line
 *
 * @param op The first operation of the pipeline.
 * @return 0 on success or negative error code.
 */
int mpix_pipeline_run_once(struct mpix_base_op *op);

/**
 * @brief Get the number of parameters that an operation accepts
 * @param type The operation type.
 * @return The number of parameters or negative error code on invalid operation.
 */
int mpix_params_nb(enum mpix_op_type type);

/**
 * @brief Add an operation to an image
 *
 * @param img The image to which add operations.
 * @param type The operation type to allocate and add.
 * @param params The array of parameters for that operation.
 * @param params_nb The number of parameters in that array.
 * @return 0 on success or negative error code.
 */
int mpix_pipeline_add(struct mpix_image *img, enum mpix_op_type type, const int32_t *params,
		      size_t params_nb);

/**
 * @brief Append an array of integer into a pipeline operation
 *
 * @param img The image to which add operations.
 * @param type The operation type to allocate and add.
 * @param params The array of parameters for that operation.
 * @param params_nb The number of parameters in that array.
 * @return 0 on success or negative error code.
 */
int mpix_pipeline_add_array(struct mpix_image *img, int32_t array[], size_t size);

/**
 * @brief Allocate intermediate memory for all operations of a pipeline
 *
 * It will go through every operation of the pipeline and only allocate buffers not yet allocated.
 * This allows buffers to be manually allocated as first or last element of the pipeline if needed.
 * Only the newly allocated buffers will be freed by @ref mpix_op_pipeline_free().
 *
 * @param first_op The first operation of the pipeline.
 * @return 0 on success or negative error code.
 */
int mpix_pipeline_alloc(struct mpix_base_op *first_op);

/**
 * @brief Free the intermediate memory as well as operations of a pipeline
 *
 * This will free every node of a pipeline as well as their intermediate buffers.
 * This will only free memory allocated by libmpix, keeping all buffers provided by the application
 * unchanged..
 *
 * @param first_op The first operation of the pipeline.
 * @return 0 on success or negative error code
 */
void mpix_pipeline_free(struct mpix_base_op *first_op);

/**
 * @brief Set an image palette for every palette-related elements of the pipeline.
 *
 * It will add the palette only if the operation is allowing to select the palette, and only if
 * the pixel format of the palette matches the operation.
 *
 * @param first_op The first operation of the pipeline.
 * @param palette The color palette to add.
 * @return 0 on success or negative error code
 */
int mpix_pipeline_set_palette(struct mpix_base_op *first_op, struct mpix_palette *palette);

int mpix_pipeline_get_palette_fourcc(struct mpix_base_op *first_op, struct mpix_palette *palette);

/**
 * @brief Process a buffer into a pipeline.
 *
 * The pipeline is first allocated, then the pipeline is continuously fed with data from the buffer.
 *
 * @param first_op The first operation of the pipeline.
 * @param palette The color palette to add.
 * @return 0 on success or negative error code
 */
int mpix_pipeline_process(struct mpix_base_op *op, const uint8_t *buffer, size_t size);

#endif /** @} */
