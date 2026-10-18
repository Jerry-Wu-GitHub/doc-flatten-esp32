// file_storage.cpp - SPIFFS 文件读写与目录工具实现
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "file_storage.h"

namespace {

constexpr const char *kFileSystemMountPoint = "/spiffs";
constexpr const char *kFileSystemPartitionLabel = "storage";

}  // namespace

bool mount_file_system() {
    esp_vfs_spiffs_conf_t configuration = {};
    configuration.base_path = kFileSystemMountPoint;
    configuration.partition_label = kFileSystemPartitionLabel;
    configuration.max_files = 8;
    configuration.format_if_mount_failed = false;
    const esp_err_t error = esp_vfs_spiffs_register(&configuration);
    if (error != ESP_OK) {
        printf("[file_storage] SPIFFS mount failed: %d\n",
               static_cast<int>(error));
        return false;
    }
    return true;
}

void print_file_system_information() {
    size_t total_bytes = 0;
    size_t used_bytes = 0;
    const esp_err_t error =
        esp_spiffs_info(kFileSystemPartitionLabel, &total_bytes, &used_bytes);
    if (error == ESP_OK) {
        printf("[file_storage] SPIFFS total=%u used=%u free=%u bytes\n",
               static_cast<unsigned>(total_bytes),
               static_cast<unsigned>(used_bytes),
               static_cast<unsigned>(total_bytes - used_bytes));
    } else {
        printf("[file_storage] esp_spiffs_info failed: %d\n",
               static_cast<int>(error));
    }
}

void *allocate_psram_memory(size_t size) {
    void *pointer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!pointer) {
        pointer = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return pointer;
}

bool create_directory(const char *directory_path) {
    if (mkdir(directory_path, 0777) == 0) return true;
    // SPIFFS 对已存在目录不一定设置 errno == EEXIST，用 opendir 再确认一次。
    DIR *directory = opendir(directory_path);
    if (directory) {
        closedir(directory);
        return true;
    }
    printf("[file_storage] cannot create %s (errno=%d)\n", directory_path,
           errno);
    return false;
}

bool read_file(const std::string &file_path, uint8_t **data, size_t *length) {
    *data = nullptr;
    *length = 0;
    FILE *file = fopen(file_path.c_str(), "rb");
    if (!file) {
        printf("[file_storage] open failed: %s\n", file_path.c_str());
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    const long file_size = ftell(file);
    if (file_size <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    uint8_t *buffer = static_cast<uint8_t *>(
        allocate_psram_memory(static_cast<size_t>(file_size)));
    if (!buffer) {
        printf("[file_storage] read buffer allocation failed: %ld bytes\n",
               file_size);
        fclose(file);
        return false;
    }
    const size_t read_bytes = fread(buffer, 1, static_cast<size_t>(file_size),
                                    file);
    fclose(file);
    if (read_bytes != static_cast<size_t>(file_size)) {
        printf("[file_storage] short read: %s\n", file_path.c_str());
        heap_caps_free(buffer);
        return false;
    }
    *data = buffer;
    *length = read_bytes;
    return true;
}

bool write_file(const std::string &file_path, const uint8_t *data,
                size_t length) {
    FILE *file = fopen(file_path.c_str(), "wb");
    if (!file) {
        printf("[file_storage] write open failed: %s (errno=%d)\n",
               file_path.c_str(), errno);
        return false;
    }
    const size_t written_bytes = fwrite(data, 1, length, file);
    fclose(file);
    if (written_bytes != length) {
        printf("[file_storage] short write: %s\n", file_path.c_str());
        remove(file_path.c_str());
        return false;
    }
    return true;
}

std::string file_stem(const std::string &file_name) {
    const size_t dot_position = file_name.find_last_of('.');
    return (dot_position == std::string::npos) ? file_name
                                               : file_name.substr(0, dot_position);
}

bool ends_with_case_insensitive(const std::string &text, const char *suffix) {
    const size_t suffix_length = strlen(suffix);
    if (text.size() < suffix_length) return false;
    const size_t start_position = text.size() - suffix_length;
    for (size_t index = 0; index < suffix_length; ++index) {
        const unsigned char text_character =
            static_cast<unsigned char>(text[start_position + index]);
        const unsigned char suffix_character =
            static_cast<unsigned char>(suffix[index]);
        if (tolower(text_character) != tolower(suffix_character)) return false;
    }
    return true;
}

bool list_files(const char *directory_path, const char *suffix,
                std::vector<std::string> *file_names) {
    DIR *directory = opendir(directory_path);
    if (!directory) {
        printf("[file_storage] cannot open %s (errno=%d)\n", directory_path,
               errno);
        return false;
    }
    struct dirent *directory_entry = nullptr;
    while ((directory_entry = readdir(directory)) != nullptr) {
        const std::string file_name = directory_entry->d_name;
        if (file_name == "." || file_name == "..") continue;
        if (suffix && !ends_with_case_insensitive(file_name, suffix)) continue;
        const std::string file_path =
            std::string(directory_path) + "/" + file_name;
        struct stat file_information = {};
        if (stat(file_path.c_str(), &file_information) != 0 ||
            !S_ISREG(file_information.st_mode)) {
            continue;
        }
        file_names->push_back(file_name);
    }
    closedir(directory);
    std::sort(file_names->begin(), file_names->end());
    return true;
}

bool list_jpeg_files(const char *directory_path,
                     std::vector<std::string> *file_names) {
    return list_files(directory_path, ".jpg", file_names);
}

int clear_directory(const char *directory_path, const char *suffix) {
    std::vector<std::string> file_names;
    if (!list_files(directory_path, suffix, &file_names)) return 0;
    int removed_count = 0;
    for (const std::string &file_name : file_names) {
        const std::string file_path =
            std::string(directory_path) + "/" + file_name;
        if (remove(file_path.c_str()) == 0) ++removed_count;
    }
    return removed_count;
}
