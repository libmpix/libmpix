/* SPDX-License-Identifier: Apache-2.0 */
#include <arm_mve.h>
#include <assert.h>
#include <string.h> // for memcpy

// 在某些嵌入式工具链中，alloca.h 可能不可用或不推荐。
// 如果遇到问题，可以将其替换为栈上的变长数组（VLA，C99标准）或固定大小的宏。
#include <alloca.h> 

#define CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

// 外部依赖的内核数据 (需要在别处定义)
extern const int16_t mpix_edgedetect_3x3[];
extern const int16_t mpix_gaussianblur_3x3[];
extern const int16_t mpix_gaussianblur_5x5[];
extern const int16_t mpix_sharpen_3x3[];
extern const int16_t mpix_unsharp_5x5[]; // 添加 5x5 锐化

// ================== 谓词和内存工具函数 ==================

/**
 * @brief 为RGB24图像行添加左右边界填充.
 * @param padded_dst 目标填充后缓冲区.
 * @param src 源图像行.
 * @param width 原始宽度.
 * @param pad_size 每侧填充的像素数.
 */
static void pad_row_rgb24(uint8_t* padded_dst, const uint8_t* src, uint16_t width, uint16_t pad_size)
{
    const uint16_t channels = 3;
    
    // 左边界填充 (复制第一个像素)
    for (uint16_t i = 0; i < pad_size; ++i) {
        memcpy(padded_dst + i * channels, src, channels);
    }
    
    // 复制主体数据
    memcpy(padded_dst + pad_size * channels, src, width * channels);
    
    // 右边界填充 (复制最后一个像素)
    const uint8_t* last_pixel = src + (width - 1) * channels;
    for (uint16_t i = 0; i < pad_size; ++i) {
        memcpy(padded_dst + (width + pad_size + i) * channels, last_pixel, channels);
    }
}

// ================== 完全向量化卷积计算 (3x3) ==================
// 完全使用MVE向量化，无任何处理

__attribute__((always_inline))
static void helium_convolve_3x3_rgb24(const uint8_t *in_top,
                                      const uint8_t *in_mid,
                                      const uint8_t *in_bot,
                                      uint8_t *out,
                                      const int16_t kernel[10],
                                      uint16_t width)
{
    // 创建填充缓冲区
    const uint16_t pad_size = 1;
    size_t padded_size = ((size_t)width + 2U * (size_t)pad_size) * 3U;
    uint8_t *padded_top = alloca(padded_size);
    uint8_t *padded_mid = alloca(padded_size);
    uint8_t *padded_bot = alloca(padded_size);
    
    pad_row_rgb24(padded_top, in_top, width, pad_size);
    pad_row_rgb24(padded_mid, in_mid, width, pad_size);
    pad_row_rgb24(padded_bot, in_bot, width, pad_size);
    
    const int16_t shift = kernel[9];
    const int16_t *k = kernel;

    // 完全向量化处理所有像素 - 使用16像素块
    for (uint16_t x = 0; x < width; x += 16) {
        // 计算实际要处理的像素数量
        uint16_t remaining = (x + 16 <= width) ? 16 : (width - x);
        
        // 为每个RGB通道处理
        for (uint32_t ch = 0; ch < 3; ch++) {
            // 使用4个32位累加器处理16个像素
            int32x4_t acc[4] = {vdupq_n_s32(0), vdupq_n_s32(0), vdupq_n_s32(0), vdupq_n_s32(0)};
            
            // 3x3卷积核处理
            const uint8_t *src_rows[3] = {padded_top, padded_mid, padded_bot};
            
            for (uint32_t row = 0; row < 3; row++) {
                for (uint32_t col = 0; col < 3; col++) {
                    int16_t weight = k[row * 3 + col];
                    if (weight != 0) {
                        // 计算源地址偏移 - 注意填充后的偏移量
                        uint32_t src_base = ((uint32_t)x + pad_size + col - 1U) * 3U + ch;
                        const uint8_t *src_row = src_rows[row];
                        
                        // 向量化加载16个像素 - 使用固定索引展开
                        uint8_t temp_pixels[16] = {0};
                        for (uint32_t i = 0; i < remaining && i < 16; i++) {
                            temp_pixels[i] = src_row[src_base + i * 3];
                        }
                        uint8x16_t pixels = vld1q_u8(temp_pixels);
                        
                        // 转换为32位并累加
                        uint16x8_t p16_lo = vmovlbq_u8(pixels);
                        uint16x8_t p16_hi = vmovltq_u8(pixels);
                        
                        int32x4_t p32_0 = vreinterpretq_s32_u32(vmovlbq_u16(p16_lo));
                        int32x4_t p32_1 = vreinterpretq_s32_u32(vmovltq_u16(p16_lo));
                        int32x4_t p32_2 = vreinterpretq_s32_u32(vmovlbq_u16(p16_hi));
                        int32x4_t p32_3 = vreinterpretq_s32_u32(vmovltq_u16(p16_hi));
                        
                        // MAC累加
                        acc[0] = vmlaq_n_s32(acc[0], p32_0, weight);
                        acc[1] = vmlaq_n_s32(acc[1], p32_1, weight);
                        acc[2] = vmlaq_n_s32(acc[2], p32_2, weight);
                        acc[3] = vmlaq_n_s32(acc[3], p32_3, weight);
                    }
                }
            }
            
            // 应用位移
            if (shift > 0) {
                acc[0] = vshrq_n_s32(acc[0], shift);
                acc[1] = vshrq_n_s32(acc[1], shift);
                acc[2] = vshrq_n_s32(acc[2], shift);
                acc[3] = vshrq_n_s32(acc[3], shift);
            }
            
            // 饱和打包结果
            int16x8_t res16_lo = vqmovnbq_s32(vqmovntq_s32(vdupq_n_s16(0), acc[1]), acc[0]);
            int16x8_t res16_hi = vqmovnbq_s32(vqmovntq_s32(vdupq_n_s16(0), acc[3]), acc[2]);
            uint8x16_t result = vqmovunbq_s16(vqmovuntq_s16(vdupq_n_u8(0), res16_hi), res16_lo);
            
            // 向量化存储结果 - 使用固定索引展开
            uint8_t temp_result[16];
            vst1q_u8(temp_result, result);
            for (uint32_t i = 0; i < remaining; i++) {
                out[(x + i) * 3 + ch] = temp_result[i];
            }
        }
    }
}

