/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @defgroup mpix_custom_api_h mpix/custom_api.h
 * @brief APIs that are specific to one particular operation type
 * @{
 */
#ifndef MPIX_OP_CUSTOM_API_H
#define MPIX_OP_CUSTOM_API_H

#include <mpix/types.h>

/**
 * @brief Set the color palette of a paleltte decode operation
 */
int mpix_palette_decode_set_palette(struct mpix_base_op *base, const struct mpix_palette *pal);

/**
 * @brief Set the color palette of a paleltte encode operation
 */
int mpix_palette_encode_set_palette(struct mpix_base_op *base, const struct mpix_palette *pal);

#endif /** @} */
