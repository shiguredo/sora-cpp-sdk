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


def get_inbound_rtp(stats: list[dict[str, Any]], kind: str) -> dict[str, Any] | None:
    """inbound-rtp 統計情報を取得する"""
    return next(
        (stat for stat in stats if stat.get("type") == "inbound-rtp" and stat.get("kind") == kind),
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
def test_sumomo_windows_sendonly_recvonly(sora_settings, port_allocator, video_codec_type):
    """SumomoWindows を使った sendonly と recvonly のペアテスト"""
    with SumomoWindows(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=next(port_allocator),
        audio=True,
        video=True,
        audio_codec_type="OPUS",
        video_codec_type=video_codec_type,
        log_level="verbose",
    ) as s:
        with SumomoWindows(
            signaling_url=sora_settings.signaling_url,
            channel_id=sora_settings.channel_id,
            role="recvonly",
            metadata=sora_settings.metadata,
            http_port=next(port_allocator),
            audio=True,
            video=True,
            log_level="verbose",
        ) as r:
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

            # Receiver の統計情報を確認
            receiver_stats: list[dict[str, Any]] = r.get_stats()
            assert receiver_stats is not None
            assert isinstance(receiver_stats, list)
            assert len(receiver_stats) > 0

            # inbound-rtp を確認
            receiver_audio_inbound = get_inbound_rtp(receiver_stats, "audio")
            assert receiver_audio_inbound is not None
            assert receiver_audio_inbound["packetsReceived"] > 0
            assert receiver_audio_inbound["bytesReceived"] > 0

            receiver_video_inbound = get_inbound_rtp(receiver_stats, "video")
            assert receiver_video_inbound is not None
            assert receiver_video_inbound["packetsReceived"] > 0
            assert receiver_video_inbound["bytesReceived"] > 0

            # audio codec を確認
            receiver_audio_codec = get_codec(receiver_stats, "audio/opus")
            assert receiver_audio_codec is not None

            # video codec を確認
            receiver_video_codec = get_codec(receiver_stats, f"video/{video_codec_type}")
            assert receiver_video_codec is not None
            assert receiver_video_codec["mimeType"] == f"video/{video_codec_type}"

            # transport を確認
            sender_transport = get_transport(sender_stats)
            receiver_transport = get_transport(receiver_stats)
            assert sender_transport is not None
            assert receiver_transport is not None
            assert sender_transport["dtlsState"] == "connected"
            assert receiver_transport["dtlsState"] == "connected"