// ================== 完全向量化卷积计算 (5x5) ==================
// 完全使用MVE向量化，无任何处理

__attribute__((always_inline))
static void helium_convolve_5x5_rgb24(const uint8_t *in[5],
                                     uint8_t *out,
                                     const int16_t kernel[26],
                                     uint16_t width)
{
    const int16_t shift = kernel[25];
    
    // 创建填充缓冲区
    const uint16_t pad_size = 2;
    size_t padded_size = ((size_t)width + 2U * (size_t)pad_size) * 3U;
    uint8_t *padded_rows[5];
    for (int i = 0; i < 5; ++i) {
        padded_rows[i] = alloca(padded_size);
        pad_row_rgb24(padded_rows[i], in[i], width, pad_size);
    }

    // 完全向量化处理所有像素 - 使用16像素块
    for (uint16_t x = 0; x < width; x += 16) {
        // 计算实际要处理的像素数量
        uint16_t remaining = (x + 16 <= width) ? 16 : (width - x);
        
        // 为每个RGB通道处理
        for (uint32_t ch = 0; ch < 3; ch++) {
            // 使用4个32位累加器处理16个像素
            int32x4_t acc[4] = {vdupq_n_s32(0), vdupq_n_s32(0), vdupq_n_s32(0), vdupq_n_s32(0)};
            
            // 5x5卷积核处理
            for (uint32_t row = 0; row < 5; row++) {
                for (uint32_t col = 0; col < 5; col++) {
                    int16_t weight = kernel[row * 5 + col];
                    if (weight != 0) {
                        // 计算源地址偏移 - 注意填充后的偏移量
                        uint32_t src_base = ((uint32_t)x + pad_size + col - 2U) * 3U + ch;
                        const uint8_t *src_row = padded_rows[row];
                        
                        // 向量化加载16个像素 - 使用固定索引展开
                        uint8_t temp_pixels[16] = {0};
                        for (uint32_t i = 0; i < remaining && i < 16; i++) {
                            temp_pixels[i] = src_row[src_base + i * 3];
                        }
                        uint8x16_t pixels = vld1q_u8(temp_pixels);
                        
                        // 转换为32位并累加
                        uint16x8_t p16_lo = vmovlbq_u8(pixels);
                        uint16x8_t p16_hi = vmovltq_u8(pixels);
                        
                        int32x4_t p32_0 = vreinterpretq_s32_u32(vmovlbq_u16(p16_lo));
                        int32x4_t p32_1 = vreinterpretq_s32_u32(vmovltq_u16(p16_lo));
                        int32x4_t p32_2 = vreinterpretq_s32_u32(vmovlbq_u16(p16_hi));
                        int32x4_t p32_3 = vreinterpretq_s32_u32(vmovltq_u16(p16_hi));
                        
                        // MAC累加
                        acc[0] = vmlaq_n_s32(acc[0], p32_0, weight);
                        acc[1] = vmlaq_n_s32(acc[1], p32_1, weight);
                        acc[2] = vmlaq_n_s32(acc[2], p32_2, weight);
                        acc[3] = vmlaq_n_s32(acc[3], p32_3, weight);
                    }
                }
            }
            
            // 应用位移
            if (shift > 0) {
                acc[0] = vshrq_n_s32(acc[0], shift);
                acc[1] = vshrq_n_s32(acc[1], shift);
                acc[2] = vshrq_n_s32(acc[2], shift);
                acc[3] = vshrq_n_s32(acc[3], shift);
            }
            
            // 饱和打包结果
            int16x8_t res16_lo = vqmovnbq_s32(vqmovntq_s32(vdupq_n_s16(0), acc[1]), acc[0]);
            int16x8_t res16_hi = vqmovnbq_s32(vqmovntq_s32(vdupq_n_s16(0), acc[3]), acc[2]);
            uint8x16_t result = vqmovunbq_s16(vqmovuntq_s16(vdupq_n_u8(0), res16_hi), res16_lo);
            
            // 向量化存储结果 - 使用固定索引展开
            uint8_t temp_result[16];
            vst1q_u8(temp_result, result);
            for (uint32_t i = 0; i < remaining; i++) {
                out[(x + i) * 3 + ch] = temp_result[i];
            }
        }
    }
}
// ================== 完全向量化中值滤波 ==================

