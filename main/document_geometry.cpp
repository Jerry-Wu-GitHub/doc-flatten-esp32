// document_geometry.cpp - 单应矩阵与双线性透视展平
#include <cmath>
#include <cstddef>

#include "document_geometry.h"

namespace {

/**
 * @brief 把浮点坐标限制在 [0, maximum] 范围内。
 *
 * @param value 输入坐标。
 * @param maximum 允许的最大值。
 * @return 限制后的坐标。
 */
float clamp_coordinate(float value, float maximum) {
    if (value < 0.0f) return 0.0f;
    if (value > maximum) return maximum;
    return value;
}

/**
 * @brief 从源图采样一个双线性插值像素。
 *
 * @param source_pixels RGB888 源像素。
 * @param width 源图宽度。
 * @param height 源图高度。
 * @param source_x 源图 x 坐标（浮点）。
 * @param source_y 源图 y 坐标（浮点）。
 * @param output_pixel 输出 3 字节像素。
 */
void sample_bilinear_pixel(const uint8_t *source_pixels, int width, int height,
                           float source_x, float source_y,
                           uint8_t *output_pixel) {
    source_x = clamp_coordinate(source_x, static_cast<float>(width - 1));
    source_y = clamp_coordinate(source_y, static_cast<float>(height - 1));

    const int left_x = static_cast<int>(source_x);
    const int top_y = static_cast<int>(source_y);
    const int right_x = (left_x + 1 < width) ? (left_x + 1) : left_x;
    const int bottom_y = (top_y + 1 < height) ? (top_y + 1) : top_y;
    const float weight_x = source_x - static_cast<float>(left_x);
    const float weight_y = source_y - static_cast<float>(top_y);

    const uint8_t *pixel00 =
        source_pixels + (static_cast<size_t>(top_y) * width + left_x) * 3;
    const uint8_t *pixel01 =
        source_pixels + (static_cast<size_t>(top_y) * width + right_x) * 3;
    const uint8_t *pixel10 =
        source_pixels + (static_cast<size_t>(bottom_y) * width + left_x) * 3;
    const uint8_t *pixel11 =
        source_pixels + (static_cast<size_t>(bottom_y) * width + right_x) * 3;

    for (int channel = 0; channel < 3; ++channel) {
        const float top_value = (1.0f - weight_x) * pixel00[channel] +
                                weight_x * pixel01[channel];
        const float bottom_value = (1.0f - weight_x) * pixel10[channel] +
                                   weight_x * pixel11[channel];
        const float interpolated_value =
            (1.0f - weight_y) * top_value + weight_y * bottom_value;
        output_pixel[channel] =
            static_cast<uint8_t>(interpolated_value + 0.5f);
    }
}

}  // namespace

bool build_document_homography(const float normalized_corners[8],
                               int image_width, int image_height,
                               DocumentHomography *homography) {
    if (!normalized_corners || !homography || image_width <= 0 ||
        image_height <= 0) {
        return false;
    }

    float clipped_corners[8];
    for (int index = 0; index < 8; ++index) {
        if (!std::isfinite(normalized_corners[index])) return false;
        clipped_corners[index] =
            clamp_coordinate(normalized_corners[index], 1.0f);
    }

    const float top_left_x = clipped_corners[0] * image_width;
    const float top_left_y = clipped_corners[1] * image_height;
    const float top_right_x = clipped_corners[2] * image_width;
    const float top_right_y = clipped_corners[3] * image_height;
    const float bottom_right_x = clipped_corners[4] * image_width;
    const float bottom_right_y = clipped_corners[5] * image_height;
    const float bottom_left_x = clipped_corners[6] * image_width;
    const float bottom_left_y = clipped_corners[7] * image_height;

    const float delta_x1 = top_right_x - bottom_right_x;
    const float delta_x2 = bottom_left_x - bottom_right_x;
    const float delta_x3 =
        top_left_x - top_right_x + bottom_right_x - bottom_left_x;
    const float delta_y1 = top_right_y - bottom_right_y;
    const float delta_y2 = bottom_left_y - bottom_right_y;
    const float delta_y3 =
        top_left_y - top_right_y + bottom_right_y - bottom_left_y;

    float projective_g = 0.0f;
    float projective_h = 0.0f;
    const float epsilon = 1e-6f;
    if (std::fabs(delta_x3) > epsilon || std::fabs(delta_y3) > epsilon) {
        const float denominator = delta_x1 * delta_y2 - delta_x2 * delta_y1;
        if (std::fabs(denominator) < epsilon) return false;
        projective_g =
            (delta_x3 * delta_y2 - delta_x2 * delta_y3) / denominator;
        projective_h =
            (delta_x1 * delta_y3 - delta_x3 * delta_y1) / denominator;
        if (!std::isfinite(projective_g) || !std::isfinite(projective_h)) {
            return false;
        }
    }

    const float coefficient_a =
        top_right_x - top_left_x + projective_g * top_right_x;
    const float coefficient_b =
        bottom_left_x - top_left_x + projective_h * bottom_left_x;
    const float coefficient_c = top_left_x;
    const float coefficient_d =
        top_right_y - top_left_y + projective_g * top_right_y;
    const float coefficient_e =
        bottom_left_y - top_left_y + projective_h * bottom_left_y;
    const float coefficient_f = top_left_y;

    homography->values[0] = coefficient_a;
    homography->values[1] = coefficient_b;
    homography->values[2] = coefficient_c;
    homography->values[3] = coefficient_d;
    homography->values[4] = coefficient_e;
    homography->values[5] = coefficient_f;
    homography->values[6] = projective_g;
    homography->values[7] = projective_h;
    for (int index = 0; index < 8; ++index) {
        if (!std::isfinite(homography->values[index])) return false;
    }
    return true;
}

void warp_document_to_rgb_image(const uint8_t *source_pixels, int width,
                                int height,
                                const DocumentHomography &homography,
                                uint8_t *output_pixels) {
    const float coefficient_a = homography.values[0];
    const float coefficient_b = homography.values[1];
    const float coefficient_c = homography.values[2];
    const float coefficient_d = homography.values[3];
    const float coefficient_e = homography.values[4];
    const float coefficient_f = homography.values[5];
    const float projective_g = homography.values[6];
    const float projective_h = homography.values[7];

    const float inverse_width_minus_one = 1.0f / static_cast<float>(width - 1);
    const float inverse_height_minus_one =
        1.0f / static_cast<float>(height - 1);

    for (int output_y = 0; output_y < height; ++output_y) {
        const float normalized_y = output_y * inverse_height_minus_one;
        const float projective_h_term = projective_h * normalized_y + 1.0f;
        const float coefficient_b_term =
            coefficient_b * normalized_y + coefficient_c;
        const float coefficient_e_term =
            coefficient_e * normalized_y + coefficient_f;
        uint8_t *output_row =
            output_pixels + static_cast<size_t>(output_y) * width * 3;

        for (int output_x = 0; output_x < width; ++output_x) {
            const float normalized_x = output_x * inverse_width_minus_one;
            const float denominator =
                projective_g * normalized_x + projective_h_term;
            float source_x;
            float source_y;
            if (std::fabs(denominator) < 1e-9f) {
                source_x = 0.0f;
                source_y = 0.0f;
            } else {
                source_x =
                    (coefficient_a * normalized_x + coefficient_b_term) /
                    denominator;
                source_y =
                    (coefficient_d * normalized_x + coefficient_e_term) /
                    denominator;
            }
            sample_bilinear_pixel(source_pixels, width, height, source_x,
                                  source_y, output_row +
                                                    static_cast<size_t>(output_x) * 3);
        }
    }
}
