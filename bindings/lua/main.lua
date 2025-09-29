-- SPDX-License-Identifier: Apache-2.0

-- test that normal Lua works
print("hello world!")

-- generate a map of the mpix module
for k, v in pairs(mpix) do
  print(type(v) .. ":" .. k)
  if type(v) == "table" then
    for kk, vv in pairs(v) do print("- " .. kk) end
  end
end

-- Add color correction operations
mpix.op.correct_black_level()
mpix.op.correct_white_balance()

-- Apply controls over the pipeline
mpix.ctrl(mpix.cid.BLACK_LEVEL, 1)
mpix.ctrl(mpix.cid.RED_BALANCE, 1 << 10)
mpix.ctrl(mpix.cid.BLUE_BALANCE, 1 << 10)

-- Compute a color from the current image
mpix.optimize_palette(200)

-- Apply the palette
mpix.op.palette_encode(mpix.fmt.PALETTE8)
mpix.op.palette_decode()

-- Test a few pixel format conversions
mpix.op.convert(mpix.fmt.RGB565)
mpix.op.convert(mpix.fmt.RGB24)
mpix.op.convert(mpix.fmt.YUYV)
mpix.op.convert(mpix.fmt.RGB24)

-- Finally add an operation for the function callback
mpix.op.callback(102, 1)

-- Read the data from the pipeline chunk by chunk
mpix.run(function (buf) print("[buffer] " .. buf) end)

-- Show the pipeline topology
mpix.dump()

-- Reset the image struct, in case the caller is not taking care of that
mpix.free()