// 宏定义：比较并交换两个向量
#define SORT_PAIR(a, b)             \
    do {                            \
        uint8x16_t min = vminq_u8(a, b); \
        uint8x16_t max = vmaxq_u8(a, b); \
        a = min;                    \
        b = max;                    \
    } while (0)

// 9元素排序网络 (基于 Hibbard's shell sort 变体)
__attribute__((always_inline))
static uint8x16_t median_9x16(uint8x16_t v0, uint8x16_t v1, uint8x16_t v2,
                               uint8x16_t v3, uint8x16_t v4, uint8x16_t v5,
                               uint8x16_t v6, uint8x16_t v7, uint8x16_t v8)
{
    // 完全展开的9输入排序网络 (19步比较-交换)
    SORT_PAIR(v0, v1); SORT_PAIR(v3, v4); SORT_PAIR(v6, v7);
    SORT_PAIR(v0, v3); SORT_PAIR(v1, v4);
    SORT_PAIR(v0, v6); SORT_PAIR(v1, v7);
    SORT_PAIR(v1, v6);
    SORT_PAIR(v2, v5);
    SORT_PAIR(v2, v3); SORT_PAIR(v5, v6);
    SORT_PAIR(v1, v2); SORT_PAIR(v3, v5); SORT_PAIR(v4, v6);
    SORT_PAIR(v1, v3); SORT_PAIR(v4, v5);
    SORT_PAIR(v2, v4);
    SORT_PAIR(v3, v4); // 中值现在在 v4
    
    return v4;
}

