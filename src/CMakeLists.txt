# Top-level ESP-IDF component for the IDF starter variant.
# Only compiles src/idf/, excluding the Arduino sources (main.cpp, starter.cpp).
idf_component_register(
    SRCS "idf/main.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver
)