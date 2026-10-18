// image_codec.h - JPEG 与 RGB888 编解码
#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief RGB888 图片数据。
 *
 * 像素按行优先排列，每像素 3 字节，顺序为 R、G、B。
 */
struct RgbImage {
    uint8_t *pixels;      ///< RGB888 像素缓冲区。
    int width;            ///< 图片宽度。
    int height;           ///< 图片高度。
    size_t size_in_bytes; ///< 像素缓冲区字节数。
};

/**
 * @brief 把 JPEG 解码为 RGB888 图片。
 *
 * 输出缓冲区从 PSRAM 分配，并使用 16 字节对齐。成功后必须调用
 * free_rgb_image() 释放。
 *
 * @param jpeg_data JPEG 字节。
 * @param jpeg_length JPEG 字节长度。
 * @param output_image 输出图片，不能为空。
 * @return 解码成功返回 true。
 */
bool decode_jpeg_to_rgb_image(const uint8_t *jpeg_data, size_t jpeg_length,
                              RgbImage *output_image);

/**
 * @brief 把 RGB888 图片编码为 JPEG。
 *
 * 输出缓冲区从 PSRAM 分配，调用方使用 heap_caps_free() 释放。
 *
 * @param rgb_pixels RGB888 像素。
 * @param width 图片宽度。
 * @param height 图片高度。
 * @param quality JPEG 质量，范围 1..100。
 * @param output_jpeg 输出 JPEG 缓冲区。
 * @param output_jpeg_length 输出 JPEG 字节长度。
 * @return 编码成功返回 true。
 */
bool encode_rgb_image_to_jpeg(const uint8_t *rgb_pixels, int width, int height,
                              int quality, uint8_t **output_jpeg,
                              size_t *output_jpeg_length);

/**
 * @brief 释放 RGB888 图片缓冲区。
 *
 * @param image 要释放的图片。
 */
void free_rgb_image(RgbImage *image);
