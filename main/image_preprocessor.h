// image_preprocessor.h - 模型输入预处理
#pragma once

#include <cstdint>

#include "tensorflow/lite/c/common.h"

/**
 * @brief 把 RGB888 图片缩放到模型输入尺寸并量化写入输入张量。
 *
 * 采用双线性插值，把原图拉伸到模型输入尺寸（不是 letterbox）。因此
 * 模型输出的归一化坐标与原图坐标是简单线性关系：
 *   pixel_x = normalized_x * image_width
 *   pixel_y = normalized_y * image_height
 *
 * @param source_pixels RGB888 源像素，行优先，每像素 3 字节。
 * @param source_width 源图片宽度。
 * @param source_height 源图片高度。
 * @param input_tensor 模型输入张量，必须为 NHWC 布局。
 */
void preprocess_rgb_image(const uint8_t *source_pixels, int source_width,
                          int source_height, TfLiteTensor *input_tensor);
