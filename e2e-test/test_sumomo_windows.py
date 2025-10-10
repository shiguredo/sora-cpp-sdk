"""Windows 上で SumomoWindows を使った E2E テスト"""

import time
from typing import Any

import pytest

from sumomo_windows import SumomoWindows


def get_outbound_rtp(stats: list[dict[str, Any]], kind: str) -> dict[str, Any] | None:
    """outbound-rtp 統計情報を取得する"""
    return next(
        (stat for stat in stats if stat.get("type") == "outbound-rtp" and stat.get("kind") == kind),
        None,
    )


def get_codec(stats: list[dict[str, Any]], mime_type: str) -> dict[str, Any] | None:
    """codec 統計情報を取得する"""
    return next(
        (
            stat
            for stat in stats
            if stat.get("type") == "codec" and mime_type in stat.get("mimeType", "")
        ),
        None,
    )


def get_transport(stats: list[dict[str, Any]]) -> dict[str, Any] | None:
    """transport 統計情報を取得する"""
    return next((stat for stat in stats if stat.get("type") == "transport"), None)


@pytest.mark.parametrize("video_codec_type", ["VP8", "VP9", "AV1"])
def test_sumomo_windows_sendonly(sora_settings, free_port, video_codec_type):
    """SumomoWindows を使った sendonly テスト"""
    with SumomoWindows(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        audio=True,
        video=True,
        audio_codec_type="OPUS",
        video_codec_type=video_codec_type,
        log_level="verbose",
    ) as s:
        time.sleep(3)

        # Sender の統計情報を確認
        sender_stats: list[dict[str, Any]] = s.get_stats()
        assert sender_stats is not None
        assert isinstance(sender_stats, list)
        assert len(sender_stats) > 0

        # outbound-rtp を確認
        sender_audio_outbound = get_outbound_rtp(sender_stats, "audio")
        assert sender_audio_outbound is not None
        assert sender_audio_outbound["packetsSent"] > 0
        assert sender_audio_outbound["bytesSent"] > 0

        sender_video_outbound = get_outbound_rtp(sender_stats, "video")
        assert sender_video_outbound is not None
        assert sender_video_outbound["packetsSent"] > 0
        assert sender_video_outbound["bytesSent"] > 0

        # audio codec を確認
        sender_audio_codec = get_codec(sender_stats, "audio/opus")
        assert sender_audio_codec is not None
        assert sender_audio_codec["clockRate"] == 48000
        assert sender_audio_outbound["codecId"] == sender_audio_codec["id"]

        # video codec を確認
        sender_video_codec = get_codec(sender_stats, f"video/{video_codec_type}")
        assert sender_video_codec is not None
        assert sender_video_codec["clockRate"] == 90000
        assert sender_video_codec["mimeType"] == f"video/{video_codec_type}"
        assert sender_video_outbound["codecId"] == sender_video_codec["id"]

        # transport を確認
        sender_transport = get_transport(sender_stats)
        assert sender_transport is not None
        assert sender_transport["dtlsState"] == "connected"
