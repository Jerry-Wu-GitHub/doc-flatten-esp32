// document_corner.cpp - 文档四角检测实现
#include <cmath>
#include <cstdio>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "document_corner.h"
#include "image_preprocessor.h"

namespace {

uint8_t *s_tensor_arena = nullptr;
tflite::MicroInterpreter *s_interpreter = nullptr;
TfLiteTensor *s_input_tensor = nullptr;
TfLiteTensor *s_coordinates_tensor = nullptr;
TfLiteTensor *s_logit_tensor = nullptr;

/**
 * @brief 按最后一维元素数量查找输出张量。
 *
 * 模型输出顺序可能变化，因此不依赖输出顺序，只根据末维大小识别
 * 坐标张量（8）和 logit 张量（1）。
 *
 * @param last_dimension 需要的最后一维元素数量。
 * @return 找到的张量，找不到返回 nullptr。
 */
TfLiteTensor *find_output_tensor_by_last_dimension(int last_dimension) {
    for (size_t output_index = 0; output_index < s_interpreter->outputs_size();
         ++output_index) {
        TfLiteTensor *tensor = s_interpreter->output(output_index);
        const int dimension_count = tensor->dims->size;
        if (dimension_count >= 1 &&
            tensor->dims->data[dimension_count - 1] == last_dimension) {
            return tensor;
        }
    }
    return nullptr;
}

/**
 * @brief 读取并反量化张量中的一个元素。
 *
 * @param tensor 目标张量。
 * @param index 元素下标。
 * @return 反量化后的浮点值。
 */
float dequantize_tensor_value(const TfLiteTensor *tensor, int index) {
    switch (tensor->type) {
        case kTfLiteInt8:
            return static_cast<float>(tensor->data.int8[index] -
                                      tensor->params.zero_point) *
                   tensor->params.scale;
        case kTfLiteUInt8:
            return static_cast<float>(
                       static_cast<int>(tensor->data.uint8[index]) -
                       tensor->params.zero_point) *
                   tensor->params.scale;
        default:
            return tensor->data.f[index];
    }
}

}  // namespace

bool DocumentCornerDetector::initialize(const unsigned char *model_data,
                                        unsigned model_length,
                                        size_t tensor_arena_size) {
    const tflite::Model *model = tflite::GetModel(model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        printf("[document_corner] schema mismatch: model=%u library=%u\n",
               static_cast<unsigned>(model->version()),
               static_cast<unsigned>(TFLITE_SCHEMA_VERSION));
        return false;
    }

    // 张量竞技场优先放 PSRAM，224x224 的激活值放不进内部 SRAM。
    s_tensor_arena = static_cast<uint8_t *>(heap_caps_aligned_alloc(
        16, tensor_arena_size, MALLOC_CAP_SPIRAM));
    if (!s_tensor_arena) {
        printf("[document_corner] PSRAM allocation failed, using internal RAM\n");
        s_tensor_arena = static_cast<uint8_t *>(heap_caps_aligned_alloc(
            16, tensor_arena_size, MALLOC_CAP_INTERNAL));
    }
    if (!s_tensor_arena) return false;

    // 当前模型用到的算子：PAD、CONV_2D、DEPTHWISE_CONV_2D、TRANSPOSE、
    // RESHAPE、FULLY_CONNECTED。
    static tflite::MicroMutableOpResolver<6> resolver;
    resolver.AddPad();
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddTranspose();
    resolver.AddReshape();
    resolver.AddFullyConnected();

    static tflite::MicroInterpreter interpreter(
        model, resolver, s_tensor_arena, tensor_arena_size);
    s_interpreter = &interpreter;

    if (s_interpreter->AllocateTensors() != kTfLiteOk) {
        printf("[document_corner] AllocateTensors failed (arena too small?)\n");
        s_interpreter = nullptr;
        return false;
    }

    s_input_tensor = s_interpreter->input(0);
    s_coordinates_tensor = find_output_tensor_by_last_dimension(8);
    s_logit_tensor = find_output_tensor_by_last_dimension(1);
    if (!s_input_tensor || !s_coordinates_tensor || !s_logit_tensor) {
        printf("[document_corner] cannot locate input/output tensors\n");
        s_interpreter = nullptr;
        return false;
    }

    const int input_height = s_input_tensor->dims->data[1];
    const int input_width = s_input_tensor->dims->data[2];
    const int input_channels = s_input_tensor->dims->data[3];
    printf("[document_corner] input %dx%dx%d %s scale=%.8f zero_point=%d\n",
           input_width, input_height, input_channels,
           s_input_tensor->type == kTfLiteInt8
               ? "int8"
               : (s_input_tensor->type == kTfLiteUInt8 ? "uint8" : "float32"),
           static_cast<double>(s_input_tensor->params.scale),
           static_cast<int>(s_input_tensor->params.zero_point));
    printf("[document_corner] outputs: coordinates[8] scale=%.6f "
           "zero_point=%d | logit[1] scale=%.6f zero_point=%d\n",
           static_cast<double>(s_coordinates_tensor->params.scale),
           static_cast<int>(s_coordinates_tensor->params.zero_point),
           static_cast<double>(s_logit_tensor->params.scale),
           static_cast<int>(s_logit_tensor->params.zero_point));
    printf("[document_corner] tensor arena used %u / %u bytes\n",
           static_cast<unsigned>(s_interpreter->arena_used_bytes()),
           static_cast<unsigned>(tensor_arena_size));
    (void)model_length;
    return true;
}

