# third_party ディレクトリについて

third_party ディレクトリ以下には、外部から取得したコードが含まれています。
そのため、コードフォーマッタは適用しないようにしてください。

## third_party/NvCodec について

`third_party/NvCodec` は [Video Codec SDK Archive](https://developer.nvidia.com/video-codec-sdk-archive) から取得したものを使用しています。
以下のファイルに関しては、Sora C++ SDK のための修正を適用しています。

- Utils/NvCodecUtils.h
- NvCodec/NvDecoder/NvDecoder.h
- NvCodec/NvDecoder/NvDecoder.cpp
- NvCodec/NvEncoder/NvEncoderCuda.h
- NvCodec/NvEncoder/NvEncoderCuda.cpp
- NvCodec/NvEncoder/NvEncoder.h
- NvCodec/NvEncoder/NvEncoder.cpp
