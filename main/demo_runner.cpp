// demo_runner.cpp - 批量读取、检测、展平、保存
#include <cstdio>
#include <string>
#include <vector>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "demo_runner.h"
#include "document_flatten.h"
#include "file_storage.h"

namespace {

constexpr int kDemoJpegQuality = 85;

/**
 * @brief 打印单张图片各阶段耗时。
 *
 * @param timings 处理耗时。
 * @param read_milliseconds 文件读取耗时（毫秒）。
 * @param write_milliseconds 文件写入耗时（毫秒）。
 */
void print_timing_breakdown(const DocumentFlattenTimings &timings,
                            double read_milliseconds,
                            double write_milliseconds) {
    printf("  breakdown: decode=%.1f detect=%.1f (pre=%.1f infer=%.1f) "
           "warp=%.1f encode=%.1f read=%.1f write=%.1f ms\n",
           timings.decode_microseconds / 1000.0,
           timings.detect_microseconds / 1000.0,
           timings.preprocess_microseconds / 1000.0,
           timings.inference_microseconds / 1000.0,
           timings.warp_microseconds / 1000.0,
           timings.encode_microseconds / 1000.0, read_milliseconds,
           write_milliseconds);
}

}  // namespace

void process_all_images(const char *input_directory,
                        const char *output_directory) {
    const int removed_count = clear_directory(output_directory, nullptr);
    printf("[demo_runner] cleared %d old output file(s)\n", removed_count);

    std::vector<std::string> file_names;
    if (!list_jpeg_files(input_directory, &file_names) || file_names.empty()) {
        printf("[demo_runner] no JPEG files in %s\n", input_directory);
        return;
    }

    print_file_system_information();
    printf("[demo_runner] %u image(s), JPEG quality=%d\n",
           static_cast<unsigned>(file_names.size()), kDemoJpegQuality);

    int document_count = 0;
    int failed_count = 0;
    const int64_t run_start = esp_timer_get_time();

    for (size_t index = 0; index < file_names.size(); ++index) {
        const std::string &file_name = file_names[index];
        const std::string input_path =
            std::string(input_directory) + "/" + file_name;

        uint8_t *jpeg_data = nullptr;
        size_t jpeg_length = 0;
        const int64_t read_start = esp_timer_get_time();
        const bool read_succeeded = read_file(input_path, &jpeg_data, &jpeg_length);
        const int64_t read_microseconds =
            esp_timer_get_time() - read_start;
        if (!read_succeeded) {
            ++failed_count;
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        uint8_t *flattened_jpeg = nullptr;
        size_t flattened_length = 0;
        DocumentCornerResult corner_result = {};
        DocumentFlattenTimings timings = {};

        const int64_t process_start = esp_timer_get_time();
        const bool process_succeeded = detect_and_flatten_document(
            jpeg_data, jpeg_length, &flattened_jpeg, &flattened_length,
            &corner_result, kDemoJpegQuality, &timings);
        const int64_t process_microseconds =
            esp_timer_get_time() - process_start;
        heap_caps_free(jpeg_data);

        char corner_text[160];
        snprintf(corner_text, sizeof(corner_text),
                 "(%.3f,%.3f)(%.3f,%.3f)(%.3f,%.3f)(%.3f,%.3f)",
                 corner_result.normalized_x[0], corner_result.normalized_y[0],
                 corner_result.normalized_x[1], corner_result.normalized_y[1],
                 corner_result.normalized_x[2], corner_result.normalized_y[2],
                 corner_result.normalized_x[3], corner_result.normalized_y[3]);

        if (!process_succeeded) {
            ++failed_count;
            printf("[%u/%u] %s score=%.3f %s time=%.1f ms -> no document\n",
                   static_cast<unsigned>(index + 1),
                   static_cast<unsigned>(file_names.size()), file_name.c_str(),
                   corner_result.score, corner_text,
                   process_microseconds / 1000.0);
            print_timing_breakdown(timings, read_microseconds / 1000.0, 0.0);
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        const std::string output_name = file_stem(file_name) + "_flat.jpg";
        const std::string output_path =
            std::string(output_directory) + "/" + output_name;
        const int64_t write_start = esp_timer_get_time();
        const bool write_succeeded =
            write_file(output_path, flattened_jpeg, flattened_length);
        const int64_t write_microseconds =
            esp_timer_get_time() - write_start;
        if (!write_succeeded) {
            ++failed_count;
            heap_caps_free(flattened_jpeg);
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        ++document_count;
        printf("[%u/%u] %s score=%.3f %s time=%.1f ms -> %s (%u bytes)\n",
               static_cast<unsigned>(index + 1),
               static_cast<unsigned>(file_names.size()), file_name.c_str(),
               corner_result.score, corner_text,
               process_microseconds / 1000.0, output_name.c_str(),
               static_cast<unsigned>(flattened_length));
        print_timing_breakdown(timings, read_microseconds / 1000.0,
                               write_microseconds / 1000.0);
        fflush(stdout);
        heap_caps_free(flattened_jpeg);
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    const int64_t total_microseconds = esp_timer_get_time() - run_start;
    printf("[demo_runner] done: %d document(s), %d failed, total %.1f s "
           "(%.1f ms/image)\n",
           document_count, failed_count, total_microseconds / 1000000.0,
           file_names.empty()
               ? 0.0
               : total_microseconds / 1000.0 /
                     static_cast<double>(file_names.size()));
    print_file_system_information();
    printf("RUN_DONE documents=%d failed=%d\n", document_count, failed_count);
}
