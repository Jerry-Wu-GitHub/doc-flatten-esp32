// document_flatten.cpp - RGB888 / JPEG 展平实现
#include <cstdio>

#include "esp_heap_caps.h"

#include "document_flatten.h"
#include "document_geometry.h"
#include "image_codec.h"

bool flatten_document_rgb_image(const uint8_t *source_rgb_pixels, int width,
                                int height,
                                const float normalized_corners[8],
                                uint8_t *output_rgb_pixels) {
    if (!source_rgb_pixels || !output_rgb_pixels || !normalized_corners ||
        width <= 1 || height <= 1) {
        return false;
    }

    DocumentHomography homography;
    if (!build_document_homography(normalized_corners, width, height,
                                   &homography)) {
        return false;
    }
    warp_document_to_rgb_image(source_rgb_pixels, width, height, homography,
                               output_rgb_pixels);
    return true;
}

bool flatten_document_jpeg(const uint8_t *jpeg_data, size_t jpeg_length,
                           const float normalized_corners[8],
                           uint8_t **output_jpeg, size_t *output_jpeg_length,
                           int quality) {
    if (!jpeg_data || jpeg_length == 0 || !normalized_corners || !output_jpeg ||
        !output_jpeg_length) {
        return false;
    }
    *output_jpeg = nullptr;
    *output_jpeg_length = 0;

    RgbImage source_image;
    if (!decode_jpeg_to_rgb_image(jpeg_data, jpeg_length, &source_image)) {
        return false;
    }

    uint8_t *flattened_pixels = static_cast<uint8_t *>(heap_caps_aligned_alloc(
        16, source_image.size_in_bytes, MALLOC_CAP_SPIRAM));
    if (!flattened_pixels) {
        printf("[document_flatten] output buffer allocation failed "
               "(%u bytes)\n",
               static_cast<unsigned>(source_image.size_in_bytes));
        free_rgb_image(&source_image);
        return false;
    }

    bool succeeded = flatten_document_rgb_image(
        source_image.pixels, source_image.width, source_image.height,
        normalized_corners, flattened_pixels);
    if (succeeded) {
        succeeded = encode_rgb_image_to_jpeg(
            flattened_pixels, source_image.width, source_image.height, quality,
            output_jpeg, output_jpeg_length);
    }

    heap_caps_free(flattened_pixels);
    free_rgb_image(&source_image);
    return succeeded;
}