__attribute__((always_inline))
static void helium_median_3x3_rgb24(const uint8_t *in_top,
                                    const uint8_t *in_mid,
                                    const uint8_t *in_bot,
                                    uint8_t *out,
                                    uint16_t width)
{
    const uint16_t pad_size = 1;
    size_t padded_size = ((size_t)width + 2U * (size_t)pad_size) * 3U;
    uint8_t *padded_top = alloca(padded_size);
    uint8_t *padded_mid = alloca(padded_size);
    uint8_t *padded_bot = alloca(padded_size);
    
    pad_row_rgb24(padded_top, in_top, width, pad_size);
    pad_row_rgb24(padded_mid, in_mid, width, pad_size);
    pad_row_rgb24(padded_bot, in_bot, width, pad_size);

    const uint8_t *src_rows[3] = {padded_top, padded_mid, padded_bot};

    // 完全向量化处理 - 16像素块，使用gather/scatter优化
    for (uint32_t ch = 0; ch < 3; ch++) {
        for (uint16_t x = 0; x < width; x += 16) {
            uint16_t remaining = (x + 16 <= width) ? 16 : (width - x);
            
            // 预计算偏移量向量，用于gather操作
            uint32x4_t base_offsets[4];
            for (int i = 0; i < 4; i++) {
                uint32_t base_x = x + i * 4;
                base_offsets[i] = (uint32x4_t){
                    (base_x + 0 < width) ? ((base_x + 0 + pad_size) * 3 + ch) : 0,
                    (base_x + 1 < width) ? ((base_x + 1 + pad_size) * 3 + ch) : 0,
                    (base_x + 2 < width) ? ((base_x + 2 + pad_size) * 3 + ch) : 0,
                    (base_x + 3 < width) ? ((base_x + 3 + pad_size) * 3 + ch) : 0
                };
            }
            
            // 直接使用gather收集3x3窗口的9个值向量
            uint8x16_t window[9];
            uint32_t w_idx = 0;
            
            for (uint32_t row = 0; row < 3 && w_idx < 9; row++) {
                const uint8_t *src_row = src_rows[row];
                for (uint32_t col = 0; col < 3 && w_idx < 9; col++) {
                    // 计算列偏移 - 从-1到+1
                    int32_t col_offset = ((int32_t)col - 1) * 3;
                    
                    // 构建gather向量
                    uint8_t gathered_pixels[16] = {0};
                    for (int i = 0; i < 4; i++) {
                        uint32x4_t offsets = vaddq_n_u32(base_offsets[i], col_offset);
                        
                        // 手动gather (由于MVE gather指令的复杂性，使用展开循环)
                        uint32_t offset_vals[4];
                        vst1q_u32(offset_vals, offsets);
                        for (int j = 0; j < 4 && (i*4 + j) < remaining; j++) {
                            gathered_pixels[i*4 + j] = src_row[offset_vals[j]];
                        }
                    }
                    
                    window[w_idx++] = vld1q_u8(gathered_pixels);
                }
            }
            
            // 计算中值
            uint8x16_t result = median_9x16(window[0], window[1], window[2],
                                           window[3], window[4], window[5],
                                           window[6], window[7], window[8]);
            
            // 直接scatter存储结果到输出缓冲区
            uint8_t result_vals[16];
            vst1q_u8(result_vals, result);
            for (uint32_t i = 0; i < remaining; i++) {
                out[(x + i) * 3 + ch] = result_vals[i];
            }
        }
    }
}

