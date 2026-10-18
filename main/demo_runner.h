// demo_runner.h - 示例程序的批量处理流程
#pragma once

/**
 * @brief 处理目录中的全部 JPEG 图片，并保存展平结果。
 *
 * @param input_directory 输入目录。
 * @param output_directory 输出目录。
 */
void process_all_images(const char *input_directory,
                        const char *output_directory);
