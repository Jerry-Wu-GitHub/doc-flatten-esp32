idf_component_register(
    SRCS
        "main.cpp"
        "demo_runner.cpp"
        "document_corner.cpp"
        "document_flatten.cpp"
        "document_geometry.cpp"
        "document_pipeline.cpp"
        "file_storage.cpp"
        "image_codec.cpp"
        "image_preprocessor.cpp"
        "serial_transfer.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp-tflite-micro esp_new_jpeg spiffs esp_timer
    EMBED_FILES "doc_corner_int8.tflite"
)
