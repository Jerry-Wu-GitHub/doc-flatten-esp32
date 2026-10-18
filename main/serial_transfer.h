// serial_transfer.h - 通过串口回传图片
#pragma once

#include <string>

/**
 * @brief 把文件编码为 base64 并发送。
 *
 * 输出格式：
 *   FILE_BEGIN <文件名> <字节数> <FNV-1a 校验值>
 *   <base64 行>
 *   FILE_END <文件名>
 *
 * @param file_path 文件路径。
 * @param file_name 文件名（用于协议标记）。
 */
void send_file_as_base64(const std::string &file_path,
                         const std::string &file_name);

/**
 * @brief 回传目录中的全部 JPEG 文件。
 *
 * @param output_directory 输出目录。
 */
void send_all_output_images(const char *output_directory);
