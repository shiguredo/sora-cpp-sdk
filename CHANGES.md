# 変更履歴

- CHANGE
  - 下位互換のない変更
- UPDATE
  - 下位互換がある変更
- ADD
  - 下位互換がある追加
- FIX
  - バグ修正

## develop

- [UPDATE] `third_party/NvCodec` を 12.1 にアップデート
  - README のライセンスバージョンを更新
  - NOTICE の `cuviddec.h / nvEncodeAPI.h / nvcuvid.h` を更新
  - `nvcodec_video_encoder.cpp` でコメントアウトしていた箇所を削除
    - すでに利用していないことと `NV_ENC_PARAMS_RC_CBR_LOWDELAY_HQ` が削除されたため
  - @torikizi

### misc

- [UPDATE] `third_party` の運用方針を見直し
  - `third_party/` は外部から取得したコードであり、アップデート時に変更の追従が困難になるためフォーマッタを今回から適用しない
  - `third_party/NvCodec/NvCodec/` に配置していた `.clang-format` を `third_party` の直下に移動
    - clang-format を適用しない対象を `third_party` 以下全体にするため
  - clang-fomat 13.0 以降で `SortIncludes: false` は不要となったため設定を削除
    - 参考: [clang-format still formatting with `DisableFormat: true`](https://stackoverflow.com/questions/55833838/clang-format-still-formatting-with-disableformat-true/55833839#55833839)
  - `third_party` に README を追加
    - `third_party` の運用方針を追加
  - @torikizi
- [UPDATE] `third_party/NvCodec` のコードをフォーマッタを適用しない状態に戻す
  - @torikizi

## 2025.2.0

**リリース日**: 2025-03-18

- [CHANGE] `SoraClientContextConfig` から `use_hardware_encoder` を削除
  - これにより、デフォルトで利用するエンコーダ/デコーダは全て libwebrtc の実装のものになります
  - ハードウェアエンコーダを有効にしたい場合は `SoraClientContextConfig::video_codec_factory_config` を適切に設定して下さい
  - @melpon
- [CHANGE] `SoraClientContextConfig` から `force_i420_conversion_for_simulcast_adapter` を削除
  - 代わりに `SoraClientContextConfig::video_codec_factory_config.encoder_factory_config.force_i420_conversion` を利用して下さい
  - @melpon
- [CHANGE] `SoraClientContextConfig` から `openh264` を削除
  - 代わりに `SoraClientContextConfig::video_codec_factory_config.::capability_config.openh264_path` を利用して下さい
  - @melpon
- [CHANGE] `sora/hwenc_vpl/vpl_session.h` ファイルを `sora/vpl_session.h` に移動
  - @melpon
- [CHANGE] `SoraVideoEncoderFactoryConfig` の `use_simulcast_adapter` を削除
  - サイマルキャストアダプタは常に有効になります
  - @melpon
- [CHANGE] `SoraVideoEncoderFactoryConfig` の `force_i420_conversion_for_simulcast_adapter` を `force_i420_conversion` に変更
  - @melpon
- [CHANGE] GitHub Actions で macOS 向けビルドで Xcode のバージョンを指定したのを削除する  
  - libwebrtc の制約で Xcode のバージョンを指定していたが、 m132.6834.5.5 の時点では制約がなくなり、指定しなくてもビルドできるようになったため
  - @torikizi
- [CHANGE] Ubuntu 20.04 x86_64 の対応を削除
  - @melpon
- [UPDATE] CMake を 3.31.6 にあげる
  - @voluntas @torikizi
- [UPDATE] libwebrtc を m132.6834.5.8 にあげる
  - @melpon
- [UPDATE] OpenH264 を v2.6.0 に上げる
  - @torikizi
- [UPDATE] VPL_VERSION を 2.14.0 に上げる
  - @torikizi
- [ADD] 有効なエンコーダの一覧を取得する `GetVideoCodecCapability()` 関数を追加
  - @melpon
- [ADD] 利用するエンコーダ/デコーダの実装を細かく指定するためのクラス `VideoCodecPreference` を追加
  - `SoraClientContextConfig::video_codec_factory_config.preference` 経由で利用できます
  - @melpon
- [ADD] OpenH264 デコーダに対応する
  - @melpon
- [ADD] タグが打たれた場合に sumomo バイナリを Release に追加する
  - Release 用の sumomo は C++ SDK のリリースバイナリを使用してビルドする  
  - リアルタイムメッセージング以外の機能がほぼ全て含まれている sumomo をリリース時に含めるようにする
  - @torikizi
- [ADD] NVIDIA Video Codec SDK を AV1 エンコーダー/デコーダーに対応する
  - @melpon
- [ADD] `CudaContext::CanCreate()` を追加
  - @melpon
- [ADD] AMD の AMF に対応する
  - @melpon
- [FIX] `NvCodecVideoDecoder` の `ImplementationName` を `NvCodec` に修正する
  - NvCodecVideoEncoder の `ImplementationName` は `NvCodec` になっているので、合わせる
  - @torikizi

### misc

- [FIX] GitHub Actions の Windows でのビルドが失敗する問題を修正
  - Microsoft Visual C++ のバージョン `14.42.34438` 以降では `<chrono>` ヘッダの明示的なインクルードが必要となったため、ビルドエラーが発生していた
  - `<chrono>` をインクルードするように修正
  - @torikizi
- [FIX] test/e2e.cpp の利用していない include を削除する
  - @torikizi

## 2025.1.0

**リリース日**: 2025-01-27

- [CHANGE] `boost::optional` を利用していた部分を全て `std::optional` に変更
  - @melpon
- [CHANGE] SRTP keying material を取得する機能を削除
  - @melpon
- [UPDATE] VPL を v2.13.0 に上げる
  - @voluntas
- [UPDATE] CMake を 3.30.5 にあげる
  - @voluntas
- [UPDATE] Boost を 1.87.0 にあげる
  - boost::asio::deadline_timer が間接的にインクルードされなくなったので、websocket.h に boost/asio/deadline_timer.hpp のインクルードを追加して利用可能にする
  - boost::json::error_code が削除されたので boost::system::error_code に置き換える
  - @voluntas @torikizi @melpon
- [UPDATE] libwebrtc を m132.6834.5.2 にあげる
  - H.265 Patch の修正に伴い、C++ SDK の H.265 に関する設定を変更
  - examples と test に `rtc::CreateRandomString` のヘッダを追加
  - `SetRtpTimestamp` を `frame.timestamp` から `frame.rtp_timestamp` に変更
  - `scalable_track_source.h` と `scalable_track_source.cpp` の `absl::optional` を `std::optional` に変更
  - `nvcodec_video_decoder.h` と `vpl_video_decoder.h` に `#include <optional>` を追加
  - `nvcodec_video_decoder.cpp` と `vpl_video_decoder.cpp` の `absl::nullopt` を `std::nullopt` に変更
  - Android の test アプリの cmake バージョンを VERSION と合わせる
  - `requested_resolution` が `scale_resolution_down_to` に変更されたのでリネーム
  - `ExportKeyingMaterial` が廃止されたため `ExportSrtpKeyingMaterial` に変更
    - `ExportKeyingMaterial` は引数を 6 つ利用できたが `ExportSrtpKeyingMaterial` では 1 つしか利用できないため互換処理を追加
  - @tnoho @torikizi @melpon
- [UPDATE] Xcode のバージョンを 15.4 にあげる
  - @tnoho
- [UPDATE] SDL を 2.30.11 に上げる
  - @torikizi
- [UPDATE] BLEND2D_VERSION を上げる
  - @torikizi
- [UPDATE] ASMJIT_VERSION を上げる
  - @torikizi
- [UPDATE] OpenH264 を v2.5.0 に上げる
  - @voluntas
- [ADD] シグナリングメッセージを取得できるよう OnSignalingMessage を SoraSignalingObserver に追加する
  - @tnoho
- [ADD] Intel VPL で AV1 エンコーダを動くようにする
  - @tnoho
- [ADD] ルート証明書を指定可能にする
  - @melpon
- [ADD] Ubuntu 24.04 armv8 に対応する
  - @melpon
- [ADD] WebSocket の Close を取得できるよう SendOnWsClose を SoraSignalingObserver に追加する
  - @tnoho
- [ADD] DataChannel のみの接続で type: close がやってきた場合に正しく切断されるようにする
  - @melpon
- [ADD] SoraSignalingConfig に audio_opus_params を追加
  - @melpon
- [ADD] SoraSignalingConfig::DataChennel に header を追加
  - @melpon
- [ADD] SoraSignalingConfig::ForwardingFilter に name と priority を追加
  - @melpon
- [ADD] SoraSignalingConfig に forwarding_filters を追加
  - @melpon
- [ADD] scaleResolutionDownTo に対応する
  - @melpon
- [ADD] SoraClientContext にオーディオデバイスの設定するオプションを追加
  - @melpon
- [ADD] sumomo にビデオデバイスとオーディオデバイスを設定するオプションを追加
  - @melpon
- [ADD] SoraSignalingConfig に degradation_preference を追加
  - @melpon
- [ADD] sumomo と test/hello に degradation_preference を設定するオプションを追加
  - @melpon
- [FIX] HTTP Proxy 利用時の Websocket 初期化で insecure_ メンバ変数が初期化されていなかったのを修正
  - @melpon
- [FIX] SoraSignalingConfig の client_cert と client_key に渡す必要がある値を、ファイルパスからファイルの内容に修正
  - Android の場合、jar に纏められたファイルからファイルパスが取得できないため
  - @melpon
- [FIX] SoraSignalingConfig の client_cert と client_key の型を `std::string` から `std::optional<std::string>` に修正
  - @melpon
- [FIX] WS と DC が両方繋がっている時に切断した時、正常終了にも関わらずエラーが発生することがあるのを修正
  - @melpon
- [FIX] Sora から切断された場合に WS reason が 1000 だったら正常終了とする
  - @melpon
- [FIX] libdrm-dev と libva-dev の依存が不要になってたので削除
  - @melpon

### misc

- [CHANGE] GitHub Actions の ubuntu-latest を ubuntu-24.04 に変更
  - @voluntas
- [CHANGE] GitHub Actions の build に cron を設定する
  - @voluntas
- [UPDATE] Boost のダウンロード URL を変更する
  - @voluntas
- [UPDATE] CATCH2 を v3.7.1 にあげる
  - @voluntas
- [UPDATE] SDL2 を 2.30.7 にあげる
  - @voluntas
- [ADD] sumomo に証明書に関するオプションを追加する
  - サーバー証明書の検証を行わないようにするオプション `--insecure` を追加
  - CA 証明書を指定するオプション `--ca-cert` を追加
    - 指定できるファイルは PEM 形式
  - クライアント認証に関するオプション `--client-cert`, `--client-key` を追加
    - 指定できるファイルは PEM 形式
  - @melpon
- [ADD] Ubuntu 24.04 armv8 向けの example を追加する
  - @melpon
- [ADD] VERSION と examples/VERSION のバージョンをチェックする仕組みを追加
  - @melpon
- [FIX] examples のビルド時に libgl-dev がない環境で SDL の画面作成が失敗する問題を解消するために、build.yml を修正して libgl-dev のインストールを追加する
  - SDL のビルド時に libgl-dev がない環境では、SDL の OpenGL 機能が有効化されず、examples アプリ起動時に SDL の画面作成が失敗する
  - libgl-dev をインストールすることで OpenGL 機能が有効化され、examples アプリ起動時に SDL の画面作成が成功するようになる
  - 参考リンク : [SDL の OpenGL をチェックしている場所](https://github.com/libsdl-org/SDL/blob/2c7b7d1d33748b6c27eaf57cc5d96ce6c4c64a87/cmake/sdlchecks.cmake#L722-L733)
  - @torikizi

## 2024.7.0

**リリース日**: 2024-07-29

- [CHANGE] `--sora-dir`, `--sora-args` を `--local-sora-cpp-sdk-dir` と `--local-sora-cpp-sdk-args` に変更する
  - @melpon
- [CHANGE] `--webrtc-build-dir`, `--webrtc-build-args` を `--local-webrtc-build-dir` と `--local-webrtc-build-args` に変更する
  - @melpon
- [CHANGE] SoraVideoDecoderFactory の CreateVideoDecoder 関数を Create に変更する
  - libwebrtc に定義されている継承元クラスが変更されたことに対する追従
  - @enm10k
- [CHANGE] SoraVideoEncoderFactory の CreateVideoEncoder 関数を Create に変更する
  - libwebrtc に定義されている継承元クラスが変更されたことに対する追従
  - @enm10k
- [CHANGE] `CreateOpenH264VideoEncoder()` 関数の第１引数の型を `const cricket::VideoCodec&` から `const webrtc::SdpVideoFormat&` に変更する
  - @melpon
- [CHANGE] Jetson 対応をサポートブランチに移動する
  - @melpon
- [UPDATE] Boost を 1.85.0 にあげる
  - @enm10k
- [UPDATE] SDL2 を 2.30.5 にあげる
  - @enm10k
- [UPDATE] CMake を 3.29.6 にあげる
  - @voluntas
- [UPDATE] CLI11 を 2.4.2 にあげる
  - @enm10k
- [UPDATE] libwebrtc を m127.6533.1.1 にあげる
  - Android の test アプリがリンクできなくなったため、リンカーを Android NDK のものから libwebrtc のものに変更
  - リンカーのバージョンの違いによる互換性の問題でエラーが発生していた
  - @melpon @enm10k @torikizi
- [ADD] sumomo に `--openh264` と `--use-hardware-encoder` オプションを追加
  - @melpon
- [ADD] sumomo に `--video-h264-params` と `--video-h265-params` オプションを追加
  - @enm10k
- [ADD] SoraSignalingConfig に `video_h265_params` を追加
  - @enm10k
- [ADD] 古い Intel CPU でも H265 エンコーダが動くようにする
  - @melpon
- [ADD] Intel VPL で AV1 デコーダを動くようにする
  - @melpon
- [ADD] NVIDIA Video Codec SDK を H265 に対応する
  - @melpon
- [ADD] Ubuntu 24.04 に対応する
  - @melpon
- [ADD] WebSocket での接続時に User-Agent ヘッダーを追加する
  - @melpon

## 2024.6.1

**リリース日**: 2024-04-16

- [CHANGE] テストモジュールについて `SoraSignalingConfig` の `sora_client` に値を設定しないようにする
  - @enm10k
- [FIX] VPL デコーダで遅延が起きたりセグフォすることがあるのを修正
  - @melpon

## 2024.6.0

**リリース日**: 2024-04-01

- [CHANGE] `VplVideoDecoderImpl` の `ImplementationName` を `oneVPL` から `libvpl` に変更する
  - @enm10k
- [FIX] `VplVideoEncoderImpl` の `implementation_name` の値が誤っていたため `libvpl` に修正する
  - @enm10k

## 2024.5.0

**リリース日**: 2024-03-29

- [CHANGE] 別リポジトリに分かれていた Sora C++ SDK のサンプル集を examples/ 以下のディレクトリに統合する
  - @melpon
- [UPDATE] libwebrtc を `m122.6261.1.0` にあげる
  - Ubuntu のビルドを通すために、 __assertion_handler というファイルをコピーする必要があった
  - @miosakuma @enm10k
- [UPDATE] libvpl を v2.10.1 にあげる
  - @enm10k
- [ADD] OpenH264 エンコーダを追加
  - @melpon
- [ADD] Catch2 を使って E2E テストを追加
  - @melpon
- [ADD] Ubuntu 22.04 で Intel VPL のH.265 に対応する
  - @enm10k
- [FIX] HWA 利用の判定を `#if defined(USE_*_ENCODER)` という使い方で統一するように修正
  - @melpon

## 2024.4.0

**リリース日**: 2024-03-13

- [ADD] test/hello.cpp に video, audio フラグを追加
  - @melpon
- [FIX] Android ハンズフリー機能において Android 11 以前で Bluetooth SCO が切れてしまう問題を改善
  - @tnoho

## 2024.3.1 (2024-03-07)

- [FIX] Sora C++ SDK を利用してビルドする時に自動的に _LIBCPP_HARDENING_MODE が定義されるように修正
  - @melpon

## 2024.3.0 (2024-03-07)

- [CHANGE] Lyra を Sora C++ SDK から外し、Lyra に関連するファイルや関数、オプションを除ける
  - SoraSignalingConfig::audio_codec_lyra_bitrate オプションを削除
  - SoraSignalingConfig::audio_codec_lyra_usedtx オプションを削除
  - SoraSignalingConfig::check_lyra_version オプションを削除
  - audio_encoder_lyra.{h,cpp} を削除し、AudioEncoderLyra クラスを削除
  - audio_decoder_lyra.{h,cpp} を削除し、AudioDecoderLyra クラスを削除
  - sora_audio_encoder_factory.{h,cpp} を削除し、CreateBuiltinAudioEncoderFactory 関数を削除
  - sora_audio_decoder_factory.{h,cpp} を削除し、CreateBuiltinAudioDecoderFactory 関数を削除
  - Version クラスから GetLyraCompatibleVersion 関数を削除
  - enum class SoraSignalingErrorCode から LYRA_VERSION_INCOMPATIBLE を削除
  - VERSION ファイルから LYRA_VERSION, LYRA_COMPATIBLE_VERSION を削除
  - リリースパッケージから `lyra-1.3.2_sora-cpp-sdk-2024.2.0_android.tar.gz` などの Lyra パッケージを生成しないようにする
  - インストールする内容から `share/cmake/FindLyra.cmake` を削除
  - run.py を実行する時のオプションから `--no-lyra` オプションを削除
  - test/hello 実行時に指定する json フォーマットのオプション mode: lyra を削除し、mode オプションそのものも削除
  - @melpon
- [CHANGE] ビルド時に Bazel のインストールを行わないようにする
  - Lyra のために Bazel を利用していたので、関連して削除となる
  - @melpon
- [ADD] Android 向けに音声出力先変更機能として `SoraAudioManager` を追加する
  - Android では C++ を経由した OS の API 利用が煩雑となるため、Java で実装し、Sora.aar をビルドして提供を行う
  - Sora.aar ファイルは Android のパッケージに含める
  - iOS 向けとは異なりインスタンス生成が必要
    - API レベル 31 でオーディオデバイスの切り替えや Bluetooth ヘッドセットのスイッチングの API が変更となり、API レベルに応じて処理を切り替える必要があったため
  - @tnoho

## 2024.2.0 (2024-03-04)

- [CHANGE] `--webrtcbuild`, `--webrtc-fetch` などの webrtc ローカルビルドに関するフラグを削除し、代わりに `--webrtc-build-dir` と `--webrtc-build-args` を追加する
  - これにより、既存の webrtc-build ディレクトリを使ってローカルビルドを行うことができるようになる
  - @melpon
- [CHANGE] `SoraClientContextConfig` から `configure_media_dependencies` を削除した
  - libwebrtc から cricket::MediaEngineDependencies が削除されたため
  - @enm10k
- [UPDATE] libwebrtc を `m121.6167.3.0` にあげる
  - @torikizi @enm10k
- [UPDATE] Boost を 1.84.0 にあげる
  - @enm10k
- [UPDATE] CMake を 3.28.1 にあげる
  - @enm10k
- [UPDATE] CUDA を 11.8 にあげる
  - 更新時に発生したビルド・エラーを回避するために `include/sora/fix_cuda_noinline_macro_error.h` を追加した
  - @enm10k
- [UPDATE] Lyra を 1.3.2 にあげる
  - @melpon
- [UPDATE] Github Actions の setup-android と setup-msbuild のバージョンをアップデート
  - Node.js 16 の Deprecated に伴う対応
  - setup-android を v3 にアップデート
  - setup-android のアップデートに伴い ANDROID_SDK_CMDLINE_TOOLS_VERSION のバージョンを `9862592` にアップデート
    - `9862592` は CMDLINE_TOOLS のバージョン 10 に相当
    - 最新の 11 を指定するとエラーが出ることを懸念し、今回は 10 を指定
    - バージョンの組み合わせは [setup-android の README を参照](https://github.com/android-actions/setup-android)
  - build.yml に Setup JDK 17 を追加
    - JDK のバージョンを指定しない場合、デフォルトで JDK 11 を使用するため、JDK 17 を指定する
    - JDK 11 は setup-android のアップデートの影響で利用できなくなるため
    - JDK 17 を選択する理由は setup-android の README に記載されているバージョンの組み合わせに合わせるため
  - setup-msbuild を v2 にアップデート
    - node 20 に対応したバージョンを指定するため、最新の v2 を指定
    - リリースノートを参照すると、v2 は node 20 に対応していることがわかる [setup-msbuild のリリースノート](https://github.com/microsoft/setup-msbuild/releases/tag/v2)
  - @torikizi
- [ADD] Python コードのフォーマッターに Ruff を使うようにする
  - @voluntas
- [ADD] `SoraClientContextConfig`, `SoraVideoEncoderFactoryConfig` に `force_i420_conversion_for_simulcast_adapter` を追加する
  - use_simulcast_adapter = true の際に、エンコーダー内でビデオ・フレームのバッファーを I420 に変換しているが、この変換の有無をフラグで制御可能にした
  - force_i420_conversion_for_simulcast_adapter のデフォルト値は true で I420 への変換を行う
  - 変換を行わない場合、エンコードの性能が向上するが、バッファーの実装によってはサイマルキャストが利用できなくなる
  - @enm10k
- [ADD] `SoraSignalingObserver` に `OnSwitched` を追加する
  - @enm10k
- [FIX] Jetson Orin で AV1 を送信中、他のユーザーが後から接続して受信した時に映像が出ない問題を修正
  - @melpon @enm10k
- [FIX] test/android のアプリが実行時にエラーで落ちてしまう問題を修正
  - "AttachCurrentThread() must be called on this thread." というメッセージでエラーとなっていた
  - JVM::Initialize の前に AttachCurrentThreadIfNeeded 呼ぶ必要があった
  - @melpon

## 2024.1.0 (2024-01-16)

- [CHANGE] JetPack 5.1.2 に対応
  - JetPack 5.1.1, 5.1.2 で動作を確認
  - JetPack 5.1 では、互換性の問題で JetsonJpegDecoder がエラーになることを確認
  - @enm10k
- [ADD] ForwardingFilter の項目に version と metadata を追加
  - version と metadata はオプション項目として追加し、値がない場合は項目を設定しない
  - @torikizi
- [FIX] DataChannel シグナリングが有効な場合、一部のシグナリングメッセージのコールバックが上がらないことがあるのを修正
  - @melpon
- [FIX] ForwardingFilter の action 項目は本来オプション項目だったが、 action は `std::optional` 型になっていなかったため修正
  - @torikizi

## 2023.17.0 (2023-12-25)

- [UPDATE] libwebrtc を `m120.6099.1.2` に上げる
  - `m120.6099.1.1` より x86 シミュレータビルドがなくなったため、CI で ios の test ビルドを行わなくした
  - @melpon @enm10k @torikizi @miosakuma

## 2023.16.1 (2023-12-02)

- [FIX] libwebrtc を `m119.6045.2.2` に上げる
  - Apple 非公開 API を利用していたため、Apple からリジェクトされる問題を修正
  - @voluntas

## 2023.16.0 (2023-11-19)

- [ADD] SRTP keying material を取得する機能を実装
  - @melpon

## 2023.15.1 (2023-11-09)

- [FIX] macOS で USB 接続されたカメラが利用できなくなっていた問題を修正する
  - 2023.15.0 リリース時の WebRTC の更新に伴い、 macOS で USB 接続されたカメラが取得できなくなっていた
  - @enm10k

## 2023.15.0 (2023-10-31)

- [UPDATE] libwebrtc を m119.6045.2.1 に上げる
  - @voluntas @torikizi @melpon @enm10k
- [UPDATE] libwebrtc を m119 に上げたことで必要になった関連するライブラリもバージョンを上げる
  - Ubuntu で使用する clang のバージョンを 15 にアップデート
  - すべてのプラットフォームで set_target_properties の CXX_STANDARD と C_STANDARD を 20 にアップデート
  - ANDROID_NDK_VERSION を r26b にアップデート
  - CMAKE_VERSION を 3.27.7 にアップデート
  - @melpon @enm10k @torikizi
- [ADD] H.265 に対応
  - libwebrtc の m119.6045.2.1 で H.265 がサポートされたため、C++ SDK でも H.265 に対応
  - macOS / iOS / Android で H.265 が利用可能
  - @voluntas @torikizi @melpon @enm10k

## 2023.14.0 (2023-10-02)

- [UPDATE] Boost を 1.83.0 に上げる
  - @voluntas
- [UPDATE] libwebrtc を m117.5938.2.0 に上げる
  - @melpon @miosakuma @voluntas
- [FIX] `MacAudioOutputHelper` でコメントアウトしていた処理をコメントインする
  - 当初 libwebrtc のサンプルにはない処理で、消していた処理を復活させる
  - アプリの起動中に `MacAudioOutputHelper` を作成、削除を繰り返しても問題ないようにする
  - @torikizi

## 2023.13.1 (2023-09-12)

- [FIX] macOS で `AudioOutputHelper` を使おうとするとリンクエラーになっていたのを修正
  - @melpon

## 2023.13.0 (2023-09-12)

- [ADD] iOS 向けに音声出力先変更機能として `AudioOutputHelper` を追加
  - @tnoho

## 2023.12.1 (2023-09-10)

- [FIX] V4L2VideoCapturer でデバイス名の指定が無視されていたのを修正
  - @melpon
- [FIX] Android で動かすために必要な libwebrtc の初期化処理を追加
  - @melpon

## 2023.12.0 (2023-09-08)

- [CHANGE] MacCapturer の解放前に明示的に Stop() 関数を呼ぶ必要があるようになる
  - @melpon
- [FIX] iOS で MacCapturer の解放時に 10 秒間固まることがあるのを修正
  - @melpon
- [FIX] Android キャプチャラのエラーハンドリングを厳密にする
  - @melpon

## 2023.11.0 (2023-09-06)

- [UPDATE] libwebrtc を m116.5845.6.1 に上げる
  - @torikizi
- [UPDATE] m116 で `cricket::Codec` は protected になったため `cricket::CreateVideoCodec` を利用するように修正
  - @torikizi
- [ADD] `SoraSignaling::GetSelectedSignalingURL()` 関数を追加
  - @melpon

## 2023.10.0 (2023-08-26)

- [UPDATE] VPL のバージョンを v2023.3.1 に上げる
  - @torikizi
- [FIX] macOS, iOS のキャプチャラの破棄時にクラッシュすることがあるのを修正
  - @melpon

## 2023.9.0 (2023-08-08)

- [ADD] Lyra 抜きでビルドする --no-lyra オプションを追加
  - @melpon
- [FIX] Lyra に iOS 最小ターゲットを設定して、アプリの最小ターゲットの設定によっては起動直後に落ちることがあったのを修正する
  - @melpon

## 2023.8.0 (2023-07-28)

- [UPDATE] libwebrtc を m115.5790.7.0 に上げる
  - @melpon @miosakuma

## 2023.7.2 (2023-07-12)

- [FIX] 特定のタイミングで切断が発生すると Closing 状態で止まってしまうのを修正
  - @melpon

## 2023.7.1 (2023-06-26)

- [FIX] Connect 直後に Disconnect すると OnDisconnect コールバックが呼ばれないことがあるのを修正
  - @melpon

## 2023.7.0 (2023-06-19)

- [ADD] 映像コーデックパラメータを指定可能にする
  - `SoraSignalingConfig` に以下のフィールドを追加する:
    - `video_vp9_params`
    - `video_av1_params`
    - `video_h264_params`
  - 値は単なる JSON 値で C++ SDK は中身のバリーデーションなどは行わない
  - @sile
- [UPDATE] CMake を 3.26.4 に上げる
  - @torikizi

## 2023.6.0 (2023-05-30)

- [UPDATE] libwebrtc を m114.5735.2.0 に上げる
  - @miosakuma
- [FIX] 一部の Windows で VP8 の受信時にクラッシュする問題を修正する
  - Query した上で Init しても MFX_ERR_UNSUPPORTED になるため VPL の場合は毎回 Init を呼ぶようにする
  - @melpon

## 2023.5.1 (2023-05-16)

- [FIX] ソケット切断時にクラッシュすることがあるのを修正
  - @melpon

## 2023.5.0 (2023-05-08)

- [UPDATE] libwebrtc を m114.5735.0.1 に上げる
  - @melpon
- [UPDATE] Boost を 1.82.0 に上げる
  - @melpon
- [ADD] Sora の Forwarding Filter 機能を使えるようにする
  - @melpon
- [FIX] 接続エラー時に Websocket::OnWrite でクラッシュすることがあるのを修正
  - @melpon

## 2023.4.0 (2023-04-08)

- [CHANGE] SoraDefaultClient を削除して SoraClientContext を追加
  - @melpon
- [ADD] デバイス一覧を取得する機能を追加
  - @melpon
- [FIX] CUDA 未対応時に HWA を利用しない設定にしてもクラッシュしてしまうのを修正
  - @melpon

## 2023.3.0 (2023-04-02)

- [CHANGE] SoraSignalingConfig から audio_codec_lyra_params を削除して、代わりに audio_codec_lyra_bitrate と audio_codec_lyra_usedtx を追加
  - @melpon
- [UPDATE] Lyra で接続する時に、チャンネル全体で同じバージョンの Lyra を使っているかをチェックする
  - @melpon
- [UPDATE] JetPack 5.1 に対応する
  - @tnoho @melpon
- [UPDATE] oneVPL を v2023.1.3 に上げる
  - @voluntas
- [UPDATE] CMake を 3.25.3 に上げる
  - @melpon
- [UPDATE] 例外が有効になっていなかった一部の依存ライブラリも例外を有効にする
  - @melpon
- [UPDATE] libwebrtc を m111.5563.4.4 に上げる
  - @melpon
- [ADD] 2022.11.0 で無効にしていた Jetson の HW MJPEG デコーダを有効にする
  - @tnoho @melpon
- [FIX] WS 切断時のタイムアウトが起きた際に無効な関数オブジェクトを呼んでいたのを修正
  - @melpon

## 2023.2.0 (2023-03-05)

- [UPDATE] NVIDIA Video Codec SDK を 12.0 に上げる
  - @melpon
- [UPDATE] deprecated になった actions/create-release と actions/upload-release の利用をやめて softprops/action-gh-release を利用する
  - @melpon
- [FIX] WebSocket 接続時に即エラーになると正常に OnDisconnect が呼ばれないのを修正
  - @melpon
- [UPDATE] oneVPL のデコードでリサイズに対応してなかったのを修正
  - @melpon
- [UPDATE] libwebrtc を m111.5563.4.2 に上げる
  - @melpon @miosakuma

## 2023.1.0 (2023-01-12)

- [ADD] SoraSignalingConfig に audio_streaming_language_code を追加
  - @melpon
- [UPDATE] libwebrtc を m109.5414.2.0 に上げる
  - @melpon
- [UPDATE] Boost を 1.81.0 に上げる
  - @melpon
- [UPDATE] oneVPL を v2023.1.1 に上げる
  - @melpon

## 2022.19.0 (2022-12-25)

- [CHANGE] Lyra を静的ライブラリ化
  - @melpon
- [ADD] iOS を Lyra に対応
  - @melpon

## 2022.18.0 (2022-11-11)

- [UPDATE] Lyra を `1.3.0` に上げる
  - @melpon

## 2022.17.1 (2022-11-10)

- [FIX] macOS 用 Lyra のターゲットがローカルのアーキテクチャに依存していたのを修正
  - @melpon

## 2022.17.0 (2022-11-08)

- [ADD] Lyra パラメータを指定可能にする
  - @melpon

## 2022.16.0 (2022-11-08)

- [CHANGE] Boost.Filesystem への依存を追加
  - @melpon
- [ADD] オーディオコーデック Lyra に対応
  - @melpon

## 2022.15.1 (2022-11-02)

- [FIX] CI で .env ファイル名の誤りを修正する
  - @miosakuma

## 2022.15.0 (2022-11-02)

- [UPDATE] libwebrtc を `M107.5304@{#4}` に上げる
  - @torikizi
- [FIX] 廃止になった `audio_opus_params_clock_rate` を削除する
  - @torikizi
- [FIX] CI で Windows の場合 $GITHUB_OUTPUT に "\r" が混入するのを除去する
  - @miosakuma

## 2022.14.0 (2022-10-05)

- [ADD] SoraSignaling に `GetConnectionID`, `GetConnectedSignalingURL`, `IsConnectedDataChannel`, `IsConnectedWebsocket` 関数を追加
  - @melpon

## 2022.13.0 (2022-10-05)

- [CHANGE] `ScalableVideoTrackSource::OnCapturedFrame` の戻り値を void から bool にする
  - @melpon
- [ADD] SoraSignalingConfig に `audio_opus_params_clock_rate`, `signaling_notify_metadata`, `disable_signaling_url_randomization` を追加
  - @melpon

## 2022.12.3 (2022-10-03)

- [UPDATE] Boost のビルド済みバイナリに Filesystem を追加
  - @melpon

## 2022.12.2 (2022-10-02)

- [UPDATE] iOS の IPHONE_DEPLOYMENT_TARGET を 10.0 から 13.0 に上げる
  - @melpon

## 2022.12.1 (2022-09-24)

- [FIX] ログが毎フレーム出力されてしまっていたのを修正
  - @melpon

## 2022.12.0 (2022-09-24)

- [CHANGE] デバイスからキャプチャしたフレームをコールバックする機能を追加
  - これに伴ってデバイス生成時のパラメータが変わるので下位互換性は無くなる
  - @melpon

## 2022.11.1 (2022-09-14)

- [FIX] Android で SoraDefaultClient が使えなかったのを修正
  - @melpon
- [FIX] SoraDefaultClientConfig の `use_audio_deivce` はスペルミスなので `use_audio_device` に修正
  - @melpon

## 2022.11.0 (2022-09-09)

- [CHANGE] Jetson の HW MJPEG デコーダを一時的に無効にする
  - @melpon
- [CHANGE] OnSetOffer に type: offer メッセージ全体を渡す
  - @melpon
- [UPDATE] libwebrtc を m105.5195.0.0 に上げる
  - @voluntas @melpon
- [UPDATE] boost を 1.80.0 に上げる
  - @voluntas
- [UPDATE] Android NDK を r25b に上げる
  - @melpon

## 2022.10.1 (2022-08-15)

- [CHANGE] Android の要求バージョンを `24` から `29` に上げる
  - @melpon
- [UPDATE] libwebrtc を m104.5112.7.0 に上げる
  - @melpon
- [ADD] Sora C++ SDK のリリースに Boost パッケージを含める
  - @melpon

## 2022.9.0 (2022-07-29)

- [CHANGE] DataChannel の準備が完了したことを示す通知を追加
  - 純粋仮想関数を追加したため破壊的変更となる
  - @melpon
- [ADD] mid を取得する関数を追加
  - @melpon

## 2022.8.0 (2022-07-28)

- [CHANGE] Intel Media SDK を利用したハードウェアエンコーダ/デコーダを削除
  - @melpon
- [ADD] oneVPL を利用したハードウェアエンコーダ/デコーダを追加
  - @melpon

## 2022.7.8 (2022-07-25)

- [FIX] NVDEC のデコードの画像サイズが変わった時に即時で追従するように修正
  - @melpon

## 2022.7.7 (2022-07-23)

- [FIX] カメラの無い Android 端末でカメラを利用しようとすると落ちるのを修正
  - @melpon

## 2022.7.6 (2022-07-23)

- [FIX] デコードする画像が小さすぎる場合に NVDEC がエラーを吐いて止まっていたのを修正
  - @melpon
- [FIX] NVDEC のフレームがロックされていなくて未定義動作を起こしていたのを修正
  - @melpon

## 2022.7.5 (2022-07-20)

- [FIX] サイマルキャストの場合、ネイティブバッファを I420 に変換するように修正
  - @melpon

## 2022.7.4 (2022-07-20)

- [FIX] アライメントされてないと動作しないエンコーダのために、ハードウェアエンコーダやサイマルキャストの場合は crop するように修正
  - @melpon

## 2022.7.3 (2022-07-13)

- [FIX] Rotation の解決が入ってなかったのを修正
  - @melpon

## 2022.7.2 (2022-07-12)

- [FIX] DataChannel の切れ方によってはアクセス違反になることがあったのを修正
  - @melpon

## 2022.7.1 (2022-07-11)

- [ADD] run.py に --relwithdebinfo フラグを追加
  - @melpon
- [UPDATE] libwebrtc を m103.5060.5.0 に上げる
  - @voluntas @melpon
- [FIX] boost::asio::post に strand を渡してなかったのを修正
  - @melpon

## 2022.6.2 (2022-07-03)

- [FIX] DataChannel が有効だと切断時にエラーが起きていたのを修正
  - @melpon

## 2022.6.1 (2022-06-30)

- [FIX] Jetson の AV1 HWA のデコーダがうまく動いてなかったのを修正
  - @melpon

## 2022.6.0

**リリース日**: 2022-06-30

- [ADD] Jetson の AV1 HWA に対応
  - @melpon

## 2022.5.0

**リリース日**: 2022-06-22

- [ADD] bundle_id を追加
  - @melpon
- [ADD] HTTP Proxy に対応
  - @melpon
- [CHANGE] multistream, simulcast, spotlight を optional 型に変更
  - @melpon

## 2022.4.0

- [ADD] `MacCapturer::EnumVideoDevice` を追加
  - @melpon

## 2022.3.0

- [ADD] client_id, spotlight_focus_rid, spotlight_unfocus_rid, simulcast_rid を追加
  - @melpon
- [FIX] 公開ヘッダーが NvCodec ヘッダーに依存していたのを修正
  - @melpon

## 2022.2.1

- [FIX] Windows で libmfx.lib ライブラリを要求してしまっていたのを修正
  - @melpon

## 2022.2.0

- [ADD] Intel Media SDK を使ったハードウェアエンコーダ/デコーダを実装
  - @melpon

## 2022.1.2

- [FIX] ubuntu-22.04_x86_64 のリリースバイナリが含まれていなかったのを修正
  - @melpon

## 2022.1.0

**祝リリース**
