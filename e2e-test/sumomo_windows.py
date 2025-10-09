"""Windows 調査用に Sumomo を詳細ログ付きで管理するヘルパー"""

from __future__ import annotations

import json
import platform
import shlex
import subprocess
import threading
import time
from pathlib import Path
from typing import Any, Iterable, Literal

import httpx


class SumomoWindows:
    """Windows 調査を支援するための Sumomo 管理クラス"""

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
        resolution: str | None = None,
        hw_mjpeg_decoder: bool = False,
        video_bit_rate: int | None = None,
        audio_bit_rate: int | None = None,
        video_h264_params: dict[str, Any] | None = None,
        video_h265_params: dict[str, Any] | None = None,
        metadata: dict[str, Any] | None = None,
        spotlight: bool | None = None,
        spotlight_number: int | None = None,
        simulcast: bool | None = None,
        data_channel_signaling: bool | None = None,
        ignore_disconnect_websocket: bool | None = None,
        # プロキシ設定
        proxy_url: str | None = None,
        proxy_username: str | None = None,
        proxy_password: str | None = None,
        # セキュリティ設定
        insecure: bool = False,
        client_cert: str | None = None,
        client_key: str | None = None,
        ca_cert: str | None = None,
        # HTTP サーバー設定
        http_port: int | None = None,
        http_host: str = "127.0.0.1",
        # パフォーマンス設定
        degradation_preference: Literal[
            "disabled", "maintain_framerate", "maintain_resolution", "balanced"
        ]
        | None = None,
        cpu_adaptation: bool | None = None,
        # Fake デバイス設定（デフォルトで fake を使用）
        fake_capture_device: bool = True,
        # オーディオデバイス設定
        audio_recording_device: str | None = None,
        audio_playout_device: str | None = None,
        # コーデック設定
        openh264: str | None = None,
        vp8_encoder: str | None = None,
        vp8_decoder: str | None = None,
        vp9_encoder: str | None = None,
        vp9_decoder: str | None = None,
        h264_encoder: str | None = None,
        h264_decoder: str | None = None,
        h265_encoder: str | None = None,
        h265_decoder: str | None = None,
        av1_encoder: str | None = None,
        av1_decoder: str | None = None,
        # ログレベル
        log_level: Literal["verbose", "info", "warning", "error"] | None = None,
        # その他のカスタム引数
        extra_args: list[str] | None = None,
        # 起動待機時間
        initial_wait: int | None = None,
        # Windows 調査用
        log_interval: float = 1.0,
    ) -> None:
        self._log_prefix = "[SumomoWindows]"
        self._platform = platform.system().lower()
        self._log(f"Detected platform: {self._platform}")

        if self._platform != "windows":
            self._log(
                f"Warning: SumomoWindows is intended for Windows, running on {self._platform}"
            )

        self._log_interval = log_interval
        self.process: subprocess.Popen[Any] | None = None
        self.http_port = http_port if http_port is not None else 0
        self.http_host = http_host
        self.initial_wait = initial_wait if initial_wait is not None else 2
        self._http_client: httpx.Client | None = None
        self._last_stats: list[dict[str, Any]] | None = None
        self._stdout_lines: list[str] = []
        self._stderr_lines: list[str] = []
        self._stdout_thread: threading.Thread | None = None
        self._stderr_thread: threading.Thread | None = None

        # すべての引数を保存
        self._kwargs: dict[str, Any] = {
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
            "insecure": insecure,
            "client_cert": client_cert,
            "client_key": client_key,
            "ca_cert": ca_cert,
            "http_port": http_port,
            "http_host": http_host,
            "degradation_preference": degradation_preference,
            "cpu_adaptation": cpu_adaptation,
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

        # 実行ファイルのパスを自動検出
        self.executable_path = self._get_sumomo_executable_path()
        self._log(f"Sumomo executable path: {self.executable_path}")

        sanitized = self._sanitize(self._kwargs)
        self._log(f"Initialized with sanitized config: {json.dumps(sanitized, indent=2)}")

    def _get_sumomo_executable_path(self) -> str:
        """ビルド済みの sumomo 実行ファイルのパスを自動検出"""
        project_root = Path(__file__).parent.parent
        build_dir = project_root / "examples" / "_build"

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
            target = available_targets[0]
            self._log(f"Auto-detected sumomo target: {target}")
        else:
            machine = platform.machine().lower()
            if system == "windows":
                preferred = ["windows_x86_64"]
            elif system == "darwin":
                if machine == "arm64" or machine == "aarch64":
                    preferred = ["macos_arm64", "macos_x86_64"]
                else:
                    preferred = ["macos_x86_64", "macos_arm64"]
            else:
                preferred = []

            target = None
            for pref in preferred:
                if pref in available_targets:
                    target = pref
                    self._log(
                        f"Auto-detected sumomo target: {target} (from {len(available_targets)} available)"
                    )
                    break

            if not target:
                target = available_targets[0]
                self._log(
                    f"Using first available target: {target} (available: {', '.join(available_targets)})"
                )

        sumomo_path = (
            project_root / "examples" / "_build" / target / "release" / "sumomo" / f"sumomo{exe_suffix}"
        )

        if not sumomo_path.exists():
            raise RuntimeError(
                f"sumomo executable not found at {sumomo_path}. "
                f"Please build with: python3 run.py build {target}"
            )

        return str(sumomo_path)

    def _log(self, message: str) -> None:
        timestamp = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime())
        print(f"{timestamp} {self._log_prefix} {message}", flush=True)

    def _read_stream(self, stream, lines_list: list[str], stream_name: str) -> None:
        """バックグラウンドスレッドで stdout/stderr を読み取る"""
        try:
            for line in stream:
                line = line.rstrip()
                if line:
                    lines_list.append(line)
                    self._log(f"{stream_name}: {line}")
        except Exception as e:
            self._log(f"Error reading {stream_name}: {e!r}")

    def _is_sensitive(self, key_path: str) -> bool:
        key_lower = key_path.lower()
        sensitive_keywords = ("secret", "token", "password", "key", "metadata")
        return any(keyword in key_lower for keyword in sensitive_keywords)

    def _sanitize(self, value: Any, key_path: str = "") -> Any:
        if self._is_sensitive(key_path):
            if isinstance(value, str):
                return f"<redacted len={len(value)}>"
            if isinstance(value, Iterable) and not isinstance(value, (str, bytes, bytearray)):
                return ["<redacted>" for _ in value]
            if isinstance(value, dict):
                return {k: "<redacted>" for k in value}
            return "<redacted>"

        if isinstance(value, dict):
            return {
                k: self._sanitize(v, f"{key_path}.{k}" if key_path else k) for k, v in value.items()
            }
        if isinstance(value, list):
            return [self._sanitize(v, key_path) for v in value]
        if isinstance(value, tuple):
            return tuple(self._sanitize(v, key_path) for v in value)
        if isinstance(value, str) and len(value) > 256:
            return f"{value[:128]}... (truncated, total_length={len(value)})"
        return value

    def _build_args(self, **kwargs: Any) -> list[str]:
        """コマンドライン引数を構築"""
        args = []

        # ログレベル
        if kwargs.get("log_level"):
            args.extend(["--log-level", kwargs["log_level"]])

        # 解像度
        if kwargs.get("resolution"):
            args.extend(["--resolution", kwargs["resolution"]])

        # HW MJPEG デコーダー（明示的に True が指定された場合のみ）
        if kwargs.get("hw_mjpeg_decoder") is True:
            args.extend(["--hw-mjpeg-decoder", "true"])

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
            metadata_json = json.dumps(kwargs["metadata"])
            # デバッグ用：metadata の実際の値をログ出力（一時的）
            # self._log(f"DEBUG: metadata JSON = {metadata_json}")
            args.extend(["--metadata", metadata_json])

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
        if kwargs.get("http_host"):
            args.extend(["--http-host", kwargs["http_host"]])

        # パフォーマンス設定
        if kwargs.get("degradation_preference"):
            args.extend(["--degradation-preference", kwargs["degradation_preference"]])
        if kwargs.get("cpu_adaptation") is not None:
            args.extend(["--cpu-adaptation", "true" if kwargs["cpu_adaptation"] else "false"])

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

    def __enter__(self) -> "SumomoWindows":
        """コンテキストマネージャーの開始"""
        self._log("Entering SumomoWindows context")
        try:
            # コマンドライン引数を構築
            args = self._build_args(**self._kwargs)

            # 起動コマンドを表示
            cmd = [self.executable_path] + args

            # 実行コマンドライン全体を表示（デバッグ用）
            display_cmd_parts = [self.executable_path]
            for i, arg in enumerate(args):
                # センシティブな情報をマスク
                if i > 0 and args[i - 1] in ["--metadata", "--proxy-password"]:
                    display_cmd_parts.append("<redacted>")
                else:
                    # スペースを含む引数は引用符で囲む
                    if " " in arg:
                        display_cmd_parts.append(f'"{arg}"')
                    else:
                        display_cmd_parts.append(arg)

            self._log("Executing command:")
            self._log(f"  {' '.join(display_cmd_parts)}")
            self._log(f"Arguments count: {len(args)}")

            # プロセスを起動
            self._log("Starting sumomo process...")
            try:
                self.process = subprocess.Popen(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
                self._log(f"Process started with PID: {self.process.pid}")

                # バックグラウンドスレッドで stdout/stderr をリアルタイムで読み取る
                if self.process.stdout:
                    self._stdout_thread = threading.Thread(
                        target=self._read_stream,
                        args=(self.process.stdout, self._stdout_lines, "STDOUT"),
                        daemon=True
                    )
                    self._stdout_thread.start()
                    self._log("Started stdout reader thread")

                if self.process.stderr:
                    self._stderr_thread = threading.Thread(
                        target=self._read_stream,
                        args=(self.process.stderr, self._stderr_lines, "STDERR"),
                        daemon=True
                    )
                    self._stderr_thread.start()
                    self._log("Started stderr reader thread")
            except FileNotFoundError:
                self._log(f"ERROR: Executable not found at {self.executable_path}")
                raise RuntimeError(
                    f"Sumomo executable not found at {self.executable_path}. "
                    f"Please build with: python3 run.py build <target>"
                )
            except Exception as e:
                self._log(f"ERROR: Failed to start process: {e!r}")
                raise RuntimeError(f"Failed to start sumomo process: {e}")

            # プロセスが起動して HTTP サーバーが利用可能になるまで待機
            if self.http_port is not None and self.http_port != 0:
                self._wait_for_startup(self.http_port, timeout=30, initial_wait=self.initial_wait)
            else:
                self._log(f"No HTTP port configured, waiting {self.initial_wait}s")
                if self.initial_wait > 0:
                    time.sleep(self.initial_wait)

            # Windows の場合、プロセスが早期終了していないか確認
            if self.process and self._platform == "windows":
                poll_result = self.process.poll()
                if poll_result is not None:
                    self._log(f"ERROR: Process exited early with code {poll_result}")
                    stderr_output = ""
                    if self.process.stderr:
                        # プロセスが終了しているので read() はブロックしない
                        try:
                            stderr_output = self.process.stderr.read()
                            self._log(f"Stderr: {stderr_output}")
                        except Exception as e:
                            self._log(f"WARNING: Failed to read stderr: {e!r}")
                    raise RuntimeError(
                        f"sumomo.exe exited unexpectedly with code {poll_result}\n"
                        f"Stderr: {stderr_output}"
                    )
                else:
                    self._log(f"Process is still running (PID: {self.process.pid})")

            # HTTP クライアントを作成（短めのタイムアウト）
            self._http_client = httpx.Client(timeout=5.0)
            self._log("HTTP client initialized with 5s timeout")

            self._log("SumomoWindows context entered successfully")
            return self
        except Exception as e:
            self._log(f"ERROR during context entry: {e!r}")
            if self.process:
                self._log(f"Cleaning up process (PID: {self.process.pid})")
            self._cleanup()
            raise

    def __exit__(self, exc_type, exc_val, _exc_tb) -> Literal[False]:
        """コンテキストマネージャーの終了"""
        self._log("Exiting SumomoWindows context")
        if exc_type:
            self._log(f"Exception during context: {exc_type.__name__}: {exc_val}")

        # HTTP クライアントをクリーンアップ
        if self._http_client:
            self._log("Closing HTTP client")
            self._http_client.close()
            self._http_client = None

        self._cleanup()
        self._log("SumomoWindows context exited")
        return False

    def _wait_for_startup(self, http_port: int, timeout: int = 30, initial_wait: int = 2) -> None:
        """プロセスが起動して HTTP サーバーが利用可能になるまで待機"""
        if not self.process:
            raise RuntimeError("Process not started")

        # プロセスが完全に起動するまで少し待機
        if initial_wait > 0:
            self._log(f"Initial wait: {initial_wait}s")
            time.sleep(initial_wait)

        self._log(f"Waiting for HTTP endpoint on port {http_port} (timeout: {timeout}s)")
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
                    self._log(f"ERROR: {error_msg}")
                    if self.process.stderr:
                        # プロセスが終了しているので read() はブロックしない
                        try:
                            stderr_output = self.process.stderr.read()
                            if stderr_output:
                                self._log(f"Stderr: {stderr_output}")
                                error_msg += f"\nStderr output:\n{stderr_output}"
                        except Exception as e:
                            self._log(f"WARNING: Failed to read stderr: {e!r}")
                    raise RuntimeError(error_msg)

                # 各試行前にプロセスの状態を確認
                poll_result_before = self.process.poll()
                self._log(f"Attempt {attempt}: Process state before HTTP check: poll={poll_result_before}")

                # Windows で netstat を使ってポートがリッスンされているか確認（最初の数回のみ）
                if attempt <= 3:
                    try:
                        import subprocess as sp
                        netstat_result = sp.run(
                            ["netstat", "-an"],
                            capture_output=True,
                            text=True,
                            timeout=2
                        )
                        # ポート番号を含む行を探す
                        port_lines = [
                            line for line in netstat_result.stdout.splitlines()
                            if f":{http_port}" in line and "LISTENING" in line
                        ]
                        if port_lines:
                            self._log(f"Attempt {attempt}: Port {http_port} is LISTENING: {port_lines[0]}")
                        else:
                            self._log(f"Attempt {attempt}: Port {http_port} is NOT listening")
                    except Exception as e:
                        self._log(f"Attempt {attempt}: Failed to check netstat: {e!r}")

                # HTTP エンドポイントをチェック
                try:
                    # 0.0.0.0 でバインドしている場合は localhost で接続
                    connect_host = "localhost" if self.http_host == "0.0.0.0" else self.http_host
                    url = f"http://{connect_host}:{http_port}/stats"
                    self._log(f"Attempt {attempt}: Checking {url} (server bound to {self.http_host})")
                    response = client.get(url, timeout=5)
                    if response.status_code == 200:
                        elapsed = time.time() - start_time
                        self._log(f"HTTP endpoint ready after {elapsed:.1f}s")
                        return
                    else:
                        self._log(f"Attempt {attempt}: Got status code {response.status_code}")
                except httpx.ConnectError as e:
                    elapsed = time.time() - start_time
                    self._log(f"Attempt {attempt}: Connection failed ({elapsed:.1f}s elapsed): {e!r}")
                except httpx.ConnectTimeout:
                    self._log(f"Attempt {attempt}: Connection timeout")
                except Exception as e:
                    self._log(f"Attempt {attempt}: Unexpected error: {e!r}")

                # 各試行後にもプロセスの状態を確認
                poll_result_after = self.process.poll()
                if poll_result_after != poll_result_before:
                    self._log(f"WARNING: Process state changed during attempt: {poll_result_before} -> {poll_result_after}")

                # プロセスが終了していた場合、即座に終了して stderr/stdout を確認
                if poll_result_after is not None:
                    self._log(f"ERROR: Process exited with code {poll_result_after} during startup")
                    # この時点でプロセスは既に終了しているため、read() はブロックしない
                    error_msg = f"Process exited with code {poll_result_after} during startup"

                    stderr_output = ""
                    stdout_output = ""
                    if self.process.stderr:
                        try:
                            stderr_output = self.process.stderr.read()
                            if stderr_output:
                                self._log(f"Stderr output:\n{stderr_output}")
                                error_msg += f"\n\nStderr:\n{stderr_output}"
                        except Exception as e:
                            self._log(f"WARNING: Failed to read stderr: {e!r}")

                    if self.process.stdout:
                        try:
                            stdout_output = self.process.stdout.read()
                            if stdout_output:
                                self._log(f"Stdout output:\n{stdout_output}")
                                error_msg += f"\n\nStdout:\n{stdout_output}"
                        except Exception as e:
                            self._log(f"WARNING: Failed to read stdout: {e!r}")

                    raise RuntimeError(error_msg)

                # 次の試行まで1秒待機
                time.sleep(1)

            # タイムアウト
            if self.process:
                self._log(f"ERROR: Timeout after {timeout}s (PID: {self.process.pid})")
                poll_result = self.process.poll()
                self._log(f"Process state at timeout: poll={poll_result}")

                # プロセスがまだ実行中の場合、強制終了して stderr/stdout を読む
                if poll_result is None:
                    self._log("Process is still running, terminating to read output...")
                    self.process.terminate()
                    try:
                        self.process.wait(timeout=2)
                        self._log("Process terminated")
                    except subprocess.TimeoutExpired:
                        self._log("Process did not terminate, killing...")
                        self.process.kill()
                        self.process.wait()
                        self._log("Process killed")

                # プロセスが終了したので stderr/stdout を読む
                error_msg = f"sumomo process failed to start within {timeout} seconds"

                stderr_output = ""
                stdout_output = ""
                if self.process.stderr:
                    try:
                        stderr_output = self.process.stderr.read()
                        if stderr_output:
                            self._log(f"Stderr output (last 2000 chars):\n{stderr_output[-2000:]}")
                            error_msg += f"\n\nStderr (last 2000 chars):\n{stderr_output[-2000:]}"
                    except Exception as e:
                        self._log(f"WARNING: Failed to read stderr: {e!r}")

                if self.process.stdout:
                    try:
                        stdout_output = self.process.stdout.read()
                        if stdout_output:
                            self._log(f"Stdout output (last 2000 chars):\n{stdout_output[-2000:]}")
                            error_msg += f"\n\nStdout (last 2000 chars):\n{stdout_output[-2000:]}"
                    except Exception as e:
                        self._log(f"WARNING: Failed to read stdout: {e!r}")

                self._cleanup()
                raise RuntimeError(error_msg)

            self._cleanup()
            raise RuntimeError(f"sumomo process failed to start within {timeout} seconds")

    def _cleanup(self) -> None:
        """プロセスをクリーンアップ"""
        if self.process:
            pid = self.process.pid
            self._log(f"Terminating process (PID: {pid})")

            # 標準的な終了シグナルを送信
            self.process.terminate()
            try:
                # 短めのタイムアウトで終了を待つ
                self.process.wait(timeout=5)
                self._log(f"Process (PID: {pid}) terminated gracefully")
            except subprocess.TimeoutExpired:
                # タイムアウトした場合は強制終了
                self._log(f"Force killing process (PID: {pid})")
                self.process.kill()
                self.process.wait()
                self._log(f"Process (PID: {pid}) killed")

            # stderr の残りを読み取ってリソースを解放
            if self.process.stderr:
                try:
                    self.process.stderr.close()
                except Exception:
                    pass
            if self.process.stdout:
                try:
                    self.process.stdout.close()
                except Exception:
                    pass

            self.process = None
            self._log("Process cleanup completed")

            # プロセス終了後の短い待機（リソース解放のため）
            time.sleep(0.2)

    def get_stats(self) -> list[dict[str, Any]]:
        """統計情報を取得"""
        self._log("Fetching stats from Sumomo")

        # HTTP クライアントとポートの確認
        if not self._http_client:
            self._log("ERROR: HTTP client not initialized")
            raise RuntimeError("HTTP client not initialized")

        if self.http_port is None or self.http_port == 0:
            self._log("ERROR: HTTP port not configured")
            raise RuntimeError("HTTP port not configured")

        # プロセスの状態確認
        if self.process:
            poll_result = self.process.poll()
            if poll_result is not None:
                self._log(f"ERROR: Process has exited with code {poll_result}")
                stderr_output = ""
                if self.process.stderr:
                    try:
                        stderr_output = self.process.stderr.read()
                        self._log(f"Stderr: {stderr_output}")
                    except Exception:
                        pass
                raise RuntimeError(
                    f"sumomo.exe has crashed (exit code: {poll_result})\n" f"Stderr: {stderr_output}"
                )
            else:
                self._log(f"Process is running (PID: {self.process.pid})")
        else:
            self._log("WARNING: No process object found")

        # HTTP リクエストの詳細をログ
        # 0.0.0.0 でバインドしている場合は localhost で接続
        connect_host = "localhost" if self.http_host == "0.0.0.0" else self.http_host
        url = f"http://{connect_host}:{self.http_port}/stats"
        self._log(f"Requesting stats from {url} (server bound to {self.http_host})")

        try:
            self._log(f"Sending GET request to {url}")
            response = self._http_client.get(url)
            self._log(f"Response received: status={response.status_code}")
            response.raise_for_status()
            stats = response.json()
            self._last_stats = stats
            self._log(f"Stats fetched successfully, entries={len(stats)}")
            return stats
        except httpx.TimeoutException as exc:
            self._log(f"ERROR: Request timed out: {exc!r}")
            # プロセスの状態を確認
            if self.process:
                poll_result = self.process.poll()
                self._log(f"Process state after timeout: poll={poll_result}")
                if poll_result is not None:
                    self._log("Process has exited!")
                else:
                    self._log("Process is still running but not responding")
            raise RuntimeError(f"HTTP request timed out: {exc}") from exc
        except Exception as exc:
            self._log(f"ERROR: Failed to get stats: {exc!r}")
            # プロセスの最終状態を確認
            if self.process:
                poll_result = self.process.poll()
                self._log(f"Process final state: poll={poll_result}")
                if poll_result is not None:
                    stderr_output = ""
                    stdout_output = ""
                    if self.process.stderr:
                        try:
                            stderr_output = self.process.stderr.read()
                            self._log(f"Stderr (last 1000 chars): {stderr_output[-1000:]}")
                        except Exception:
                            pass
                    if self.process.stdout:
                        try:
                            stdout_output = self.process.stdout.read()
                            self._log(f"Stdout (last 1000 chars): {stdout_output[-1000:]}")
                        except Exception:
                            pass
                    raise RuntimeError(
                        f"sumomo.exe crashed while getting stats (exit code: {poll_result})\n"
                        f"Stderr: {stderr_output}\n"
                        f"Stdout: {stdout_output}\n"
                        f"Original error: {exc}"
                    )
            raise

    def wait_for_stats(self, attempts: int = 10, delay: float = 1.0) -> list[dict[str, Any]]:
        """統計情報を取得できるまでリトライ"""
        self._log(f"Waiting for stats: attempts={attempts}, delay={delay}s")
        last_error: Exception | None = None
        for attempt in range(1, attempts + 1):
            try:
                stats = self.get_stats()
                if stats:
                    self._log(f"Stats retrieved on attempt {attempt}")
                    return stats
                self._log(f"Attempt {attempt}: stats list is empty")
            except Exception as exc:
                last_error = exc
                self._log(f"Attempt {attempt} failed with error: {exc!r}")

            if attempt < attempts:
                self._log(f"Waiting {delay}s before next attempt")
                time.sleep(delay)

        if last_error:
            self._log(f"Failed after {attempts} attempts, last error: {last_error!r}")
            raise RuntimeError("Failed to retrieve stats") from last_error
        self._log(f"Failed after {attempts} attempts, no stats available")
        raise RuntimeError("Failed to retrieve stats within expected attempts")

    def diagnostics(self) -> dict[str, Any]:
        """診断情報を取得"""
        self._log("Generating diagnostics snapshot")

        process_info = None
        if self.process:
            poll_result = self.process.poll()
            process_info = {
                "pid": self.process.pid,
                "returncode": self.process.returncode,
                "running": poll_result is None,
            }
            self._log(f"Process info: PID={self.process.pid}, running={poll_result is None}")

        snapshot = {
            "platform": self._platform,
            "config": self._sanitize(self._kwargs),
            "process": process_info,
            "executable_path": self.executable_path,
            "http_port": self.http_port,
            "http_host": self.http_host,
        }

        if isinstance(self._last_stats, list) and self._last_stats:
            snapshot["last_stats_sample"] = self._sanitize(
                self._last_stats[: min(3, len(self._last_stats))], key_path="stats"
            )
            self._log(f"Last stats available: {len(self._last_stats)} entries")
        else:
            snapshot["last_stats_sample"] = None
            self._log("No stats available")

        self._log(f"Diagnostics snapshot: {json.dumps(snapshot, indent=2)}")
        return snapshot
