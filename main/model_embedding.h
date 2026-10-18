// model_embedding.h - 通过 ESP-IDF EMBED_FILES 嵌入的模型符号
#pragma once

#include <cstdint>

extern const uint8_t document_corner_model_start[]
    asm("_binary_doc_corner_int8_tflite_start");
extern const uint8_t document_corner_model_end[]
    asm("_binary_doc_corner_int8_tflite_end");
