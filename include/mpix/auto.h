/**
 * SPDX-License-Identifier: Apache-2.0
 * @defgroup mpix_auto_ctrls ctrls/ipa.h
 * @brief Basic IPAs (auto-exposure, auto-white-balance...) for demo purpose
 * @{
 */
#ifndef MPIX_AUTO_H
#define MPIX_AUTO_H

#include <stdint.h>
#include <stddef.h>

#include <mpix/stats.h>
#include <mpix/op_correction.h>

struct mpix_auto_ctrls {
	/** Pointer to user-provided data that represents a video device */
	void *dev;
	/** Current sensor exposure value */
	int32_t exposure_level;
	/** Maximum sensor exposure value */
	int32_t exposure_max;
	/** The correction levels */
	struct mpix_correction correction;
};

/**
 * @brief Configure the video device to use for sending controls such as exposure.
 *
 * @param ipa The IPA context to initialize.
 * @param dev The device pointer, passed to functions such as @ref mpix_port_set_exposure().
 */
int mpix_auto_exposure_init(struct mpix_auto_ctrls *ctrls, void *dev);

/**
 * @brief Run auto-exposure algorithm to update the exposure control value.
 *
 * @param ipa The current Image Processing Algorithm (IPA) context.
 * @param stats The statistics used to control the exposure.
 */
void mpix_auto_exposure_control(struct mpix_auto_ctrls *ctrls, struct mpix_stats *stats);

/**
 * @brief Run Black Level Correction (BLC) algorithm to update the black level.
 *
 * @param ipa The current Image Processing Algorithm (IPA) context.
 * @param stats The statistics used to control the black level and then updated.
 */
void mpix_auto_black_level(struct mpix_auto_ctrls *ctrls, struct mpix_stats *stats);

/**
 * @brief Run Auto White Balance algorithm to update the color balance.
 *
 * @param ipa The current Image Processing Algorithm (IPA) context.
 * @param stats The statistics used to control the white balance and then updated.
 */
void mpix_auto_white_balance(struct mpix_auto_ctrls *ctrls, struct mpix_stats *stats);

#endif /** @} */
