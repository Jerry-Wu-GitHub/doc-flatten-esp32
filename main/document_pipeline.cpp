// document_pipeline.cpp - 检测 + 展平一体化流程
#include <cstdio>

#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "document_flatten.h"
#include "document_geometry.h"
#include "image_codec.h"

namespace {

DocumentCornerDetector s_detector;
bool s_is_initialized = false;

}  // namespace

bool document_flatten_initialize(const unsigned char *model_data,
                                 unsigned model_length,
                                 size_t tensor_arena_size) {
    s_is_initialized =
        s_detector.initialize(model_data, model_length, tensor_arena_size);
    return s_is_initialized;
}

bool document_flattener_is_ready() { return s_is_initialized; }

bool detect_and_flatten_document(const uint8_t *jpeg_data, size_t jpeg_length,
                                 uint8_t **output_jpeg,
                                 size_t *output_jpeg_length,
                                 DocumentCornerResult *corner_result,
                                 int quality,
                                 DocumentFlattenTimings *timings) {
    if (!output_jpeg || !output_jpeg_length) return false;
    *output_jpeg = nullptr;
    *output_jpeg_length = 0;

    DocumentFlattenTimings local_timings = {};
    DocumentFlattenTimings *timing_output =
        timings ? timings : &local_timings;
    *timing_output = DocumentFlattenTimings{};

    if (!s_is_initialized) {
        printf("[document_flatten] detector is not initialized\n");
        return false;
    }
    if (!jpeg_data || jpeg_length == 0) return false;

    const int64_t decode_start = esp_timer_get_time();
    RgbImage source_image;
    if (!decode_jpeg_to_rgb_image(jpeg_data, jpeg_length, &source_image)) {
        return false;
    }
    timing_output->decode_microseconds =
        static_cast<int32_t>(esp_timer_get_time() - decode_start);

    DocumentCornerResult detected_corners = {};
    int32_t inference_microseconds = 0;
    int32_t preprocess_microseconds = 0;
    const int64_t detect_start = esp_timer_get_time();
    if (!s_detector.predict_corners(source_image.pixels, source_image.width,
                                    source_image.height, &detected_corners,
                                    &inference_microseconds,
                                    &preprocess_microseconds)) {
        free_rgb_image(&source_image);
        return false;
    }
    timing_output->detect_microseconds =
        static_cast<int32_t>(esp_timer_get_time() - detect_start);
    timing_output->preprocess_microseconds = preprocess_microseconds;
    timing_output->inference_microseconds = inference_microseconds;
    if (corner_result) *corner_result = detected_corners;

    if (!detected_corners.has_document) {
        free_rgb_image(&source_image);
        return false;
    }

    const float normalized_corners[8] = {
        detected_corners.normalized_x[0], detected_corners.normalized_y[0],
        detected_corners.normalized_x[1], detected_corners.normalized_y[1],
        detected_corners.normalized_x[2], detected_corners.normalized_y[2],
        detected_corners.normalized_x[3], detected_corners.normalized_y[3],
    };

    uint8_t *flattened_pixels = static_cast<uint8_t *>(heap_caps_aligned_alloc(
        16, source_image.size_in_bytes, MALLOC_CAP_SPIRAM));
    if (!flattened_pixels) {
        printf("[document_flatten] output buffer allocation failed "
               "(%u bytes)\n",
               static_cast<unsigned>(source_image.size_in_bytes));
        free_rgb_image(&source_image);
        return false;
    }

    DocumentHomography homography;
    const int64_t warp_start = esp_timer_get_time();
    const bool homography_ready = build_document_homography(
        normalized_corners, source_image.width, source_image.height,
        &homography);
    if (homography_ready) {
        warp_document_to_rgb_image(source_image.pixels, source_image.width,
                                   source_image.height, homography,
                                   flattened_pixels);
    }
    timing_output->warp_microseconds =
        static_cast<int32_t>(esp_timer_get_time() - warp_start);

    bool succeeded = homography_ready;
    if (succeeded) {
        const int64_t encode_start = esp_timer_get_time();
        succeeded = encode_rgb_image_to_jpeg(
            flattened_pixels, source_image.width, source_image.height, quality,
            output_jpeg, output_jpeg_length);
        timing_output->encode_microseconds =
            static_cast<int32_t>(esp_timer_get_time() - encode_start);
    }

    heap_caps_free(flattened_pixels);
    free_rgb_image(&source_image);
    return succeeded;
}
