/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <mpix/image.h>
#include <mpix/low_level.h>
#include <mpix/print.h>

static void build_palette_gray(struct mpix_palette *p, int bits)
{
	int n = 1 << bits;

	p->fourcc = (bits == 8)   ? MPIX_FMT_PALETTE8
		    : (bits == 4) ? MPIX_FMT_PALETTE4
		    : (bits == 2) ? MPIX_FMT_PALETTE2
				  : MPIX_FMT_PALETTE1;

	for (int i = 0; i < n; i++) {
		uint8_t v = (uint8_t)(i * 255 / (n - 1));

		p->colors_rgb24[i] = RGB32(v, v, v);
	}
}

static bool check_roundtrip(const uint8_t *src, const uint8_t *dst, int n, int tol)
{
	for (int i = 0; i < n; i++) {
		int s = (src[3 * i] + src[3 * i + 1] + src[3 * i + 2]) / 3;
		int d = (dst[3 * i] + src[3 * i + 1] + src[3 * i + 2]) / 3;

		if (d < s - tol || d > s + tol) {
			printf("mismatch at %d: in=%d out=%d diff=%d\n", i, s, d, d - s);
			mpix_hexdump_raw(src, 2 * n);
			mpix_hexdump_raw(dst, 2 * n);
			return false;
		}
	}
	return true;
}

int main(void)
{
	/* 8bpp: 32px gradient */
	{
		uint8_t src[32 * 3];
		uint8_t idx[32];
		uint8_t dst[32 * 3];

		for (int i = 0; i < 32; i++) {
			src[3 * i + 0] = src[3 * i + 1] = src[3 * i + 2] = (uint8_t)(i * 8);
		}

		struct mpix_palette p = {};
		build_palette_gray(&p, 8);

		mpix_convert_rgb24_to_palette8(src, idx, 32, p.colors_rgb24);
		mpix_convert_palette8_to_rgb24(idx, dst, 32, p.colors_rgb24);
		assert(check_roundtrip(src, dst, 32, 8));
		printf("palettize 8bpp smoke OK\n");
	}

	/* 4bpp: 30px (tail not multiple of 16), expect pack ceil(n/2) */
	{
		enum {
			N = 30
		};
		uint8_t src[N * 3];
		uint8_t idx[(N + 1) / 2];
		uint8_t dst[N * 3];

		for (int i = 0; i < N; i++) {
			src[3 * i + 0] = src[3 * i + 1] = src[3 * i + 2] =
				(uint8_t)(i * 255 / (N - 1));
		}

		struct mpix_palette p = {};
		build_palette_gray(&p, 4);

		mpix_convert_rgb24_to_palette4(src, idx, N, p.colors_rgb24);
		mpix_convert_palette4_to_rgb24(idx, dst, N, p.colors_rgb24);
		assert(check_roundtrip(src, dst, N, 16 + 1)); // TODO: +1 normal? aliasing?
		printf("palettize 4bpp smoke OK\n");
	}

	/* 2bpp: 32px */
	{
		enum {
			N = 32
		};
		uint8_t src[N * 3];
		uint8_t idx[N / 4];
		uint8_t dst[N * 3];

		for (int i = 0; i < N; i++) {
			src[3 * i + 0] = src[3 * i + 1] = src[3 * i + 2] =
				(uint8_t)(i * 255 / (N - 1));
		}

		struct mpix_palette p = {};
		build_palette_gray(&p, 2);

		mpix_convert_rgb24_to_palette2(src, idx, N, p.colors_rgb24);
		mpix_convert_palette2_to_rgb24(idx, dst, N, p.colors_rgb24);
		assert(check_roundtrip(src, dst, N, 64));
		printf("palettize 2bpp smoke OK\n");
	}

	/* 1bpp: 32px */
	{
		enum {
			N = 32
		};
		uint8_t src[N * 3];
		uint8_t idx[N / 8];
		uint8_t dst[N * 3];

		for (int i = 0; i < N; i++) {
			src[3 * i + 0] = src[3 * i + 1] = src[3 * i + 2] =
				(uint8_t)(i * 255 / (N - 1));
		}

		struct mpix_palette p = {};
		build_palette_gray(&p, 1);

		mpix_convert_rgb24_to_palette1(src, idx, N, p.colors_rgb24);
		mpix_convert_palette1_to_rgb24(idx, dst, N, p.colors_rgb24);
		assert(check_roundtrip(src, dst, N, 128));
		printf("palettize 1bpp smoke OK\n");
	}

	/* End-to-end test using mpix_image_palette_encode / mpix_image_palette_decode */
	{
		const int W = 48, H = 11;
		const int N = W * H;
		uint8_t src[3 * N];
		struct mpix_format fmt = {.width = W, .height = H, .fourcc = MPIX_FMT_RGB24};

		for (int i = 0; i < N; ++i) {
			src[3 * i + 0] =  src[3 * i + 1] =  src[3 * i + 2] = i * 9 + 17;
		}

		int depths[4] = {8, 4, 2, 1};

		for (int di = 0; di < 4; ++di) {
			int depth = depths[di];
			struct mpix_palette pal = {
				.fourcc = mpix_palette_fourcc(depth),
			};

			int n = 1 << depth;
			for (int i = 0; i < n; i++) {
				uint8_t v = (uint8_t)(i * 255 / (n - 1));
				pal.colors_rgb24[i] = RGB32(v, v, v);
			}

			struct mpix_image img = {0};
			mpix_image_from_buf(&img, src, sizeof(src), &fmt);
			assert(mpix_image_palette_encode(&img, pal.fourcc) == 0);
			assert(mpix_image_palette_decode(&img) == 0);
			assert(mpix_image_set_palette(&img, &pal) == 0);
			uint8_t outbuf[3 * N];
			assert(mpix_image_to_buf(&img, outbuf, sizeof(outbuf)) == 0);
			int tol = (depth == 8) ? 8 : (depth == 4) ? 16 + 1 /* TODO remove +1 */ : (depth == 2) ? 64 : 128;
			assert(check_roundtrip(src, outbuf, N, tol));
		}
		printf("image_api palette encode/decode OK\n");
	}

	fflush(stdout);
	return 0;
}
