-- SPDX-License-Identifier: Apache-2.0
-- Example ISP pipeline script in Lua

-- Add color correction operations
pipeline = {}
mpix.op.correct_black_level(pipeline)
mpix.op.correct_white_balance(pipeline)
mpix.op.kernel_denoise_3x3(pipeline)

-- Get the statistics
stats = mpix.get_stats()

-- Auto black level
sum = 0
for i = 1, #stats.y_histogram do
  sum = sum + stats.y_histogram[i]
  if sum > 10 then black_level = stats.y_histogram_vals[i] break end
end

-- Auto white balance
red_balance_q10 = (stats.rgb_average_g << 10) // stats.rgb_average_r;
blue_balance_q10 = (stats.rgb_average_g << 10) // stats.rgb_average_b;

-- Apply controls over the pipeline
function q10(float) return math.ceil(float * (1 << 10)) end
mpix.set_ctrl(mpix.cid.BLACK_LEVEL, black_level or 0)
mpix.set_ctrl(mpix.cid.RED_BALANCE, red_balance_q10)
mpix.set_ctrl(mpix.cid.BLUE_BALANCE, blue_balance_q10)

-- Test a few pixel format conversions
mpix.op.convert(pipeline, mpix.fmt.RGB565)
mpix.op.convert(pipeline, mpix.fmt.RGB24)
mpix.op.convert(pipeline, mpix.fmt.YUYV)
mpix.op.convert(pipeline, mpix.fmt.RGB24)

-- JPEG encode the result for convenience
mpix.op.jpeg_encode(pipeline)

-- Set the active pipeline, which will be run after the script returns
mpix.set_pipeline(pipeline)
