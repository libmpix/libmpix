/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <mpix/auto.h>
#include <mpix/stats.h>
#include <mpix/utils.h>
#include <mpix/port.h>
#include <mpix/op_correction.h>

int mpix_auto_exposure_init(struct mpix_auto_ctrls *ctrls, void *dev)
{
	int ret;

	ctrls->dev = dev;
	/* Initialize default AE target if not set */
	if (ctrls->ae_target == 0) {
		ctrls->ae_target = 128; /* Mid-gray default */
	}

	ret = mpix_port_init_exposure(dev, &ctrls->exposure_level, &ctrls->exposure_max);
	if (ret < 0) {
		MPIX_ERR("Failed to initialize exposure control");
		return ret;
	}

	return 0;
}

#ifndef CONFIG_MPIX_AEC_THRESHOLD
/** @brief Threshold value over which trigger an state-exposure cycle */
#define CONFIG_MPIX_AEC_THRESHOLD 5
#endif

#ifndef CONFIG_MPIX_AEC_CHANGE_RATE
/** @brief Percentage of change to apply per state-exposure cycle */
#define CONFIG_MPIX_AEC_CHANGE_RATE 30
#endif

/* Minimum percentage change when adjusting exposure (for stability near target) */
#ifndef CONFIG_MPIX_AEC_MIN_CHANGE_RATE
#define CONFIG_MPIX_AEC_MIN_CHANGE_RATE 2
#endif

void mpix_auto_exposure_control(struct mpix_auto_ctrls *ctrls, struct mpix_stats *stats)
{
	uint8_t mean = mpix_stats_get_y_mean(stats);
	int32_t val = ctrls->exposure_level;
	uint8_t target = ctrls->ae_target ? ctrls->ae_target : 128;
	int error = (int)mean - (int)target;
	int abs_err = error < 0 ? -error : error;

	if (abs_err > CONFIG_MPIX_AEC_THRESHOLD) {
		/* Scale change rate between MIN and MAX based on how far we are from target.
		 * abs_err in [threshold..255] -> rate in [MIN..MAX]. Use 128 as mid reference. */
		int dyn_rate = CONFIG_MPIX_AEC_MIN_CHANGE_RATE +
			(CONFIG_MPIX_AEC_CHANGE_RATE - CONFIG_MPIX_AEC_MIN_CHANGE_RATE) * abs_err / 128;
		if (dyn_rate > CONFIG_MPIX_AEC_CHANGE_RATE) dyn_rate = CONFIG_MPIX_AEC_CHANGE_RATE;
		if (dyn_rate < CONFIG_MPIX_AEC_MIN_CHANGE_RATE) dyn_rate = CONFIG_MPIX_AEC_MIN_CHANGE_RATE;

		if (error > 0) {
			/* Over-exposed: reduce */
			val = val * (100 - dyn_rate) / 100;
			if (val < 1) val = 1;
			MPIX_DBG("AE over exp mean=%u tgt=%u err=%d rate=%d%% new=%d", mean, target, error, dyn_rate, val);
		} else {
			/* Under-exposed: increase */
			val = val * (100 + dyn_rate) / 100;
			MPIX_DBG("AE under exp mean=%u tgt=%u err=%d rate=%d%% new=%d", mean, target, error, dyn_rate, val);
		}
	}

	/* Update the value itself */
	ctrls->exposure_level = CLAMP(val, 1, ctrls->exposure_max);

	MPIX_DBG("New exposure value: %u/%u", ctrls->exposure_level, ctrls->exposure_max);

	if (ctrls->dev != NULL) {
		mpix_port_set_exposure(ctrls->dev, ctrls->exposure_level);
	}
}


#ifndef CONFIG_MPIX_BLC_THRESHOLD
/** @brief Number of pixels to cross before setting the minimum value */
#define CONFIG_MPIX_BLC_THRESHOLD 0
#endif

void mpix_auto_black_level(struct mpix_auto_ctrls *ctrls, struct mpix_stats *stats)
{
	union mpix_correction_any *corr = (void *)&ctrls->correction.black_level;
	uint16_t sum = 0;

	ctrls->correction.black_level.level = 0;

	/* Seek the first bucket that */
	for (size_t i = 0; i < ARRAY_SIZE(stats->y_histogram); i++) {
		sum += stats->y_histogram[i];

		if (sum > CONFIG_MPIX_BLC_THRESHOLD) {
			ctrls->correction.black_level.level = stats->y_histogram_vals[i];
			break;
		}
	}

	/* Update the statistics so that they reflect the change of black level */
	mpix_correction_black_level_raw8(stats->y_histogram_vals, stats->y_histogram_vals,
					 sizeof(stats->y_histogram_vals), 0, corr);
	mpix_correction_black_level_rgb24(stats->rgb_average, stats->rgb_average, 1, 0, corr);

	MPIX_DBG("New black level: %u", corr->black_level.level);
}

/*
 * Simple Gray World strategy for Auto White Balance (AWB).
 *
 * The gain values are shifted from the range [x0, x1] to the range [x1, x2].
 * This means that images are always expected to be more green than blue or red, which is
 * assumed to be the case for all image sensors encountered by this library.
 */
void mpix_auto_white_balance(struct mpix_auto_ctrls *ctrls, struct mpix_stats *stats)
{
	union mpix_correction_any *corr = (void *)&ctrls->correction.white_balance;
	uint16_t r = MAX(1, stats->rgb_average[0]);
	uint16_t g = MAX(1, stats->rgb_average[1]);
	uint16_t b = MAX(1, stats->rgb_average[2]);

	corr->white_balance.red_level = (g << MPIX_CORRECTION_SCALE_BITS) / r;
	corr->white_balance.blue_level = (g << MPIX_CORRECTION_SCALE_BITS) / b;

	/* Update the statistics so that they reflect the change of white balance */
	mpix_correction_white_balance_rgb24(stats->rgb_average, stats->rgb_average, 1, 0, corr);

	MPIX_DBG("New red level: %u", corr->white_balance.red_level);
	MPIX_DBG("New blue level: %u", corr->white_balance.blue_level);
}
