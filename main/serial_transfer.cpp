// serial_transfer.cpp - base64 串口回传实现
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "esp_heap_caps.h"
#include "esp_rom_uart.h"
#include "file_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "serial_transfer.h"

namespace {

constexpr size_t kBase64LineLength = 96;
constexpr size_t kLinesPerYield = 8;

/**
 * @brief 计算 FNV-1a 32 位校验值。
 *
 * @param data 输入数据。
 * @param length 数据长度。
 * @return 校验值。
 */
uint32_t calculate_fnv1a_checksum(const uint8_t *data, size_t length) {
    uint32_t checksum = 2166136261u;
    for (size_t index = 0; index < length; ++index) {
        checksum ^= data[index];
        checksum *= 16777619u;
    }
    return checksum;
}

/**
 * @brief 把字节数据编码为 base64 字符串。
 *
 * @param data 输入数据。
 * @param length 数据长度。
 * @param output 输出缓冲区，长度至少为 ((length + 2) / 3) * 4 + 1。
 * @return 编码后的字符数（不含结尾 '\0'）。
 */
size_t encode_base64(const uint8_t *data, size_t length, char *output) {
    static const char kEncodingTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t input_index = 0;
    size_t output_index = 0;
    while (input_index + 3 <= length) {
        const uint32_t value = (static_cast<uint32_t>(data[input_index]) << 16) |
                               (static_cast<uint32_t>(data[input_index + 1]) << 8) |
                               static_cast<uint32_t>(data[input_index + 2]);
        output[output_index++] = kEncodingTable[(value >> 18) & 0x3F];
        output[output_index++] = kEncodingTable[(value >> 12) & 0x3F];
        output[output_index++] = kEncodingTable[(value >> 6) & 0x3F];
        output[output_index++] = kEncodingTable[value & 0x3F];
        input_index += 3;
    }
    if (input_index < length) {
        uint32_t value = static_cast<uint32_t>(data[input_index]) << 16;
        const bool has_second_byte = (input_index + 1 < length);
        if (has_second_byte) {
            value |= static_cast<uint32_t>(data[input_index + 1]) << 8;
        }
        output[output_index++] = kEncodingTable[(value >> 18) & 0x3F];
        output[output_index++] = kEncodingTable[(value >> 12) & 0x3F];
        output[output_index++] =
            has_second_byte ? kEncodingTable[(value >> 6) & 0x3F] : '=';
        output[output_index++] = '=';
    }
    output[output_index] = '\0';
    return output_index;
}

}  // namespace

void send_file_as_base64(const std::string &file_path,
                         const std::string &file_name) {
    uint8_t *data = nullptr;
    size_t length = 0;
    if (!read_file(file_path, &data, &length)) return;

    const uint32_t checksum = calculate_fnv1a_checksum(data, length);
    const size_t encoded_capacity = ((length + 2) / 3) * 4 + 1;
    char *encoded_output =
        static_cast<char *>(allocate_psram_memory(encoded_capacity));
    if (!encoded_output) {
        printf("[serial_transfer] base64 buffer allocation failed "
               "(%u bytes)\n",
               static_cast<unsigned>(encoded_capacity));
        heap_caps_free(data);
        return;
    }

    const size_t encoded_length =
        encode_base64(data, length, encoded_output);
    heap_caps_free(data);

    printf("FILE_BEGIN %s %u %08x\n", file_name.c_str(),
           static_cast<unsigned>(length), static_cast<unsigned>(checksum));
    size_t lines_since_yield = 0;
    for (size_t offset = 0; offset < encoded_length;
         offset += kBase64LineLength) {
        const size_t chunk_length =
            std::min(kBase64LineLength, encoded_length - offset);
        fwrite(encoded_output + offset, 1, chunk_length, stdout);
        fputc('\n', stdout);
        // 默认 UART VFS 写 FIFO 不阻塞，连续输出会丢字符；每行等待
        // FIFO 排空，并定期让出 CPU 喂任务看门狗。
        esp_rom_output_tx_wait_idle(
            static_cast<uint8_t>(CONFIG_ESP_CONSOLE_UART_NUM));
        if (++lines_since_yield >= kLinesPerYield) {
            lines_since_yield = 0;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    printf("FILE_END %s\n", file_name.c_str());
    fflush(stdout);
    heap_caps_free(encoded_output);
}

void send_all_output_images(const char *output_directory) {
    std::vector<std::string> file_names;
    if (!list_jpeg_files(output_directory, &file_names) || file_names.empty()) {
        printf("[serial_transfer] no output images in %s\n", output_directory);
        printf("PULL_DONE count=0\n");
        return;
    }

    printf("[serial_transfer] sending %u image(s) from %s\n",
           static_cast<unsigned>(file_names.size()), output_directory);
    for (size_t index = 0; index < file_names.size(); ++index) {
        const std::string &file_name = file_names[index];
        const std::string file_path =
            std::string(output_directory) + "/" + file_name;
        printf("[serial_transfer] %u/%u %s\n",
               static_cast<unsigned>(index + 1),
               static_cast<unsigned>(file_names.size()), file_name.c_str());
        fflush(stdout);
        send_file_as_base64(file_path, file_name);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    printf("PULL_DONE count=%u\n", static_cast<unsigned>(file_names.size()));
}
