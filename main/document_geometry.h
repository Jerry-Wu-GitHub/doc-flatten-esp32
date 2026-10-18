// document_geometry.h - 文档透视变换几何
#pragma once

#include <cstdint>

/**
 * @brief 描述从输出矩形到源图四边形的单应矩阵。
 *
 * 矩阵按以下形式使用：
 *   source_x = (a * u + b * v + c) / (g * u + h * v + 1)
 *   source_y = (d * u + e * v + f) / (g * u + h * v + 1)
 * 其中 (u, v) 是输出图片的归一化坐标。
 */
struct DocumentHomography {
    float values[8];  ///< 依次为 a、b、c、d、e、f、g、h。
};

/**
 * @brief 根据归一化四角坐标构建单应矩阵。
 *
 * @param normalized_corners 8 个浮点数，顺序为左上、右上、右下、左下。
 * @param image_width 源图宽度。
 * @param image_height 源图高度。
 * @param homography 输出单应矩阵，不能为空。
 * @return 构建成功返回 true。
 */
bool build_document_homography(const float normalized_corners[8],
                               int image_width, int image_height,
                               DocumentHomography *homography);

/**
 * @brief 按单应矩阵把文档区域透视展平到输出图片。
 *
 * 输出图片尺寸由 width 和 height 指定。对于输出图片的每个像素，
 * 反算源图坐标并做双线性插值；越界坐标按边缘复制处理。
 *
 * @param source_pixels RGB888 源像素。
 * @param width 源图/输出图宽度。
 * @param height 源图/输出图高度。
 * @param homography 单应矩阵。
 * @param output_pixels RGB888 输出像素。
 */
void warp_document_to_rgb_image(const uint8_t *source_pixels, int width,
                                int height,
                                const DocumentHomography &homography,
                                uint8_t *output_pixels);
