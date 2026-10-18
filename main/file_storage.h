// file_storage.h - SPIFFS 文件读写与目录工具
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief 挂载 SPIFFS 文件系统。
 *
 * @return 挂载成功返回 true。
 */
bool mount_file_system();

/**
 * @brief 打印 SPIFFS 空间使用情况。
 */
void print_file_system_information();

/**
 * @brief 从 PSRAM 分配内存，失败时回退到内部 RAM。
 *
 * @param size 需要的字节数。
 * @return 分配到的缓冲区，失败返回 nullptr。
 */
void *allocate_psram_memory(size_t size);

/**
 * @brief 创建目录；目录已存在时也视为成功。
 *
 * @param directory_path 目录路径。
 * @return 创建成功或目录已存在返回 true。
 */
bool create_directory(const char *directory_path);

/**
 * @brief 读取整个文件。
 *
 * @param file_path 文件路径。
 * @param data 输出缓冲区，由 PSRAM 分配。
 * @param length 输出文件长度。
 * @return 读取成功返回 true。
 */
bool read_file(const std::string &file_path, uint8_t **data, size_t *length);

/**
 * @brief 写入整个文件。
 *
 * @param file_path 文件路径。
 * @param data 文件内容。
 * @param length 文件长度。
 * @return 写入成功返回 true。
 */
bool write_file(const std::string &file_path, const uint8_t *data,
                size_t length);

/**
 * @brief 列出目录中的文件（按文件名排序）。
 *
 * @param directory_path 目录路径。
 * @param suffix 可选后缀过滤；为空时列出全部普通文件。
 * @param file_names 输出文件名列表。
 * @return 目录打开成功返回 true。
 */
bool list_files(const char *directory_path, const char *suffix,
                std::vector<std::string> *file_names);

/**
 * @brief 列出目录中的 JPEG 文件（按文件名排序）。
 *
 * @param directory_path 目录路径。
 * @param file_names 输出文件名列表。
 * @return 目录打开成功返回 true。
 */
bool list_jpeg_files(const char *directory_path,
                     std::vector<std::string> *file_names);

/**
 * @brief 清空目录中的文件。
 *
 * @param directory_path 目录路径。
 * @param suffix 可选后缀过滤；为空时删除全部普通文件。
 * @return 删除的文件数量。
 */
int clear_directory(const char *directory_path, const char *suffix);

/**
 * @brief 获取不含扩展名的文件名。
 *
 * @param file_name 文件名。
 * @return 去掉最后一个扩展名后的文件名。
 */
std::string file_stem(const std::string &file_name);

/**
 * @brief 判断字符串是否以指定后缀结尾（不区分大小写）。
 *
 * @param text 输入字符串。
 * @param suffix 后缀。
 * @return 匹配返回 true。
 */
bool ends_with_case_insensitive(const std::string &text, const char *suffix);
