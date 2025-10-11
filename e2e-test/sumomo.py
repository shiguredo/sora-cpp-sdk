"""Sumomo プロセスを管理するためのクラス"""

import json
import platform
import shlex
import subprocess
import time
from pathlib import Path
from types import TracebackType
from typing import Any, Literal, Self

import httpx


class Sumomo:
    """Sumomo プロセスを管理するクラス"""

    def __init__(
        self,
        # Sora に関するオプション（必須）
        signaling_url: str,
        channel_id: str,
        role: Literal["sendonly", "recvonly", "sendrecv"],
        client_id: str | None = None,
        video: bool = True,
        audio: bool = True,
        video_device: str | None = None,
        video_codec_type: Literal["VP8", "VP9", "AV1", "H264", "H265"] | None = None,
        audio_codec_type: Literal["OPUS"] | None = None,
        resolution: str | None = None,  # QVGA, VGA, HD, FHD, 4K, or [WIDTH]x[HEIGHT]
        hw_mjpeg_decoder: bool | None = None,
        video_bit_rate: int | None = None,  # 0-30000
        audio_bit_rate: int | None = None,  # 0-510
        video_h264_params: dict[str, Any] | None = None,
        video_h265_params: dict[str, Any] | None = None,
        metadata: dict[str, Any] | None = None,
        spotlight: bool | None = None,
        spotlight_number: int | None = None,  # 0-8
        simulcast: bool | None = None,
        data_channel_signaling: bool | None = None,
        ignore_disconnect_websocket: bool | None = None,
        # プロキシ設定
        proxy_url: str | None = None,
        proxy_username: str | None = None,
        proxy_password: str | None = None,
        # SDL 設定
        use_sdl: bool = False,
        window_width: int | None = None,  # 180-16384
        window_height: int | None = None,  # 180-16384
        show_me: bool = False,
        fullscreen: bool = False,
        # Sixel 設定
        use_sixel: bool = False,
        sixel_width: int | None = None,
        sixel_height: int | None = None,
        # ANSI 設定
        use_ansi: bool = False,
        ansi_width: int | None = None,
        ansi_height: int | None = None,
        # セキュリティ設定
        insecure: bool = False,
        client_cert: str | None = None,  # PEM ファイルパス
        client_key: str | None = None,  # PEM ファイルパス
        ca_cert: str | None = None,  # PEM ファイルパス
        # HTTP サーバー設定
        http_port: int | None = None,
        # パフォーマンス設定
        degradation_preference: Literal[
            "disabled", "maintain_framerate", "maintain_resolution", "balanced"
        ]
        | None = None,
        cpu_adaptation: bool | None = None,
        # libcamera 設定
        use_libcamera: bool = False,
        use_libcamera_native: bool = False,
        libcamera_controls: list[tuple[str, str]] | None = None,
        # Fake デバイス設定（デフォルトで fake を使用）
        fake_capture_device: bool = True,
        # オーディオデバイス設定
        audio_recording_device: str | None = None,
        audio_playout_device: str | None = None,
        # コーデック設定
        openh264: str | None = None,  # ファイルパス
        vp8_encoder: Literal[
            "internal",
            "cisco_openh264",
            "intel_vpl",
            "nvidia_video_codec",
            "amd_amf",
            "raspi_v4l2m2m",
        ]
        | None = None,
        vp8_decoder: Literal[
            "internal",
            "cisco_openh264",
            "intel_vpl",
            "nvidia_video_codec",
            "amd_amf",
            "raspi_v4l2m2m",
        ]
        | None = None,
        vp9_encoder: Literal[
            "internal",
            "cisco_openh264",
            "intel_vpl",
            "nvidia_video_codec",
            "amd_amf",
            "raspi_v4l2m2m",
        ]
        | None = None,
        vp9_decoder: Literal[
            "internal",
            "cisco_openh264",
            "intel_vpl",
            "nvidia_video_codec",
            "amd_amf",
            "raspi_v4l2m2m",
        ]
        | None = None,
        h264_encoder: Literal[
            "internal",
            "cisco_openh264",
            "intel_vpl",
            "nvidia_video_codec",
            "amd_amf",
            "raspi_v4l2m2m",
        ]
        | None = None,
        h264_decoder: Literal[
            "internal",
            "cisco_openh264",
            "intel_vpl",
            "nvidia_video_codec",
            "amd_amf",
            "raspi_v4l2m2m",
        ]
        | None = None,
        h265_encoder: Literal[
            "internal",
            "cisco_openh264",
            "intel_vpl",
            "nvidia_video_codec",
            "amd_amf",
            "raspi_v4l2m2m",
        ]
        | None = None,
        h265_decoder: Literal[
            "internal",
            "cisco_openh264",
            "intel_vpl",
            "nvidia_video_codec",
            "amd_amf",
            "raspi_v4l2m2m",
        ]
        | None = None,
        av1_encoder: Literal[
            "internal",
            "cisco_openh264",
            "intel_vpl",
            "nvidia_video_codec",
            "amd_amf",
            "raspi_v4l2m2m",
        ]
        | None = None,
        av1_decoder: Literal[
            "internal",
            "cisco_openh264",
            "intel_vpl",
            "nvidia_video_codec",
            "amd_amf",
            "raspi_v4l2m2m",
        ]
        | None = None,
        # ログレベル
        log_level: Literal["verbose", "info", "warning", "error"] | None = None,
        # その他のカスタム引数
        extra_args: list[str] | None = None,
        # 起動待機時間
        initial_wait: int | None = None,
    ) -> None:
        """
        Sumomo プロセスを管理するクラス

        必須パラメータ:
            - signaling_url: Sora のシグナリング URL
            - channel_id: チャンネル ID
            - role: ロール ("sendonly", "recvonly", "sendrecv")

        注意:
            デフォルトで fake_capture_device=True になっています。
            実際のカメラ・マイクを使用する場合は明示的に False を指定してください。

        使用例:
            # デフォルト（fake デバイスを使用）
            with Sumomo(
                signaling_url="wss://sora.example.com/signaling",
                channel_id="test-channel",
                role="sendonly",
                http_port=8080,
            ) as s:
                stats = s.get_stats()

            # 実際のカメラ・マイクを使用する場合
            with Sumomo(
                signaling_url="wss://sora.example.com/signaling",
                channel_id="test-channel",
                role="sendonly",
                fake_capture_device=False,  # 実際のカメラ・マイクを使用
                http_port=8080,
            ) as s:
                stats = s.get_stats()
        """
        # 実行ファイルのパスを自動検出
        self.executable_path = self._get_sumomo_executable_path()
        self.process: subprocess.Popen[Any] | None = None
        self.http_port = http_port if http_port is not None else 0
        self.http_host = "127.0.0.1"
        # デフォルトの初期待機時間を設定
        self.initial_wait = initial_wait if initial_wait is not None else 2

        # すべての引数を保存
        self.kwargs: dict[str, Any] = {
            "signaling_url": signaling_url,
            "channel_id": channel_id,
            "role": role,
            "client_id": client_id,
            "video": video,
            "audio": audio,
            "video_device": video_device,
            "video_codec_type": video_codec_type,
            "audio_codec_type": audio_codec_type,
            "resolution": resolution,
            "hw_mjpeg_decoder": hw_mjpeg_decoder,
            "video_bit_rate": video_bit_rate,
            "audio_bit_rate": audio_bit_rate,
            "video_h264_params": video_h264_params,
            "video_h265_params": video_h265_params,
            "metadata": metadata,
            "spotlight": spotlight,
            "spotlight_number": spotlight_number,
            "simulcast": simulcast,
            "data_channel_signaling": data_channel_signaling,
            "ignore_disconnect_websocket": ignore_disconnect_websocket,
            "proxy_url": proxy_url,
            "proxy_username": proxy_username,
            "proxy_password": proxy_password,
            "use_sdl": use_sdl,
            "window_width": window_width,
            "window_height": window_height,
            "show_me": show_me,
            "fullscreen": fullscreen,
            "use_sixel": use_sixel,
            "sixel_width": sixel_width,
            "sixel_height": sixel_height,
            "use_ansi": use_ansi,
            "ansi_width": ansi_width,
            "ansi_height": ansi_height,
            "insecure": insecure,
            "client_cert": client_cert,
            "client_key": client_key,
            "ca_cert": ca_cert,
            "http_port": http_port,
            "degradation_preference": degradation_preference,
            "cpu_adaptation": cpu_adaptation,
            "use_libcamera": use_libcamera,
            "use_libcamera_native": use_libcamera_native,
            "libcamera_controls": libcamera_controls,
            "fake_capture_device": fake_capture_device,
            "audio_recording_device": audio_recording_device,
            "audio_playout_device": audio_playout_device,
            "openh264": openh264,
            "vp8_encoder": vp8_encoder,
            "vp8_decoder": vp8_decoder,
            "vp9_encoder": vp9_encoder,
            "vp9_decoder": vp9_decoder,
            "h264_encoder": h264_encoder,
            "h264_decoder": h264_decoder,
            "h265_encoder": h265_encoder,
            "h265_decoder": h265_decoder,
            "av1_encoder": av1_encoder,
            "av1_decoder": av1_decoder,
            "log_level": log_level,
            "extra_args": extra_args,
        }

        # HTTP クライアントの初期化（None で初期化）
        self._http_client: httpx.Client | None = None

    def _get_sumomo_executable_path(self) -> str:
        """ビルド済みの sumomo 実行ファイルのパスを自動検出"""
        project_root = Path(__file__).parent.parent
        build_dir = project_root / "examples" / "_build"

        # _build ディレクトリ内の実際のビルドターゲットを検出
        if not build_dir.exists():
            raise RuntimeError(
                f"Build directory {build_dir} does not exist. "
                f"Please build with: python3 run.py build <target>"
            )

        # Windows の場合は .exe 拡張子を考慮
        system = platform.system().lower()
        exe_suffix = ".exe" if system == "windows" else ""

        available_targets = [
            d.name
            for d in build_dir.iterdir()
            if d.is_dir() and (d / "release" / "sumomo" / f"sumomo{exe_suffix}").exists()
        ]

        if not available_targets:
            raise RuntimeError(
                f"No built sumomo executables found in {build_dir}. "
                f"Please build with: python3 run.py build <target>"
            )

        if len(available_targets) == 1:
            # ビルドが1つだけの場合は自動選択
            target = available_targets[0]
            print(f"Auto-detected sumomo target: {target}")
        else:
            # 複数ビルドがある場合は、プラットフォームに応じて優先順位を決める
            machine = platform.machine().lower()

            # プラットフォームに応じた優先順位リスト
            if system == "darwin":
                if machine == "arm64" or machine == "aarch64":
                    preferred = ["macos_arm64", "macos_x86_64"]
                else:
                    preferred = ["macos_x86_64", "macos_arm64"]
            elif system == "linux":
                if machine == "aarch64":
                    preferred = ["ubuntu-24.04_armv8", "ubuntu-22.04_armv8", "ubuntu-20.04_armv8"]
                else:
                    preferred = [
                        "ubuntu-24.04_x86_64",
                        "ubuntu-22.04_x86_64",
                        "ubuntu-20.04_x86_64",
                    ]
            elif system == "windows":
                preferred = ["windows_x86_64"]
            else:
                preferred = []

            # 優先順位に従って選択
            target = None
            for pref in preferred:
                if pref in available_targets:
                    target = pref
                    print(
                        f"Auto-detected sumomo target: {target} (from {len(available_targets)} available)"
                    )
                    break

            if not target:
                # 優先順位で見つからない場合は最初のものを使用
                target = available_targets[0]
                print(
                    f"Using first available target: {target} (available: {', '.join(available_targets)})"
                )

        # sumomo のパスを構築
        assert target is not None
        sumomo_path = (
            project_root
            / "examples"
            / "_build"
            / target
            / "release"
            / "sumomo"
            / f"sumomo{exe_suffix}"
        )

        if not sumomo_path.exists():
            raise RuntimeError(
                f"sumomo executable not found at {sumomo_path}. "
                f"Please build with: python3 run.py build {target}"
            )

        return str(sumomo_path)

    def __enter__(self) -> Self:
        """コンテキストマネージャーの開始"""
        try:
            # コマンドライン引数を構築
            args = self._build_args(**self.kwargs)

            # 起動コマンドを表示
            cmd = [self.executable_path] + args
            # JSON を含む引数を適切に表示するため、shlex.quote を使用
            quoted_cmd = " ".join(shlex.quote(arg) for arg in cmd)
            print(f"Starting sumomo with command: {quoted_cmd}")

            # プロセスを起動
            try:
                self.process = subprocess.Popen(
                    cmd,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.PIPE,
                    text=True,
                )
                print(f"Started sumomo process with PID: {self.process.pid}")
            except FileNotFoundError:
                raise RuntimeError(
                    f"Sumomo executable not found at {self.executable_path}. "
                    f"Please build with: python3 run.py build <target>"
                )
            except Exception as e:
                raise RuntimeError(f"Failed to start sumomo process: {e}")

            # プロセスが起動して HTTP サーバーが利用可能になるまで待機
            if self.http_port is not None and self.http_port != 0:
                self._wait_for_startup(self.http_port, timeout=30, initial_wait=self.initial_wait)
            else:
                # http_port が指定されていない場合は、単純に初期待機時間だけ待つ
                if self.initial_wait > 0:
                    time.sleep(self.initial_wait)

            # Windows の場合、プロセスが早期終了していないか確認
            if self.process and platform.system().lower() == "windows":
                poll_result = self.process.poll()
                if poll_result is not None:
                    # プロセスが終了していたらエラーメッセージを表示
                    stderr_output = ""
                    if self.process.stderr:
                        try:
                            stderr_output = self.process.stderr.read()
                        except Exception:
                            pass
                    raise RuntimeError(
                        f"sumomo.exe exited unexpectedly with code {poll_result}\n"
                        f"Stderr: {stderr_output}"
                    )

            # HTTP クライアントを作成
            self._http_client = httpx.Client(timeout=10.0)

            return self
        except Exception:
            # 例外が発生した場合は必ずクリーンアップ
            self._cleanup()
            raise

    def __exit__(
        self,
        _exc_type: type[BaseException] | None,
        _exc_val: BaseException | None,
        _exc_tb: TracebackType | None,
    ) -> Literal[False]:
        """コンテキストマネージャーの終了"""
        # HTTP クライアントをクリーンアップ
        if self._http_client:
            self._http_client.close()
            self._http_client = None

        self._cleanup()
        return False

    def _build_args(self, **kwargs: Any) -> list[str]:
        """コマンドライン引数を構築"""
        args = []

        # ログレベル
        if kwargs.get("log_level"):
            args.extend(["--log-level", kwargs["log_level"]])

        # 解像度
        if kwargs.get("resolution"):
            args.extend(["--resolution", kwargs["resolution"]])

        # HW MJPEG デコーダー
        if kwargs.get("hw_mjpeg_decoder") is not None:
            args.extend(["--hw-mjpeg-decoder", "true" if kwargs["hw_mjpeg_decoder"] else "false"])

        # Sora 設定
        if kwargs.get("signaling_url"):
            args.extend(["--signaling-url", kwargs["signaling_url"]])
        if kwargs.get("channel_id"):
            args.extend(["--channel-id", kwargs["channel_id"]])
        if kwargs.get("role"):
            args.extend(["--role", kwargs["role"]])
        if kwargs.get("client_id"):
            args.extend(["--client-id", kwargs["client_id"]])

        # ビデオ・オーディオ
        if kwargs.get("video") is not None:
            args.extend(["--video", "true" if kwargs["video"] else "false"])
        if kwargs.get("audio") is not None:
            args.extend(["--audio", "true" if kwargs["audio"] else "false"])
        if kwargs.get("video_device"):
            args.extend(["--video-device", kwargs["video_device"]])
        if kwargs.get("video_codec_type"):
            args.extend(["--video-codec-type", kwargs["video_codec_type"]])
        if kwargs.get("audio_codec_type"):
            args.extend(["--audio-codec-type", kwargs["audio_codec_type"]])

        # ビットレート
        if kwargs.get("video_bit_rate") is not None:
            args.extend(["--video-bit-rate", str(kwargs["video_bit_rate"])])
        if kwargs.get("audio_bit_rate") is not None:
            args.extend(["--audio-bit-rate", str(kwargs["audio_bit_rate"])])

        # コーデックパラメータ
        if kwargs.get("video_h264_params"):
            args.extend(["--video-h264-params", json.dumps(kwargs["video_h264_params"])])
        if kwargs.get("video_h265_params"):
            args.extend(["--video-h265-params", json.dumps(kwargs["video_h265_params"])])

        # メタデータ
        if kwargs.get("metadata"):
            args.extend(["--metadata", json.dumps(kwargs["metadata"])])

        # スポットライト
        if kwargs.get("spotlight") is not None:
            args.extend(["--spotlight", "true" if kwargs["spotlight"] else "false"])
        if kwargs.get("spotlight_number") is not None:
            args.extend(["--spotlight-number", str(kwargs["spotlight_number"])])

        # サイマルキャスト
        if kwargs.get("simulcast") is not None:
            args.extend(["--simulcast", "true" if kwargs["simulcast"] else "false"])

        # データチャネルシグナリング
        if kwargs.get("data_channel_signaling") is not None:
            args.extend(
                [
                    "--data-channel-signaling",
                    "true" if kwargs["data_channel_signaling"] else "false",
                ]
            )

        # 切断時の WebSocket 無視
        if kwargs.get("ignore_disconnect_websocket") is not None:
            args.extend(
                [
                    "--ignore-disconnect-websocket",
                    "true" if kwargs["ignore_disconnect_websocket"] else "false",
                ]
            )

        # プロキシ設定
        if kwargs.get("proxy_url"):
            args.extend(["--proxy-url", kwargs["proxy_url"]])
        if kwargs.get("proxy_username"):
            args.extend(["--proxy-username", kwargs["proxy_username"]])
        if kwargs.get("proxy_password"):
            args.extend(["--proxy-password", kwargs["proxy_password"]])

        # SDL 設定
        if kwargs.get("use_sdl"):
            args.append("--use-sdl")
        if kwargs.get("window_width") is not None:
            args.extend(["--window-width", str(kwargs["window_width"])])
        if kwargs.get("window_height") is not None:
            args.extend(["--window-height", str(kwargs["window_height"])])
        if kwargs.get("show_me"):
            args.append("--show-me")
        if kwargs.get("fullscreen"):
            args.append("--fullscreen")

        # Sixel 設定
        if kwargs.get("use_sixel"):
            args.append("--use-sixel")
        if kwargs.get("sixel_width") is not None:
            args.extend(["--sixel-width", str(kwargs["sixel_width"])])
        if kwargs.get("sixel_height") is not None:
            args.extend(["--sixel-height", str(kwargs["sixel_height"])])

        # ANSI 設定
        if kwargs.get("use_ansi"):
            args.append("--use-ansi")
        if kwargs.get("ansi_width") is not None:
            args.extend(["--ansi-width", str(kwargs["ansi_width"])])
        if kwargs.get("ansi_height") is not None:
            args.extend(["--ansi-height", str(kwargs["ansi_height"])])

        # セキュリティ設定
        if kwargs.get("insecure"):
            args.append("--insecure")
        if kwargs.get("client_cert"):
            args.extend(["--client-cert", kwargs["client_cert"]])
        if kwargs.get("client_key"):
            args.extend(["--client-key", kwargs["client_key"]])
        if kwargs.get("ca_cert"):
            args.extend(["--ca-cert", kwargs["ca_cert"]])

        # HTTP サーバー設定
        if kwargs.get("http_port") is not None:
            args.extend(["--http-port", str(kwargs["http_port"])])
        # http-host は 127.0.0.1 固定
        args.extend(["--http-host", "127.0.0.1"])

        # パフォーマンス設定
        if kwargs.get("degradation_preference"):
            args.extend(["--degradation-preference", kwargs["degradation_preference"]])
        if kwargs.get("cpu_adaptation") is not None:
            args.extend(["--cpu-adaptation", "true" if kwargs["cpu_adaptation"] else "false"])

        # libcamera 設定
        if kwargs.get("use_libcamera"):
            args.append("--use-libcamera")
        if kwargs.get("use_libcamera_native"):
            args.append("--use-libcamera-native")
        if kwargs.get("libcamera_controls"):
            for key, value in kwargs["libcamera_controls"]:
                args.extend(["--libcamera-control", key, value])

        # Fake デバイス設定
        if kwargs.get("fake_capture_device"):
            args.append("--fake-capture-device")

        # オーディオデバイス設定
        if kwargs.get("audio_recording_device"):
            args.extend(["--audio-recording-device", kwargs["audio_recording_device"]])
        if kwargs.get("audio_playout_device"):
            args.extend(["--audio-playout-device", kwargs["audio_playout_device"]])

        # コーデック設定
        if kwargs.get("openh264"):
            args.extend(["--openh264", kwargs["openh264"]])
        if kwargs.get("vp8_encoder"):
            args.extend(["--vp8-encoder", kwargs["vp8_encoder"]])
        if kwargs.get("vp8_decoder"):
            args.extend(["--vp8-decoder", kwargs["vp8_decoder"]])
        if kwargs.get("vp9_encoder"):
            args.extend(["--vp9-encoder", kwargs["vp9_encoder"]])
        if kwargs.get("vp9_decoder"):
            args.extend(["--vp9-decoder", kwargs["vp9_decoder"]])
        if kwargs.get("h264_encoder"):
            args.extend(["--h264-encoder", kwargs["h264_encoder"]])
        if kwargs.get("h264_decoder"):
            args.extend(["--h264-decoder", kwargs["h264_decoder"]])
        if kwargs.get("h265_encoder"):
            args.extend(["--h265-encoder", kwargs["h265_encoder"]])
        if kwargs.get("h265_decoder"):
            args.extend(["--h265-decoder", kwargs["h265_decoder"]])
        if kwargs.get("av1_encoder"):
            args.extend(["--av1-encoder", kwargs["av1_encoder"]])
        if kwargs.get("av1_decoder"):
            args.extend(["--av1-decoder", kwargs["av1_decoder"]])

        # その他のカスタム引数
        if kwargs.get("extra_args"):
            args.extend(kwargs["extra_args"])

        return args

    def _wait_for_startup(self, http_port: int, timeout: int = 30, initial_wait: int = 2) -> None:
        """プロセスが起動して HTTP サーバーが利用可能になるまで待機"""
        if not self.process:
            raise RuntimeError("Process not started")

        # プロセスが完全に起動するまで少し待機
        if initial_wait > 0:
            time.sleep(initial_wait)

        print(f"Waiting for HTTP endpoint to be ready on port {http_port} (timeout: {timeout}s)...")
        start_time = time.time()

        with httpx.Client() as client:
            attempt = 0
            while time.time() - start_time < timeout:
                attempt += 1
                # プロセスの状態を確認
                poll_result = self.process.poll()
                if poll_result is not None:
                    # プロセスが終了している場合のみ stderr を読む
                    error_msg = f"Process exited unexpectedly with code {poll_result}"
                    if self.process.stderr:
                        # プロセスが終了しているので read() はブロックしない
                        try:
                            stderr_output = self.process.stderr.read()
                            if stderr_output:
                                error_msg += f"\nStderr output:\n{stderr_output}"
                        except Exception:
                            pass
                    raise RuntimeError(error_msg)

                # HTTP エンドポイントをチェック
                try:
                    # http_host は 127.0.0.1 固定
                    url = f"http://{self.http_host}:{http_port}/stats"
                    response = client.get(url, timeout=5)
                    if response.status_code == 200:
                        elapsed = time.time() - start_time
                        print(f"Sumomo started successfully after {elapsed:.1f}s")
                        return
                except httpx.ConnectError:
                    # 接続エラーは無視して次の試行へ
                    pass
                except httpx.ConnectTimeout:
                    pass
                except Exception as e:
                    # その他の例外（ReadTimeout など）も無視して続行
                    elapsed = time.time() - start_time
                    if elapsed > 5:  # 5秒以上経過していたらログ出力
                        print(f"  Unexpected error (will retry): {e}")

                # 各試行後にもプロセスの状態を確認
                poll_result_after = self.process.poll()

                # プロセスが終了していた場合、即座に終了して stderr/stdout を確認
                if poll_result_after is not None:
                    # この時点でプロセスは既に終了しているため、read() はブロックしない
                    error_msg = f"Process exited with code {poll_result_after} during startup"

                    if self.process.stderr:
                        try:
                            stderr_output = self.process.stderr.read()
                            if stderr_output:
                                error_msg += f"\n\nStderr:\n{stderr_output}"
                        except Exception:
                            pass

                    if self.process.stdout:
                        try:
                            stdout_output = self.process.stdout.read()
                            if stdout_output:
                                error_msg += f"\n\nStdout:\n{stdout_output}"
                        except Exception:
                            pass

                    raise RuntimeError(error_msg)

                # 次の試行まで1秒待機
                time.sleep(1)

            # タイムアウト
            if self.process:
                poll_result = self.process.poll()

                # プロセスがまだ実行中の場合、強制終了して stderr/stdout を読む
                if poll_result is None:
                    self.process.terminate()
                    try:
                        self.process.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        self.process.kill()
                        self.process.wait()

                # プロセスが終了したので stderr/stdout を読む
                error_msg = f"sumomo process failed to start within {timeout} seconds"

                if self.process.stderr:
                    try:
                        stderr_output = self.process.stderr.read()
                        if stderr_output:
                            error_msg += f"\n\nStderr (last 2000 chars):\n{stderr_output[-2000:]}"
                    except Exception:
                        pass

                if self.process.stdout:
                    try:
                        stdout_output = self.process.stdout.read()
                        if stdout_output:
                            error_msg += f"\n\nStdout (last 2000 chars):\n{stdout_output[-2000:]}"
                    except Exception:
                        pass

                self._cleanup()
                raise RuntimeError(error_msg)

            self._cleanup()
            raise RuntimeError(f"sumomo process failed to start within {timeout} seconds")

    def _cleanup(self) -> None:
        """プロセスをクリーンアップ"""
        if self.process:
            pid = self.process.pid
            print(f"Terminating sumomo process (PID: {pid})")

            # 標準的な終了シグナルを送信
            self.process.terminate()
            try:
                # 短めのタイムアウトで終了を待つ
                self.process.wait(timeout=5)
                print(f"Sumomo process (PID: {pid}) terminated gracefully")
            except subprocess.TimeoutExpired:
                # タイムアウトした場合は強制終了
                print(f"Force killing sumomo process (PID: {pid})")
                self.process.kill()
                self.process.wait()
                print(f"Sumomo process (PID: {pid}) killed")

            # stderr の残りを読み取ってリソースを解放
            if hasattr(self.process, "stderr") and self.process.stderr:
                try:
                    self.process.stderr.close()
                except:
                    pass

            self.process = None

            # プロセス終了後の短い待機（リソース解放のため）
            time.sleep(0.2)

    def get_stats(self) -> list[dict[str, Any]]:
        """
        統計情報を取得

        Returns:
            統計情報の配列（各要素は統計情報の辞書）

        Raises:
            RuntimeError: HTTP クライアントが初期化されていない場合

        使用例:
            # 通常の使用（即座に統計情報を取得）
            stats = sumomo.get_stats()
            # stats は配列で、各要素が統計情報の辞書
        """
        if not self._http_client:
            raise RuntimeError("HTTP client not initialized")
        if self.http_port is None or self.http_port == 0:
            raise RuntimeError("HTTP port not configured")

        # Windows の場合、プロセスが生きているか確認
        if self.process and platform.system().lower() == "windows":
            if self.process.poll() is not None:
                stderr_output = ""
                if hasattr(self.process, "stderr") and self.process.stderr:
                    try:
                        stderr_output = self.process.stderr.read()
                    except:
                        pass
                raise RuntimeError(
                    f"sumomo.exe has crashed (exit code: {self.process.returncode})\n"
                    f"Stderr: {stderr_output}"
                )

        try:
            # http_host は 127.0.0.1 固定
            response = self._http_client.get(f"http://{self.http_host}:{self.http_port}/stats")
            response.raise_for_status()
            return response.json()
        except Exception as e:
            # Windows の場合、エラー時にプロセスの状態を確認
            if self.process and platform.system().lower() == "windows":
                if self.process.poll() is not None:
                    stderr_output = ""
                    if hasattr(self.process, "stderr") and self.process.stderr:
                        try:
                            stderr_output = self.process.stderr.read()
                        except:
                            pass
                    raise RuntimeError(
                        f"sumomo.exe crashed while getting stats (exit code: {self.process.returncode})\n"
                        f"Stderr: {stderr_output}\n"
                        f"Original error: {e}"
                    )
            raise