bool DocumentCornerDetector::is_ready() const {
    return s_interpreter != nullptr;
}

bool DocumentCornerDetector::predict_corners(
    const uint8_t *rgb_pixels, int image_width, int image_height,
    DocumentCornerResult *result, int32_t *inference_microseconds,
    int32_t *preprocess_microseconds) {
    if (!s_interpreter || !rgb_pixels || !result || image_width <= 0 ||
        image_height <= 0) {
        return false;
    }

    const int64_t preprocess_start = esp_timer_get_time();
    preprocess_rgb_image(rgb_pixels, image_width, image_height, s_input_tensor);
    const int64_t preprocess_end = esp_timer_get_time();
    if (preprocess_microseconds) {
        *preprocess_microseconds =
            static_cast<int32_t>(preprocess_end - preprocess_start);
    }

    const int64_t inference_start = esp_timer_get_time();
    if (s_interpreter->Invoke() != kTfLiteOk) {
        printf("[document_corner] Invoke failed\n");
        return false;
    }
    const int64_t inference_end = esp_timer_get_time();
    if (inference_microseconds) {
        *inference_microseconds =
            static_cast<int32_t>(inference_end - inference_start);
    }

    for (int corner_index = 0; corner_index < 4; ++corner_index) {
        float normalized_x = dequantize_tensor_value(
            s_coordinates_tensor, 2 * corner_index);
        float normalized_y = dequantize_tensor_value(
            s_coordinates_tensor, 2 * corner_index + 1);
        if (normalized_x < 0.0f) normalized_x = 0.0f;
        if (normalized_x > 1.0f) normalized_x = 1.0f;
        if (normalized_y < 0.0f) normalized_y = 0.0f;
        if (normalized_y > 1.0f) normalized_y = 1.0f;
        result->normalized_x[corner_index] = normalized_x;
        result->normalized_y[corner_index] = normalized_y;
    }

    const float logit = dequantize_tensor_value(s_logit_tensor, 0);
    result->score = 1.0f / (1.0f + expf(-logit));
    result->has_document = result->score >= DOCUMENT_SCORE_THRESHOLD;
    result->input_height = s_input_tensor->dims->data[1];
    result->input_width = s_input_tensor->dims->data[2];
    return true;
}
