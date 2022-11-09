cmake_minimum_required(VERSION 3.23)

# Only interpret if() arguments as variables or keywords when unquoted.
cmake_policy(SET CMP0054 NEW)
# MSVC runtime library flags are selected by an abstraction.
cmake_policy(SET CMP0091 NEW)

set(WEBRTC_INCLUDE_DIR "" CACHE PATH "WebRTC のインクルードディレクトリ")
set(WEBRTC_LIBRARY_DIR "" CACHE PATH "WebRTC のライブラリディレクトリ")
set(WEBRTC_LIBRARY_NAME "webrtc" CACHE STRING "WebRTC のライブラリ名")
set(BOOST_ROOT "" CACHE PATH "Boost のルートディレクトリ")
set(SORA_DIR "" CACHE PATH "Sora のルートディレクトリ")
set(CLI11_DIR "" CACHE PATH "CLI11 のルートディレクトリ")

project(sora-sdl-sample C CXX)

list(APPEND CMAKE_PREFIX_PATH ${SORA_DIR})
list(APPEND CMAKE_MODULE_PATH ${SORA_DIR}/share/cmake)

set(Boost_USE_STATIC_LIBS ON)

find_package(Boost REQUIRED COMPONENTS json filesystem)
find_package(WebRTC REQUIRED)
find_package(Sora REQUIRED)
find_package(Threads REQUIRED)

add_executable(messaging_recvonly_sample)
set_target_properties(messaging_recvonly_sample PROPERTIES CXX_STANDARD 17 C_STANDARD 17)
set_target_properties(messaging_recvonly_sample PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_sources(messaging_recvonly_sample PRIVATE ../src/messaging_recvonly_sample.cpp)

target_compile_options(messaging_recvonly_sample
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>"
    "$<$<COMPILE_LANGUAGE:CXX>:-isystem${LIBCXX_INCLUDE_DIR}>"
)
target_include_directories(messaging_recvonly_sample PRIVATE ${CLI11_DIR}/include)
target_link_libraries(messaging_recvonly_sample PRIVATE Sora::sora)
target_link_directories(messaging_recvonly_sample PRIVATE ${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/tegra)
target_compile_definitions(messaging_recvonly_sample PRIVATE CLI11_HAS_FILESYSTEM=0)
