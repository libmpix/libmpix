/**
 * SPDX-License-Identifier: Apache-2.0
 * @defgroup mpix_auto_h mpix/auto.h
 * @brief Basic auto-algorithm (auto-exposure, auto-white-balance...)
 * @{
 */
#ifndef MPIX_AUTO_H
#define MPIX_AUTO_H

#include <stdint.h>
#include <stddef.h>

#include <mpix/types.h>

/**
 * @brief Set the defaults for the auto-tuning parameters that are set to zero
 *
 * @param ctrls The Image Processing Algorithm (IPA) context to initialize.
 */
void mpix_auto_init_defaults(struct mpix_auto_ctrls *ctrls);

/**
 * @brief Run auto-exposure algorithm to update the exposure control value.
 *
 * @param ctrls The Image Processing Algorithm (IPA) context.
 * @param stats The statistics used to control the exposure.
 */
void mpix_auto_exposure_control(struct mpix_auto_ctrls *ctrls, struct mpix_stats *stats);

/**
 * @brief Run Black Level Correction (BLC) algorithm to update the black level.
 *
 * @param ctrls The Image Processing Algorithm (IPA) context.
 * @param stats The statistics used to control the black level and then updated.
 */
void mpix_auto_black_level(struct mpix_auto_ctrls *ctrls, struct mpix_stats *stats);

/**
 * @brief Run Auto White Balance algorithm to update the color balance.
 *
 * @param ctrls The Image Processing Algorithm (IPA) context.
 * @param stats The statistics used to control the white balance and then updated.
 */
void mpix_auto_white_balance(struct mpix_auto_ctrls *ctrls, struct mpix_stats *stats);


#endif /** @} */
