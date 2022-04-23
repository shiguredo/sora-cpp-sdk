cmake_minimum_required(VERSION 3.16)

# Only interpret if() arguments as variables or keywords when unquoted.
cmake_policy(SET CMP0054 NEW)
# MSVC runtime library flags are selected by an abstraction.
cmake_policy(SET CMP0091 NEW)

set(WEBRTC_INCLUDE_DIR "" CACHE PATH "WebRTC のインクルードディレクトリ")
set(WEBRTC_LIBRARY_DIR "" CACHE PATH "WebRTC のライブラリディレクトリ")
set(WEBRTC_LIBRARY_NAME "webrtc" CACHE STRING "WebRTC のライブラリ名")
set(USE_LIBCXX OFF CACHE BOOL "libstdc++ の代わりに libc++ を使うかどうか")
set(LIBCXX_INCLUDE_DIR "" CACHE PATH "libc++ を使う場合の libc++ のインクルードディレクトリ\n空文字だった場合はデフォルト検索パスの libc++ を利用する")
set(USE_NVCODEC_ENCODER OFF CACHE BOOL "NVIDIA Video Codec SDK によるハードウェアエンコーダを利用するかどうか")

project(sora-cpp-sdk C CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(Boost_USE_STATIC_LIBS ON)
if (WIN32)
  set(Boost_USE_STATIC_RUNTIME ON)
endif()

find_package(WebRTC REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem json date_time system regex)
find_package(Threads REQUIRED)

# libsora.a を作る

add_library(sora STATIC)
set_target_properties(sora PROPERTIES CXX_STANDARD 17 C_STANDARD 17)

string(SUBSTRING "${SORA_CPP_SDK_COMMIT}" 0 8 SORA_CPP_SDK_COMMIT_SHORT)
string(SUBSTRING "${WEBRTC_COMMIT}" 0 8 WEBRTC_COMMIT_SHORT)
configure_file(src/version.gen.h.template ${CMAKE_CURRENT_BINARY_DIR}/version.gen.h)
target_include_directories(sora PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_sources(sora
  PRIVATE
    src/data_channel.cpp
    src/device_video_capturer.cpp
    src/rtc_ssl_verifier.cpp
    src/rtc_stats.cpp
    src/scalable_track_source.cpp
    src/session_description.cpp
    src/sora_signaling.cpp
    src/ssl_verifier.cpp
    src/url_parts.cpp
    src/version.cpp
    src/websocket.cpp
    src/zlib_helper.cpp
)

target_link_libraries(sora
  PUBLIC
    WebRTC::WebRTC
    Boost::filesystem
    Boost::system
    Boost::json
)

if (USE_LIBCXX)
  target_compile_options(sora
    PUBLIC
      "$<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<BOOL:LIBCXX_INCLUDE_DIR>>:-isystem${LIBCXX_INCLUDE_DIR}>"
  )
endif()

target_include_directories(sora
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(sora
  PRIVATE
    USE_NVCODEC_ENCODER=$<BOOL:${USE_NVCODEC_ENCODER}>
)

if (WIN32)
  # 文字コードを utf-8 として扱うのと、シンボルテーブル数を増やす
  target_compile_options(sora PRIVATE /utf-8 /bigobj)
  set_target_properties(sora
    PROPERTIES
      # CRTライブラリを静的リンクさせる
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )

  target_link_libraries(sora
    PUBLIC
  #    dbghelp.lib
  #    delayimp.lib
  #    dnsapi.lib
  #    msimg32.lib
  #    oleaut32.lib
  #    psapi.lib
  #    shell32.lib
  #    shlwapi.lib
  #    usp10.lib
  #    version.lib
  #    wininet.lib
      winmm.lib
      ws2_32.lib
  #    amstrmid.lib
      Strmiids.lib
  #    crypt32.lib
      dmoguids.lib
  #    iphlpapi.lib
      msdmo.lib
      Secur32.lib
      wmcodecdspuuid.lib
  )

  target_compile_definitions(sora
    PUBLIC
      _CONSOLE
      _WIN32_WINNT=0x0A00
      WEBRTC_WIN
      NOMINMAX
      WIN32_LEAN_AND_MEAN
      UNICODE
      _UNICODE
      _HAS_ITERATOR_DEBUGGING=0
  )

  if (USE_NVCODEC_ENCODER)
    target_sources(sora
      PRIVATE
        src/hwenc_nvcodec/nvcodec_h264_encoder.cpp
        NvCodec/NvCodec/NvEncoder/NvEncoder.cpp
        NvCodec/NvCodec/NvEncoder/NvEncoderD3D11.cpp
        src/hwenc_nvcodec/nvcodec_video_decoder.cpp)
    target_include_directories(sora
      PRIVATE
        NvCodec/include
        NvCodec/NvCodec)
    target_link_libraries(sora
      PRIVATE
        DXGI.lib
        D3D11.lib
    )

    # enable_language(CUDA) は CUDA 用 Visual Studio Integration を利用して CUDA を検出しているが、
    # CUDA ドライバがインストールできない場所では VS Integration をインストールできないため、
    # enable_language(CUDA) は利用できない。
    # なので（deprecated だけど）FindCUDA を利用してコンパイルする。

    find_package(CUDA REQUIRED)

    set_source_files_properties(
        src/cuda/cuda_context_cuda.cpp
        NvCodec/NvCodec/NvDecoder/NvDecoder.cpp
        src/hwenc_nvcodec/nvcodec_decoder_cuda.cpp
      PROPERTIES
        CUDA_SOURCE_PROPERTY_FORMAT OBJ
    )
    cuda_compile(CUDA_FILES
        src/cuda/cuda_context_cuda.cpp
        NvCodec/NvCodec/NvDecoder/NvDecoder.cpp
        src/hwenc_nvcodec/nvcodec_decoder_cuda.cpp
      OPTIONS
        -Xcompiler /utf-8
        -Xcompiler /I${CMAKE_CURRENT_SOURCE_DIR}/NvCodec/include
        -Xcompiler /I${CMAKE_CURRENT_SOURCE_DIR}/NvCodec/NvCodec
        -Xcompiler /I${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_sources(sora PRIVATE ${CUDA_FILES})
    target_include_directories(sora PRIVATE ${CUDA_INCLUDE_DIRS})
    target_link_libraries(sora
      PRIVATE
        ${CUDA_TOOLKIT_ROOT_DIR}/lib/x64/cudart_static.lib 
        #${CUDA_LIBRARIES}
    )
  endif()
elseif (APPLE)

  target_compile_options(sora PRIVATE -fconstant-string-class=NSConstantString -mmacosx-version-min=11.0)
  target_link_options(sora PRIVATE -ObjC)
  set_target_properties(sora PROPERTIES CXX_VISIBILITY_PRESET hidden)

  target_compile_definitions(sora
    PUBLIC
      WEBRTC_POSIX
      WEBRTC_MAC
  )

  #target_link_libraries(sora
  #  PRIVATE
  #    "-framework Foundation"
  #    "-framework AVFoundation"
  #    "-framework CoreServices"
  #    "-framework CoreFoundation"
  #    "-framework AudioUnit"
  #    "-framework AudioToolbox"
  #    "-framework CoreAudio"
  #    "-framework CoreGraphics"
  #    "-framework CoreMedia"
  #    "-framework CoreVideo"
  #    "-framework VideoToolbox"
  #    "-framework AppKit"
  #    "-framework Metal"
  #    "-framework MetalKit"
  #    "-framework OpenGL"
  #)

elseif (UNIX)
  target_compile_definitions(sora
    PUBLIC
      WEBRTC_POSIX
  )
  # Linux 系の場合はカスタムされた libc++ を使っているためオプション追加
  target_compile_definitions(sora PUBLIC _LIBCPP_ABI_UNSTABLE _LIBCPP_DISABLE_AVAILABILITY)

  set_target_properties(sora PROPERTIES POSITION_INDEPENDENT_CODE ON)
  #target_link_libraries(sora
  #  PRIVATE
  #    X11
  #    Xau
  #    Xdmcp
  #    Xtst
  #    xcb
  #    plds4
  #    Xext
  #    expat
  #    dl
  #    nss3
  #    nssutil3
  #    plc4
  #    nspr4
  #    rt
  #    Threads::Threads
  #)

  if (USE_NVCODEC_ENCODER)
    target_sources(sora
      PRIVATE
        src/cuda/cuda_context_cuda.cpp
        src/hwenc_nvcodec/nvcodec_h264_encoder.cpp
        src/hwenc_nvcodec/nvcodec_v4l2_capturer.cpp
        src/hwenc_nvcodec/nvcodec_h264_encoder_cuda.cpp
        src/hwenc_nvcodec/nvcodec_decoder_cuda.cpp
        src/hwenc_nvcodec/nvcodec_video_decoder.cpp
        NvCodec/NvCodec/NvDecoder/NvDecoder.cpp
        NvCodec/NvCodec/NvEncoder/NvEncoder.cpp
        NvCodec/NvCodec/NvEncoder/NvEncoderCuda.cpp)
    target_include_directories(sora
      PRIVATE
        NvCodec/include
        NvCodec/NvCodec
        /usr/local/cuda/include)

    # これらのソースは CUDA としてコンパイルする
    set_source_files_properties(
        src/cuda/cuda_context_cuda.cpp
        src/hwenc_nvcodec/nvcodec_h264_encoder_cuda.cpp
        src/hwenc_nvcodec/nvcodec_decoder_cuda.cpp
        NvCodec/NvCodec/NvDecoder/NvDecoder.cpp
        NvCodec/NvCodec/NvEncoder/NvEncoderCuda.cpp
      PROPERTIES
        COMPILE_OPTIONS "-xcuda;--cuda-gpu-arch=sm_35;-std=gnu++14"
    )

    target_link_directories(sora PRIVATE /usr/local/cuda/lib64)
    target_link_libraries(sora PRIVATE cudart_static dl rt)
  endif()
endif()




# テスト用バイナリを作る
enable_testing()
add_executable(test_hello test/hello.cpp)
set_target_properties(test_hello PROPERTIES CXX_STANDARD 17 C_STANDARD 17)
set_target_properties(test_hello PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_compile_options(test_hello PRIVATE /utf-8 /bigobj)
target_include_directories(test_hello PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_hello PRIVATE sora)
SET(CTEST_OUTPUT_ON_FAILURE ON)
add_test(
  NAME test_hello
  COMMAND $<TARGET_FILE:test_hello> ${CMAKE_CURRENT_SOURCE_DIR}/test/.testparam.json
)
# set_tests_properties(test_hello PROPERTIES TIMEOUT 3) 