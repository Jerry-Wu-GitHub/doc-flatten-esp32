// image_codec.cpp - 基于 esp_new_jpeg 的 JPEG 编解码
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"
#include "esp_jpeg_enc.h"

#include "image_codec.h"

namespace {

/**
 * @brief 从 PSRAM 分配 16 字节对齐的缓冲区。
 *
 * PSRAM 分配失败时回退到内部 RAM。ESP32-S3 的 JPEG 解码输出和
 * 编码输入都要求 16 字节对齐。
 *
 * @param size 缓冲区大小。
 * @param alignment 对齐字节数。
 * @return 分配到的缓冲区，失败返回 nullptr。
 */
void *allocate_psram_aligned(size_t size, size_t alignment) {
    if (size == 0) return nullptr;
    void *pointer =
        heap_caps_aligned_alloc(alignment, size, MALLOC_CAP_SPIRAM);
    if (!pointer) {
        printf("[image_codec] PSRAM allocation failed (%u bytes), "
               "using internal RAM\n",
               static_cast<unsigned>(size));
        pointer = heap_caps_aligned_alloc(
            alignment, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return pointer;
}

}  // namespace

bool decode_jpeg_to_rgb_image(const uint8_t *jpeg_data, size_t jpeg_length,
                              RgbImage *output_image) {
    if (!jpeg_data || jpeg_length == 0 || !output_image) return false;
    *output_image = RgbImage{};

    jpeg_dec_config_t configuration = {};
    configuration.output_type = JPEG_PIXEL_FORMAT_RGB888;
    configuration.rotate = JPEG_ROTATE_0D;
    configuration.block_enable = false;

    jpeg_dec_handle_t decoder = nullptr;
    if (jpeg_dec_open(&configuration, &decoder) != JPEG_ERR_OK) {
        printf("[image_codec] jpeg_dec_open failed\n");
        return false;
    }

    jpeg_dec_io_t decoder_io = {};
    decoder_io.inbuf = const_cast<uint8_t *>(jpeg_data);
    decoder_io.inbuf_len = static_cast<int>(jpeg_length);

    jpeg_dec_header_info_t header_information = {};
    jpeg_error_t error =
        jpeg_dec_parse_header(decoder, &decoder_io, &header_information);
    if (error != JPEG_ERR_OK) {
        printf("[image_codec] jpeg_dec_parse_header failed (%d)\n",
               static_cast<int>(error));
        jpeg_dec_close(decoder);
        return false;
    }

    int output_buffer_size = 0;
    error = jpeg_dec_get_outbuf_len(decoder, &output_buffer_size);
    if (error != JPEG_ERR_OK || output_buffer_size <= 0) {
        output_buffer_size = header_information.width *
                             header_information.height * 3;
    }

    uint8_t *pixels =
        static_cast<uint8_t *>(allocate_psram_aligned(output_buffer_size, 16));
    if (!pixels) {
        jpeg_dec_close(decoder);
        return false;
    }

    decoder_io.outbuf = pixels;
    decoder_io.out_size = output_buffer_size;
    error = jpeg_dec_process(decoder, &decoder_io);
    if (error != JPEG_ERR_OK) {
        printf("[image_codec] jpeg_dec_process failed (%d)\n",
               static_cast<int>(error));
        heap_caps_free(pixels);
        jpeg_dec_close(decoder);
        return false;
    }
    jpeg_dec_close(decoder);

    output_image->pixels = pixels;
    output_image->width = header_information.width;
    output_image->height = header_information.height;
    output_image->size_in_bytes =
        static_cast<size_t>(header_information.width) *
        static_cast<size_t>(header_information.height) * 3;
    return true;
}

bool encode_rgb_image_to_jpeg(const uint8_t *rgb_pixels, int width, int height,
                              int quality, uint8_t **output_jpeg,
                              size_t *output_jpeg_length) {
    if (!rgb_pixels || width <= 0 || height <= 0 || !output_jpeg ||
        !output_jpeg_length) {
        return false;
    }
    *output_jpeg = nullptr;
    *output_jpeg_length = 0;
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;

    jpeg_enc_config_t configuration = {};
    configuration.width = width;
    configuration.height = height;
    configuration.src_type = JPEG_PIXEL_FORMAT_RGB888;
    configuration.subsampling = JPEG_SUBSAMPLE_420;
    configuration.quality = static_cast<uint8_t>(quality);
    configuration.rotate = JPEG_ROTATE_0D;
    configuration.task_enable = false;
    configuration.hfm_task_priority = 13;
    configuration.hfm_task_core = 1;

    jpeg_enc_handle_t encoder = nullptr;
    if (jpeg_enc_open(&configuration, &encoder) != JPEG_ERR_OK) {
        printf("[image_codec] jpeg_enc_open failed\n");
        return false;
    }

    const size_t input_size = static_cast<size_t>(width) * height * 3;
    const size_t output_capacity = input_size + 65536;
    uint8_t *encoded_output =
        static_cast<uint8_t *>(heap_caps_malloc(output_capacity,
                                                MALLOC_CAP_SPIRAM));
    if (!encoded_output) {
        printf("[image_codec] encode buffer allocation failed (%u bytes)\n",
               static_cast<unsigned>(output_capacity));
        jpeg_enc_close(encoder);
        return false;
    }

    int encoded_size = 0;
    const jpeg_error_t error = jpeg_enc_process(
        encoder, rgb_pixels, static_cast<int>(input_size), encoded_output,
        static_cast<int>(output_capacity), &encoded_size);
    jpeg_enc_close(encoder);
    if (error != JPEG_ERR_OK || encoded_size <= 0) {
        printf("[image_codec] jpeg_enc_process failed (%d)\n",
               static_cast<int>(error));
        heap_caps_free(encoded_output);
        return false;
    }

    // 缩到实际大小，避免长时间占着编码临时缓冲区。
    uint8_t *exact_output = static_cast<uint8_t *>(
        heap_caps_malloc(static_cast<size_t>(encoded_size), MALLOC_CAP_SPIRAM));
    if (exact_output) {
        memcpy(exact_output, encoded_output,
               static_cast<size_t>(encoded_size));
        heap_caps_free(encoded_output);
        encoded_output = exact_output;
    }

    *output_jpeg = encoded_output;
    *output_jpeg_length = static_cast<size_t>(encoded_size);
    return true;
}

void free_rgb_image(RgbImage *image) {
    if (!image) return;
    if (image->pixels) heap_caps_free(image->pixels);
    *image = RgbImage{};
}
