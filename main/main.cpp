// main.cpp - DocFlatten ESP32-S3 示例程序
//
// 功能：
//   1. 从 SPIFFS 的 /spiffs/sample 目录逐张读取 JPG；
//   2. 调用 detect_and_flatten_document() 检测并透视展平；
//   3. 打印每张图片的角点、置信度和各阶段耗时；
//   4. 把结果保存到 /spiffs/out/<原名>_flat.jpg。
//
// 串口命令（UART0，921600）：
//   RUN      处理 /spiffs/sample
//   RUN224   处理 /spiffs/sample224
//   PULL     回传 /spiffs/out
//   PULL224  回传 /spiffs/out224
//   INFO     打印 SPIFFS 和文件数量
//   ERASE    清空两个输出目录
//   REBOOT   重启
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <strings.h>

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "demo_runner.h"
#include "document_flatten.h"
#include "file_storage.h"
#include "model_embedding.h"
#include "serial_transfer.h"

namespace {

constexpr const char *kInputDirectory = "/spiffs/sample";
constexpr const char *kOutputDirectory = "/spiffs/out";
constexpr const char *kInputDirectory224 = "/spiffs/sample224";
constexpr const char *kOutputDirectory224 = "/spiffs/out224";
constexpr size_t kTensorArenaSize = 1024 * 1024;

/**
 * @brief 去掉命令行末尾的换行符和空白字符。
 *
 * @param line 命令行缓冲区。
 */
void trim_line_ending(char *line) {
    size_t length = strlen(line);
    while (length > 0) {
        const char character = line[length - 1];
        if (character == '\r' || character == '\n' || character == ' ' ||
            character == '\t') {
            line[--length] = '\0';
        } else {
            break;
        }
    }
}

/**
 * @brief 打印支持的串口命令。
 */
void print_command_help() {
    printf("commands: RUN | RUN224 | PULL | PULL224 | INFO | ERASE | REBOOT\n");
}

/**
 * @brief 打印输入/输出目录中的文件数量。
 */
void print_file_counts() {
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::vector<std::string> inputs224;
    std::vector<std::string> outputs224;
    list_jpeg_files(kInputDirectory, &inputs);
    list_jpeg_files(kOutputDirectory, &outputs);
    list_jpeg_files(kInputDirectory224, &inputs224);
    list_jpeg_files(kOutputDirectory224, &outputs224);
    printf("[main] 800px inputs=%u outputs=%u; 224px inputs=%u outputs=%u\n",
           static_cast<unsigned>(inputs.size()),
           static_cast<unsigned>(outputs.size()),
           static_cast<unsigned>(inputs224.size()),
           static_cast<unsigned>(outputs224.size()));
}

}  // namespace

extern "C" void app_main(void) {
    printf("\n=== DocFlatten ESP32-S3 (esp-tflite-micro + esp_new_jpeg) ===\n");

    if (!mount_file_system()) return;
    print_file_system_information();
    if (!create_directory(kOutputDirectory) ||
        !create_directory(kOutputDirectory224)) {
        printf("[main] cannot create output directories\n");
        return;
    }

    const size_t model_length = static_cast<size_t>(
        document_corner_model_end - document_corner_model_start);
    printf("[main] model bytes: %u (~%.2f MB)\n",
           static_cast<unsigned>(model_length),
           static_cast<double>(model_length) / 1048576.0);

    // EMBED_FILES 生成的模型在 flash 中不保证 4 字节对齐，而 TFLite
    // 解释器要求模型数据对齐。复制一份到 16 字节对齐的 PSRAM 再使用。
    uint8_t *aligned_model = static_cast<uint8_t *>(
        heap_caps_aligned_alloc(16, model_length, MALLOC_CAP_SPIRAM));
    if (!aligned_model) {
        printf("[main] aligned model allocation failed\n");
        return;
    }
    memcpy(aligned_model, document_corner_model_start, model_length);
    if (!document_flatten_initialize(
            aligned_model, static_cast<unsigned>(model_length),
            kTensorArenaSize)) {
        printf("[main] document_flatten_initialize failed\n");
        return;
    }

    print_command_help();
    printf("READY\n");
    fflush(stdout);

    static char line[128];
    while (true) {
        if (!fgets(line, sizeof(line), stdin)) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        trim_line_ending(line);
        if (strcasecmp(line, "RUN") == 0) {
            process_all_images(kInputDirectory, kOutputDirectory);
        } else if (strcasecmp(line, "RUN224") == 0) {
            create_directory(kOutputDirectory224);
            process_all_images(kInputDirectory224, kOutputDirectory224);
        } else if (strcasecmp(line, "PULL") == 0) {
            send_all_output_images(kOutputDirectory);
        } else if (strcasecmp(line, "PULL224") == 0) {
            send_all_output_images(kOutputDirectory224);
        } else if (strcasecmp(line, "INFO") == 0) {
            print_file_system_information();
            print_file_counts();
        } else if (strcasecmp(line, "ERASE") == 0) {
            const int removed_count =
                clear_directory(kOutputDirectory, nullptr) +
                clear_directory(kOutputDirectory224, nullptr);
            printf("[main] removed %d file(s)\n", removed_count);
        } else if (strcasecmp(line, "REBOOT") == 0) {
            printf("[main] rebooting\n");
            fflush(stdout);
            esp_restart();
        } else if (line[0] != '\0') {
            printf("[main] unknown command: %s\n", line);
        }
        printf("READY\n");
        fflush(stdout);
    }
}
