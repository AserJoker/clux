cmake_minimum_required(VERSION 3.14)
project(clux C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(third_party/icu)

add_executable(clux src/main.c)
target_link_libraries(clux PRIVATE
    ICU::i18n
    ICU::uc
    ICU::data
    ICU::io
    icu_data_init
)