"""Sumomo の --list-devices と実機キャプチャの E2E テスト

nvidia self-hosted runner で実行する想定。GitHub-hosted runner で実機キャプチャの
テストを動かしたい場合は別途 CI ワークフロー側で実行を制御する。
テストコード側では実行環境を判定する仕組み (skipif や環境変数) は持たない。
"""

import subprocess
import time
from typing import Any

from helper import get_outbound_rtp, get_transport
from sumomo import Sumomo, get_sumomo_executable_path


def test_list_devices():
    """--list-devices で音声・ビデオの 3 セクションが出力されることを確認する"""
    sumomo_path = get_sumomo_executable_path()

    # --list-devices は ListDevices() を実行して return 0 するだけなので Sora 接続は不要
    # デバイス列挙がハングしてもテストが永遠にブロックされないよう timeout を必ず指定する
    result = subprocess.run(
        [sumomo_path, "--list-devices"],
        capture_output=True,
        text=True,
        timeout=10,
    )

    # 終了コードが 0 であることを確認する
    assert result.returncode == 0, (
        f"--list-devices が異常終了した\n"
        f"returncode: {result.returncode}\n"
        f"stdout:\n{result.stdout}\n"
        f"stderr:\n{result.stderr}"
    )

    # 3 つのセクションヘッダーが含まれることを確認する
    # デバイス名の出力形式はプラットフォーム間で異なるためヘッダーのみ検証する
    assert "=== Available audio input devices ===" in result.stdout, (
        f"音声入力デバイスのセクションヘッダーが見つからない\nstdout:\n{result.stdout}"
    )
    assert "=== Available audio output devices ===" in result.stdout, (
        f"音声出力デバイスのセクションヘッダーが見つからない\nstdout:\n{result.stdout}"
    )
    assert "=== Available video devices ===" in result.stdout, (
        f"ビデオデバイスのセクションヘッダーが見つからない\nstdout:\n{result.stdout}"
    )


def test_capture_device(sora_settings, free_port):
    """実機カメラ・マイクから取得した映像・音声が送信されることを確認する

    デバイス名は指定せず Sumomo のデフォルト (最初に見つかったデバイス) を利用する。
    特定デバイスを指定しないことでプラットフォーム依存の文字列パースを避けつつ
    「実機からキャプチャして送信できる」ことだけを検証する。
    """
    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        # 実機キャプチャを行うため fake_capture_device を明示的に無効化する
        fake_capture_device=False,
        video=True,
        audio=True,
        # デフォルト解像度への暗黙依存を避けるため明示する
        resolution="VGA",
    ) as s:
        # 接続確立後、映像/音声フレームが流れるまで十分な待機時間を確保する
        time.sleep(3)

        stats: list[dict[str, Any]] = s.get_stats()
        # キャプチャ失敗時は HTTP サーバーが起動しないか stats が空になる
        assert stats, "get_stats() の結果が空のため、実機キャプチャに失敗した可能性がある"

        # 映像が送信されていることを確認する
        video_outbound = get_outbound_rtp(stats, "video")
        assert video_outbound is not None, "video の outbound-rtp が見つからない"
        # 実機の解像度は要求解像度と異なる場合があるため正の値であることのみ確認する
        assert video_outbound["frameWidth"] > 0, (
            f"frameWidth が 0 以下: {video_outbound['frameWidth']}"
        )
        assert video_outbound["frameHeight"] > 0, (
            f"frameHeight が 0 以下: {video_outbound['frameHeight']}"
        )
        assert video_outbound["packetsSent"] > 0, "video パケットが 1 つも送信されていない"

        # 音声が送信されていることを確認する
        audio_outbound = get_outbound_rtp(stats, "audio")
        assert audio_outbound is not None, "audio の outbound-rtp が見つからない"
        assert audio_outbound["packetsSent"] > 0, "audio パケットが 1 つも送信されていない"

        # DTLS が確立していることを確認する
        transport = get_transport(stats)
        assert transport is not None, "transport が見つからない"
        assert transport["dtlsState"] == "connected", (
            f"DTLS が確立していない: dtlsState={transport['dtlsState']}"
        )