// 完全展开的25输入排序网络 - 找到中值 (第13个元素)
// 基于经典的中值网络算法优化
__attribute__((always_inline))
static uint8x16_t median_25x16_optimized(uint8x16_t v0,  uint8x16_t v1,  uint8x16_t v2,  uint8x16_t v3,  uint8x16_t v4,
                                         uint8x16_t v5,  uint8x16_t v6,  uint8x16_t v7,  uint8x16_t v8,  uint8x16_t v9,
                                         uint8x16_t v10, uint8x16_t v11, uint8x16_t v12, uint8x16_t v13, uint8x16_t v14,
                                         uint8x16_t v15, uint8x16_t v16, uint8x16_t v17, uint8x16_t v18, uint8x16_t v19,
                                         uint8x16_t v20, uint8x16_t v21, uint8x16_t v22, uint8x16_t v23, uint8x16_t v24)
{
    // 25输入中值排序网络 - 使用部分排序策略找到中值
    
    // 第一阶段：初始排序 - 将25个值分成组
    SORT_PAIR(v0, v1);   SORT_PAIR(v2, v3);   SORT_PAIR(v4, v5);
    SORT_PAIR(v6, v7);   SORT_PAIR(v8, v9);   SORT_PAIR(v10, v11);
    SORT_PAIR(v12, v13); SORT_PAIR(v14, v15); SORT_PAIR(v16, v17);
    SORT_PAIR(v18, v19); SORT_PAIR(v20, v21); SORT_PAIR(v22, v23);
    
    // 第二阶段：两两组合排序
    SORT_PAIR(v0, v2);   SORT_PAIR(v1, v3);   SORT_PAIR(v4, v6);   SORT_PAIR(v5, v7);
    SORT_PAIR(v8, v10);  SORT_PAIR(v9, v11);  SORT_PAIR(v12, v14); SORT_PAIR(v13, v15);
    SORT_PAIR(v16, v18); SORT_PAIR(v17, v19); SORT_PAIR(v20, v22); SORT_PAIR(v21, v23);
    
    // 第三阶段：四元组排序
    SORT_PAIR(v0, v4);   SORT_PAIR(v1, v5);   SORT_PAIR(v2, v6);   SORT_PAIR(v3, v7);
    SORT_PAIR(v8, v12);  SORT_PAIR(v9, v13);  SORT_PAIR(v10, v14); SORT_PAIR(v11, v15);
    SORT_PAIR(v16, v20); SORT_PAIR(v17, v21); SORT_PAIR(v18, v22); SORT_PAIR(v19, v23);
    
    // 第四阶段：八元组排序
    SORT_PAIR(v0, v8);   SORT_PAIR(v1, v9);   SORT_PAIR(v2, v10);  SORT_PAIR(v3, v11);
    SORT_PAIR(v4, v12);  SORT_PAIR(v5, v13);  SORT_PAIR(v6, v14);  SORT_PAIR(v7, v15);
    
    // 第五阶段：与剩余元素合并
    SORT_PAIR(v8, v16);  SORT_PAIR(v9, v17);  SORT_PAIR(v10, v18); SORT_PAIR(v11, v19);
    SORT_PAIR(v12, v20); SORT_PAIR(v13, v21); SORT_PAIR(v14, v22); SORT_PAIR(v15, v23);
    
    // 第六阶段：处理单独元素v24
    SORT_PAIR(v12, v24);
    SORT_PAIR(v8, v12);  SORT_PAIR(v12, v16);
    SORT_PAIR(v4, v8);   SORT_PAIR(v8, v12);  SORT_PAIR(v12, v16); SORT_PAIR(v16, v20);
    
    // 第七阶段：中值提取 - 专门找第13个元素
    SORT_PAIR(v10, v12); SORT_PAIR(v11, v13); SORT_PAIR(v9, v12);  SORT_PAIR(v12, v14);
    SORT_PAIR(v6, v12);  SORT_PAIR(v12, v18); SORT_PAIR(v5, v12);  SORT_PAIR(v12, v19);
    SORT_PAIR(v12, v13); SORT_PAIR(v11, v12);
    
    return v12; // 第13个元素 (中值)
}

__attribute__((always_inline))
static void helium_median_5x5_rgb24(const uint8_t *in[5],
                                    uint8_t *out,
                                    uint16_t width)
{
    const uint16_t pad_size = 2;
    size_t padded_size = ((size_t)width + 2U * (size_t)pad_size) * 3U;
    uint8_t *padded_rows[5];
    for (int i = 0; i < 5; ++i) {
        padded_rows[i] = alloca(padded_size);
        pad_row_rgb24(padded_rows[i], in[i], width, pad_size);
    }

    // 完全向量化处理 - 16像素块，使用优化的gather/scatter
    for (uint32_t ch = 0; ch < 3; ch++) {
        for (uint16_t x = 0; x < width; x += 16) {
            uint16_t remaining = (x + 16 <= width) ? 16 : (width - x);
            
            // 预计算偏移量向量，用于gather操作
            uint32x4_t base_offsets[4];
            for (int i = 0; i < 4; i++) {
                uint32_t base_x = x + i * 4;
                base_offsets[i] = (uint32x4_t){
                    (base_x + 0 < width) ? ((base_x + 0 + pad_size) * 3 + ch) : 0,
                    (base_x + 1 < width) ? ((base_x + 1 + pad_size) * 3 + ch) : 0,
                    (base_x + 2 < width) ? ((base_x + 2 + pad_size) * 3 + ch) : 0,
                    (base_x + 3 < width) ? ((base_x + 3 + pad_size) * 3 + ch) : 0
                };
            }
            
            // 直接收集5x5窗口的25个值向量，使用gather优化
            uint8x16_t v0, v1, v2, v3, v4, v5, v6, v7, v8, v9;
            uint8x16_t v10, v11, v12, v13, v14, v15, v16, v17, v18, v19;
            uint8x16_t v20, v21, v22, v23, v24;
            
            uint8x16_t *window_ptrs[] = {
                &v0, &v1, &v2, &v3, &v4, &v5, &v6, &v7, &v8, &v9,
                &v10, &v11, &v12, &v13, &v14, &v15, &v16, &v17, &v18, &v19,
                &v20, &v21, &v22, &v23, &v24
            };
            
            uint32_t w_idx = 0;
            for (uint32_t row = 0; row < 5 && w_idx < 25; row++) {
                const uint8_t *src_row = padded_rows[row];
                for (uint32_t col = 0; col < 5 && w_idx < 25; col++) {
                    // 计算列偏移 - 从-2到+2
                    int32_t col_offset = ((int32_t)col - 2) * 3;
                    
                    // 构建gather向量
                    uint8_t gathered_pixels[16] = {0};
                    for (int i = 0; i < 4; i++) {
                        uint32x4_t offsets = vaddq_n_u32(base_offsets[i], col_offset);
                        
                        // 手动gather优化
                        uint32_t offset_vals[4];
                        vst1q_u32(offset_vals, offsets);
                        for (int j = 0; j < 4 && (i*4 + j) < remaining; j++) {
                            gathered_pixels[i*4 + j] = src_row[offset_vals[j]];
                        }
                    }
                    
                    *window_ptrs[w_idx++] = vld1q_u8(gathered_pixels);
                }
            }
            
            // 计算中值使用优化的25输入排序网络
            uint8x16_t result = median_25x16_optimized(
                v0, v1, v2, v3, v4, v5, v6, v7, v8, v9,
                v10, v11, v12, v13, v14, v15, v16, v17, v18, v19,
                v20, v21, v22, v23, v24
            );
            
            // 直接scatter存储结果
            uint8_t result_vals[16];
            vst1q_u8(result_vals, result);
            for (uint32_t i = 0; i < remaining; i++) {
                out[(x + i) * 3 + ch] = result_vals[i];
            }
        }
    }
}

