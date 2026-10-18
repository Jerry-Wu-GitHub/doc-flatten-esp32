// document_flatten.h - 文档检测与透视展平对外接口
//
// 角点顺序固定为左上、右上、右下、左下，坐标是相对原图宽高的
// 归一化值 [0, 1]。
#pragma once

#include <cstddef>
#include <cstdint>

#include "document_corner.h"

/**
 * @brief 单张图片各阶段耗时（微秒）。
 */
struct DocumentFlattenTimings {
    int32_t decode_microseconds;      ///< JPEG 解码。
    int32_t detect_microseconds;      ///< 检测总耗时。
    int32_t preprocess_microseconds;  ///< 缩放到模型输入并量化。
    int32_t inference_microseconds;   ///< tflite 推理。
    int32_t warp_microseconds;        ///< 透视展平。
    int32_t encode_microseconds;      ///< JPEG 编码。
};

/**
 * @brief 对 RGB888 图片执行透视展平。
 *
 * @param source_rgb_pixels 源图片 RGB888 像素。
 * @param width 图片宽度。
 * @param height 图片高度。
 * @param normalized_corners 归一化四角坐标，长度为 8。
 * @param output_rgb_pixels 输出 RGB888 像素，尺寸与源图相同。
 * @return 展平成功返回 true。
 */
bool flatten_document_rgb_image(const uint8_t *source_rgb_pixels, int width,
                                int height,
                                const float normalized_corners[8],
                                uint8_t *output_rgb_pixels);

/**
 * @brief 对 JPEG 图片按给定角点执行透视展平。
 *
 * @param jpeg_data JPEG 字节。
 * @param jpeg_length JPEG 字节长度。
 * @param normalized_corners 归一化四角坐标，长度为 8。
 * @param output_jpeg 输出 JPEG 缓冲区，由 PSRAM 分配。
 * @param output_jpeg_length 输出 JPEG 字节长度。
 * @param quality 输出 JPEG 质量，范围 1..100。
 * @return 展平成功返回 true。
 */
bool flatten_document_jpeg(const uint8_t *jpeg_data, size_t jpeg_length,
                           const float normalized_corners[8],
                           uint8_t **output_jpeg, size_t *output_jpeg_length,
                           int quality = 85);

/**
 * @brief 检测文档并直接展平。
 *
 * 未检测到文档（score 小于 DOCUMENT_SCORE_THRESHOLD）时返回 false，
 * output_jpeg 置空。
 *
 * @param jpeg_data JPEG 字节。
 * @param jpeg_length JPEG 字节长度。
 * @param output_jpeg 输出 JPEG 缓冲区，由 PSRAM 分配。
 * @param output_jpeg_length 输出 JPEG 字节长度。
 * @param corner_result 可选输出，返回检测结果。
 * @param quality 输出 JPEG 质量，范围 1..100。
 * @param timings 可选输出，返回各阶段耗时。
 * @return 检测并展平成功返回 true。
 */
bool detect_and_flatten_document(const uint8_t *jpeg_data, size_t jpeg_length,
                                 uint8_t **output_jpeg,
                                 size_t *output_jpeg_length,
                                 DocumentCornerResult *corner_result = nullptr,
                                 int quality = 85,
                                 DocumentFlattenTimings *timings = nullptr);

/**
 * @brief 初始化内置文档检测器。
 *
 * @param model_data 模型起始地址。
 * @param model_length 模型长度（字节）。
 * @param tensor_arena_size 张量竞技场大小（字节），建议 1 MB。
 * @return 初始化成功返回 true。
 */
bool document_flatten_initialize(const unsigned char *model_data,
                                 unsigned model_length,
                                 size_t tensor_arena_size);

/**
 * @brief 检测器是否已初始化。
 *
 * @return 已初始化返回 true。
 */
bool document_flattener_is_ready();
