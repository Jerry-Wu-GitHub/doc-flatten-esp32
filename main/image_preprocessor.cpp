// image_preprocessor.cpp - 双线性缩放 + 模型输入量化
#include <cmath>

#include "image_preprocessor.h"

namespace {

/**
 * @brief 把一个浮点像素值量化为模型输入类型。
 *
 * @param pixel_value 0..255 的浮点像素值。
 * @param inverse_scale 模型量化 scale 的倒数。
 * @param zero_point 模型量化 zero point。
 * @param is_int8 输入张量是否为 int8。
 * @return 量化后的整数。
 */
int quantize_pixel_value(float pixel_value, float inverse_scale, int zero_point,
                         bool is_int8) {
    const float normalized_value = pixel_value * (2.0f / 255.0f) - 1.0f;
    int quantized_value =
        static_cast<int>(lrintf(normalized_value * inverse_scale)) + zero_point;
    if (is_int8) {
        if (quantized_value < -128) quantized_value = -128;
        if (quantized_value > 127) quantized_value = 127;
    } else {
        if (quantized_value < 0) quantized_value = 0;
        if (quantized_value > 255) quantized_value = 255;
    }
    return quantized_value;
}

}  // namespace

void preprocess_rgb_image(const uint8_t *source_pixels, int source_width,
                          int source_height, TfLiteTensor *input_tensor) {
    const int input_height = input_tensor->dims->data[1];
    const int input_width = input_tensor->dims->data[2];
    const int input_channels = input_tensor->dims->data[3];
    const float inverse_scale = 1.0f / input_tensor->params.scale;
    const int zero_point = input_tensor->params.zero_point;
    const bool is_int8 = (input_tensor->type == kTfLiteInt8);
    int8_t *int8_destination = input_tensor->data.int8;
    uint8_t *uint8_destination = input_tensor->data.uint8;

    // 如果源图已经等于模型输入尺寸，跳过双线性采样，只做量化。
    if (source_width == input_width && source_height == input_height) {
        const size_t pixel_count =
            static_cast<size_t>(input_width) * static_cast<size_t>(input_height);
        for (size_t pixel_index = 0; pixel_index < pixel_count; ++pixel_index) {
            const size_t output_index = pixel_index * static_cast<size_t>(input_channels);
            for (int channel = 0; channel < input_channels && channel < 3; ++channel) {
                const float pixel_value =
                    static_cast<float>(source_pixels[pixel_index * 3 + channel]);
                const int quantized_value =
                    quantize_pixel_value(pixel_value, inverse_scale, zero_point,
                                         is_int8);
                if (is_int8) {
                    int8_destination[output_index + channel] =
                        static_cast<int8_t>(quantized_value);
                } else {
                    uint8_destination[output_index + channel] =
                        static_cast<uint8_t>(quantized_value);
                }
            }
        }
        return;
    }

    const float horizontal_scale =
        static_cast<float>(source_width) / static_cast<float>(input_width);
    const float vertical_scale =
        static_cast<float>(source_height) / static_cast<float>(input_height);

    for (int output_y = 0; output_y < input_height; ++output_y) {
        float source_y = (output_y + 0.5f) * vertical_scale - 0.5f;
        if (source_y < 0.0f) source_y = 0.0f;
        int source_y0 = static_cast<int>(source_y);
        if (source_y0 > source_height - 1) source_y0 = source_height - 1;
        const int source_y1 =
            (source_y0 + 1 < source_height) ? (source_y0 + 1) : source_y0;
        const float weight_y = source_y - static_cast<float>(source_y0);
        const uint8_t *source_row0 =
            source_pixels + static_cast<size_t>(source_y0) * source_width * 3;
        const uint8_t *source_row1 =
            source_pixels + static_cast<size_t>(source_y1) * source_width * 3;

        for (int output_x = 0; output_x < input_width; ++output_x) {
            float source_x = (output_x + 0.5f) * horizontal_scale - 0.5f;
            if (source_x < 0.0f) source_x = 0.0f;
            int source_x0 = static_cast<int>(source_x);
            if (source_x0 > source_width - 1) source_x0 = source_width - 1;
            const int source_x1 =
                (source_x0 + 1 < source_width) ? (source_x0 + 1) : source_x0;
            const float weight_x = source_x - static_cast<float>(source_x0);

            const uint8_t *pixel00 = source_row0 + static_cast<size_t>(source_x0) * 3;
            const uint8_t *pixel01 = source_row0 + static_cast<size_t>(source_x1) * 3;
            const uint8_t *pixel10 = source_row1 + static_cast<size_t>(source_x0) * 3;
            const uint8_t *pixel11 = source_row1 + static_cast<size_t>(source_x1) * 3;

            const size_t output_index =
                (static_cast<size_t>(output_y) * input_width + output_x) *
                static_cast<size_t>(input_channels);
            for (int channel = 0; channel < input_channels && channel < 3; ++channel) {
                const float top_value = (1.0f - weight_x) * pixel00[channel] +
                                        weight_x * pixel01[channel];
                const float bottom_value = (1.0f - weight_x) * pixel10[channel] +
                                           weight_x * pixel11[channel];
                const float interpolated_value =
                    (1.0f - weight_y) * top_value + weight_y * bottom_value;
                const int quantized_value =
                    quantize_pixel_value(interpolated_value, inverse_scale,
                                         zero_point, is_int8);
                if (is_int8) {
                    int8_destination[output_index + channel] =
                        static_cast<int8_t>(quantized_value);
                } else {
                    uint8_destination[output_index + channel] =
                        static_cast<uint8_t>(quantized_value);
                }
            }
        }
    }
}
