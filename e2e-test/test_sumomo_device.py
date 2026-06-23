"""Sumomo の --list-devices と実機キャプチャの E2E テスト

nvidia self-hosted runner で実行する想定。GitHub-hosted runner で実機キャプチャの
テストを動かしたい場合は別途 CI ワークフロー側で実行を制御する。
テストコード側では実行環境を判定する仕組み (skipif や環境変数) は持たない。
"""

import re
import subprocess
import time
from typing import Any

import pytest

from helper import get_outbound_rtp, get_transport
from sumomo import Sumomo, get_device_lists, get_sumomo_executable_path


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

    --list-devices で列挙されたデバイスを明示的に指定してキャプチャを行う。
    """
    # --list-devices で列挙されたデバイスを取得する
    device_lists = get_device_lists()

    assert len(device_lists.audio_recording) > 0, "音声入力デバイスが 1 つも見つからない"
    assert len(device_lists.video) > 0, "ビデオデバイスが 1 つも見つからない"

    # 録音デバイス: "default:" プレフィクスが付いたデバイス名をそのまま使用する
    # "default:" は PulseAudio のデフォルトデバイス指定であり、そのまま指定する必要がある
    recording_device = device_lists.audio_recording[0]

    # ビデオデバイス: V4L2 のデバイスパスを指定する
    video_device = device_lists.video[0][0]

    print(f"音声録音デバイス: {recording_device}")
    print(f"ビデオデバイス: {video_device}")

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
        # 列挙されたデバイスを明示的に指定する
        audio_recording_device=recording_device,
        video_device=video_device,
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


def pytest_generate_tests(metafunc):
    """音声録音デバイスを個別にテストするためのパラメータを生成する"""
    if "audio_recording_device" in metafunc.fixturenames:
        device_lists = get_device_lists()
        metafunc.parametrize("audio_recording_device", device_lists.audio_recording)


def test_audio_recording_device(
    sora_settings, free_port, audio_recording_device, capfd
):
    """--audio-recording-device で指定した音声録音デバイスから音声が送信されることを確認する

    --list-devices で列挙された各音声録音デバイスを個別に指定し、それぞれで
    音声フレームが送信されることを確認する。
    さらに標準エラー出力を確認し、指定したデバイスが実際に選択されていることを検証する。
    """
    print(f"音声録音デバイスをテスト: {audio_recording_device}")

    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        # 実機キャプチャを行うため fake_capture_device を明示的に無効化する
        fake_capture_device=False,
        # 音声録音デバイスのみを対象にするため映像は無効化する
        video=False,
        audio=True,
        # 列挙された音声録音デバイスを明示的に指定する
        audio_recording_device=audio_recording_device,
        # 標準エラー出力からデバイス選択ログを取得するため info レベルを有効にする
        log_level="info",
    ) as s:
        # 接続確立後、音声フレームが流れるまで十分な待機時間を確保する
        time.sleep(3)

        stats: list[dict[str, Any]] = s.get_stats()
        # キャプチャ失敗時は HTTP サーバーが起動しないか stats が空になる
        assert stats, (
            f"デバイス {audio_recording_device} で get_stats() の結果が空のため、"
            "音声キャプチャに失敗した可能性がある"
        )

        # 音声が送信されていることを確認する
        audio_outbound = get_outbound_rtp(stats, "audio")
        assert audio_outbound is not None, (
            f"デバイス {audio_recording_device} で audio の outbound-rtp が見つからない"
        )
        assert audio_outbound["packetsSent"] > 0, (
            f"デバイス {audio_recording_device} で audio パケットが 1 つも送信されていない"
        )

        # DTLS が確立していることを確認する
        transport = get_transport(stats)
        assert transport is not None, "transport が見つからない"
        assert transport["dtlsState"] == "connected", (
            f"DTLS が確立していない: dtlsState={transport['dtlsState']}"
        )

    # sumomo プロセス終了後に標準エラー出力を取得する
    captured = capfd.readouterr()

    # 実際に選択された音声録音デバイス名をログから抽出する
    m = re.search(
        r"Succeeded SetRecordingDevice:.* name=(.+?) guid=", captured.err
    )
    assert m is not None, (
        f"デバイス {audio_recording_device} で SetRecordingDevice 成功ログが見つからない"
    )
    selected_device = m.group(1).strip()
    assert selected_device == audio_recording_device, (
        f"指定したデバイス {audio_recording_device} が選択されていない。 "
        f"実際に選択されたデバイス: {selected_device}"
    )


def test_default_audio_recording_device(sora_settings, free_port, capfd):
    """録音デバイス名を指定しない場合、デフォルトデバイスが選択されることを確認する

    デバイスリストの先頭をデフォルトデバイスとして扱い、明示的に指定しなくても
    同じデバイスが選択されることを検証する。
    """
    device_lists = get_device_lists()
    if len(device_lists.audio_recording) == 0:
        pytest.skip("音声録音デバイスが 1 つも見つからない")

    default_device = device_lists.audio_recording[0]

    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        # 実機キャプチャを行うため fake_capture_device を明示的に無効化する
        fake_capture_device=False,
        # 音声録音デバイスのみを対象にするため映像は無効化する
        video=False,
        audio=True,
        # 標準エラー出力からデバイス選択ログを取得するため info レベルを有効にする
        log_level="info",
    ) as s:
        # 接続確立後、音声フレームが流れるまで十分な待機時間を確保する
        time.sleep(3)

        stats: list[dict[str, Any]] = s.get_stats()
        # キャプチャ失敗時は HTTP サーバーが起動しないか stats が空になる
        assert stats, "get_stats() の結果が空のため、音声キャプチャに失敗した可能性がある"

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

    # sumomo プロセス終了後に標準エラー出力を取得する
    captured = capfd.readouterr()

    # 実際に選択された音声録音デバイス名をログから抽出する
    m = re.search(r"Succeeded SetRecordingDevice:.* name=(.+?) guid=", captured.err)
    assert m is not None, "SetRecordingDevice 成功ログが見つからない"
    selected_device = m.group(1).strip()
    assert selected_device == default_device, (
        f"デフォルトデバイス {default_device} が選択されていない。 "
        f"実際に選択されたデバイス: {selected_device}"
    )


def test_invalid_audio_recording_device(sora_settings, free_port, capfd):
    """存在しない録音デバイス名を指定した場合、デフォルトデバイスにフォールバックすることを確認する

    無効なデバイス名を指定しても接続が成功し、かつデフォルトデバイスが
    選択されることを検証する。
    """
    device_lists = get_device_lists()
    if len(device_lists.audio_recording) == 0:
        pytest.skip("音声録音デバイスが 1 つも見つからない")

    default_device = device_lists.audio_recording[0]
    # 空文字列は sumomo 側で無視されるため、実在しない非空文字列を指定する
    invalid_device = "__nonexistent_device_for_test__"

    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        # 実機キャプチャを行うため fake_capture_device を明示的に無効化する
        fake_capture_device=False,
        # 音声録音デバイスのみを対象にするため映像は無効化する
        video=False,
        audio=True,
        # 実在しない録音デバイス名を指定する
        audio_recording_device=invalid_device,
        # 標準エラー出力からデバイス選択ログを取得するため info レベルを有効にする
        log_level="info",
    ) as s:
        # 接続確立後、音声フレームが流れるまで十分な待機時間を確保する
        time.sleep(3)

        stats: list[dict[str, Any]] = s.get_stats()
        # キャプチャ失敗時は HTTP サーバーが起動しないか stats が空になる
        assert stats, "get_stats() の結果が空のため、音声キャプチャに失敗した可能性がある"

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

    # sumomo プロセス終了後に標準エラー出力を取得する
    captured = capfd.readouterr()

    # 実際に選択された音声録音デバイス名をログから抽出する
    m = re.search(r"Succeeded SetRecordingDevice:.* name=(.+?) guid=", captured.err)
    assert m is not None, "SetRecordingDevice 成功ログが見つからない"
    selected_device = m.group(1).strip()
    assert selected_device == default_device, (
        f"無効なデバイス名を指定した場合のフォールバック先 {default_device} が選択されていない。 "
        f"実際に選択されたデバイス: {selected_device}"
    )