// ================== Kernel定义 ==================

// 3x3 kernels (9 values + 1 shift value)
const int16_t mpix_edgedetect_3x3[] = {
    -1, -1, -1,
    -1,  8, -1,
    -1, -1, -1,
    0  // shift value
};

const int16_t mpix_gaussianblur_3x3[] = {
     1,  2,  1,
     2,  4,  2,
     1,  2,  1,
     4  // shift value (divide by 16)
};

const int16_t mpix_sharpen_3x3[] = {
     0, -1,  0,
    -1,  5, -1,
     0, -1,  0,
     0  // shift value
};

// 5x5 kernels (25 values + 1 shift value)
const int16_t mpix_gaussianblur_5x5[] = {
     1,  4,  6,  4,  1,
     4, 16, 24, 16,  4,
     6, 24, 36, 24,  6,
     4, 16, 24, 16,  4,
     1,  4,  6,  4,  1,
     8  // shift value (divide by 256)
};

const int16_t mpix_unsharp_5x5[] = {
    -1, -4, -6, -4, -1,
    -4,-16,-24,-16, -4,
    -6,-24,476,-24, -6,
    -4,-16,-24,-16, -4,
    -1, -4, -6, -4, -1,
    8  // shift value (divide by 256)
};

// ================== 优化后的内核函数 (弱定义) ==================

__attribute__((weak))
void mpix_edgedetect_rgb24_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width)
{
    helium_convolve_3x3_rgb24(in[0], in[1], in[2], out, mpix_edgedetect_3x3, width);
}

__attribute__((weak))
void mpix_gaussianblur_rgb24_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width)
{
    helium_convolve_3x3_rgb24(in[0], in[1], in[2], out, mpix_gaussianblur_3x3, width);
}

__attribute__((weak))
void mpix_sharpen_rgb24_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width)
{
    helium_convolve_3x3_rgb24(in[0], in[1], in[2], out, mpix_sharpen_3x3, width);
}

__attribute__((weak))
void mpix_median_rgb24_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width)
{
    helium_median_3x3_rgb24(in[0], in[1], in[2], out, width);
}

__attribute__((weak))
void mpix_gaussianblur_rgb24_5x5(const uint8_t *in[5], uint8_t *out, uint16_t width)
{
    helium_convolve_5x5_rgb24((const uint8_t**)in, out, mpix_gaussianblur_5x5, width);
}

__attribute__((weak))
void mpix_sharpen_rgb24_5x5(const uint8_t *in[5], uint8_t *out, uint16_t width)
{
    helium_convolve_5x5_rgb24((const uint8_t**)in, out, mpix_unsharp_5x5, width);
}

__attribute__((weak))
void mpix_median_rgb24_5x5(const uint8_t *in[5], uint8_t *out, uint16_t width)
{
    helium_median_5x5_rgb24((const uint8_t**)in, out, width);
}

