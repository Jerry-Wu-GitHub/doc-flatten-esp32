// document_corner.h - 文档四角检测（esp-tflite-micro）
//
// 本模块只负责“输入 RGB888 图片，输出文档四角”。角点顺序固定为
// 左上、右上、右下、左下，坐标是相对原图宽高的归一化值 [0, 1]。
#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief 文档四角检测结果。
 *
 * 四个角点的顺序为：左上、右上、右下、左下。坐标已经裁剪到 [0, 1]。
 */
struct DocumentCornerResult {
    float normalized_x[4];  ///< 四个角点的归一化 x 坐标。
    float normalized_y[4];  ///< 四个角点的归一化 y 坐标。
    float score;            ///< sigmoid(logit)，图片中存在文档的概率。
    bool has_document;      ///< score 是否达到文档阈值。
    int input_width;        ///< 模型输入宽度。
    int input_height;       ///< 模型输入高度。
};

#ifndef DOCUMENT_SCORE_THRESHOLD
#define DOCUMENT_SCORE_THRESHOLD 0.5f
#endif

/**
 * @brief 文档四角检测器。
 *
 * 内部使用 esp-tflite-micro 解释器。检测器使用静态状态，同一时刻
 * 只应有一个实例进行推理；多任务场景需要调用方自行加锁。
 */
class DocumentCornerDetector {
public:
    /**
     * @brief 初始化检测器并分配张量竞技场。
     *
     * @param model_data 模型文件起始地址。
     * @param model_length 模型文件长度（字节）。
     * @param tensor_arena_size 张量竞技场大小（字节），建议 1 MB。
     * @return 初始化成功返回 true。
     */
    bool initialize(const unsigned char *model_data, unsigned model_length,
                    size_t tensor_arena_size);

    /**
     * @brief 检测图片中的文档四角。
     *
     * @param rgb_pixels RGB888 像素数据，行优先，每像素 3 字节。
     * @param image_width 原图宽度。
     * @param image_height 原图高度。
     * @param result 输出检测结果，不能为空。
     * @param inference_microseconds 可选输出，纯推理耗时（微秒）。
     * @param preprocess_microseconds 可选输出，预处理耗时（微秒）。
     * @return 检测成功返回 true。
     */
    bool predict_corners(const uint8_t *rgb_pixels, int image_width,
                         int image_height, DocumentCornerResult *result,
                         int32_t *inference_microseconds = nullptr,
                         int32_t *preprocess_microseconds = nullptr);

    /**
     * @brief 检测器是否已经初始化。
     *
     * @return 已初始化返回 true。
     */
    bool is_ready() const;
};
