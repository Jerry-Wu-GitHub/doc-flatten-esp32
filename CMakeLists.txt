cmake_minimum_required(VERSION 3.16)

# DocCorner ESP32-S3 项目（ESP-IDF v5.x）
#   idf.py set-target esp32s3
#   idf.py -p COM3 flash monitor

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(doc_corner_esp32s3)

# 把 flash_samples/ 下的图片打包成 SPIFFS 镜像，烧录到 storage 分区
spiffs_create_partition_image(storage "${CMAKE_CURRENT_SOURCE_DIR}/flash_samples" FLASH_IN_PROJECT)
