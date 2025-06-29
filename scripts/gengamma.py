# Copyright (c) 2025 Brilliant Labs Ltd.
# Copyright (c) 2025 tinyVision.ai Inc.
# SPDX-License-Identifier: Apache-2.0
#
# Compute gamma lookup tables with a log2 index, as a way to reduce the size without loosing
# precisoin.The first and last values are implicitly 0 and 255, and not stored on the lookup table,
# but still plotted.

import matplotlib.pyplot as plt
import math

def gamma(x, g):
    return int(256 * math.pow(x / 256, g))

def print_c_array_entry(arr, comment):
    fmt = '\t{},'
    line = fmt.format(', '.join(map(str, arr)))

    tabstop = 6
    print(line + ('\t' * (tabstop - (7 + len(line)) // 8)) + f'/* {comment} */')

def print_c_gamma_table(logn):
    print('static const uint8_t mpix_gamma_y[] = {')

    for g in range(1, 16):
        x = []
        y = []

        # Fill the values with a log2 scale
        i = 1
        while i < 256:
            x.append(i)
            y.append(gamma(i, g / 16))
            i *= logn

        print_c_array_entry(y, f'y for gamma = {g} / 16')

        # Implicit 0 and 255 before and after the chart
        x.insert(0, 0)
        y.insert(0, 0)
        x.append(256)
        y.append(256)

        plt.plot(x, y)

    print('};')

    print('static const uint8_t mpix_gamma_x[] = {')

    print_c_array_entry(x[1:-1], f'x scale')

    print('};')

    plt.show()

print('/* Generated by gengamma.py */')
print_c_gamma_table(4)
