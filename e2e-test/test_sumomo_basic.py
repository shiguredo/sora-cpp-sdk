"""Sumomo の基本的な E2E テスト"""

import time

import pytest

from helper import get_codec, get_inbound_rtp, get_outbound_rtp, get_transport
from sumomo import Sumomo


@pytest.mark.parametrize("video_codec_type", ["VP8", "VP9", "AV1"])
def test_sumomo_sendonly_recvonly(sora_settings, port_allocator, video_codec_type):
    """sendonly と recvonly のペアでの統合テスト"""
    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=next(port_allocator),
        audio=True,
        video=True,
        audio_codec_type="OPUS",
        video_codec_type=video_codec_type,
    ) as s:
        with Sumomo(
            signaling_url=sora_settings.signaling_url,
            channel_id=sora_settings.channel_id,
            role="recvonly",
            metadata=sora_settings.metadata,
            http_port=next(port_allocator),
            audio=True,
            video=True,
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


@pytest.mark.parametrize("video_codec_type", ["VP8", "VP9", "AV1"])
def test_sumomo_sendrecv_pair(sora_settings, port_allocator, video_codec_type):
    """sendrecv モードのペアテスト（送受信の双方向通信）"""
    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendrecv",
        metadata=sora_settings.metadata,
        http_port=next(port_allocator),
        audio=True,
        video=True,
        audio_codec_type="OPUS",
        video_codec_type=video_codec_type,
    ) as c1:
        with Sumomo(
            signaling_url=sora_settings.signaling_url,
            channel_id=sora_settings.channel_id,
            role="sendrecv",
            metadata=sora_settings.metadata,
            http_port=next(port_allocator),
            audio=True,
            video=True,
        ) as c2:
            time.sleep(3)

            # Client1 の統計情報を確認
            client1_stats = c1.get_stats()
            assert client1_stats is not None

            # outbound-rtp を確認
            client1_audio_outbound = get_outbound_rtp(client1_stats, "audio")
            assert client1_audio_outbound is not None
            assert client1_audio_outbound["packetsSent"] > 0
            assert client1_audio_outbound["bytesSent"] > 0

            client1_video_outbound = get_outbound_rtp(client1_stats, "video")
            assert client1_video_outbound is not None
            assert client1_video_outbound["packetsSent"] > 0
            assert client1_video_outbound["bytesSent"] > 0

            # inbound-rtp を確認
            client1_audio_inbound = get_inbound_rtp(client1_stats, "audio")
            assert client1_audio_inbound is not None
            assert client1_audio_inbound["packetsReceived"] > 0
            assert client1_audio_inbound["bytesReceived"] > 0

            client1_video_inbound = get_inbound_rtp(client1_stats, "video")
            assert client1_video_inbound is not None
            assert client1_video_inbound["packetsReceived"] > 0
            assert client1_video_inbound["bytesReceived"] > 0

            # audio codec を確認
            client1_audio_codec = get_codec(client1_stats, "audio/opus")
            assert client1_audio_codec is not None
            assert client1_audio_codec["clockRate"] == 48000

            # video codec を確認
            client1_video_codec = get_codec(client1_stats, f"video/{video_codec_type}")
            assert client1_video_codec is not None
            assert client1_video_codec["clockRate"] == 90000
            assert client1_video_codec["mimeType"] == f"video/{video_codec_type}"

            # transport を確認
            client1_transport = get_transport(client1_stats)
            assert client1_transport is not None
            assert client1_transport["dtlsState"] == "connected"

            # Client2 の統計情報を確認
            client2_stats = c2.get_stats()
            assert client2_stats is not None

            # outbound-rtp を確認
            client2_audio_outbound = get_outbound_rtp(client2_stats, "audio")
            assert client2_audio_outbound is not None
            assert client2_audio_outbound["packetsSent"] > 0
            assert client2_audio_outbound["bytesSent"] > 0

            client2_video_outbound = get_outbound_rtp(client2_stats, "video")
            assert client2_video_outbound is not None
            assert client2_video_outbound["packetsSent"] > 0
            assert client2_video_outbound["bytesSent"] > 0

            # inbound-rtp を確認
            client2_audio_inbound = get_inbound_rtp(client2_stats, "audio")
            assert client2_audio_inbound is not None
            assert client2_audio_inbound["packetsReceived"] > 0
            assert client2_audio_inbound["bytesReceived"] > 0

            client2_video_inbound = get_inbound_rtp(client2_stats, "video")
            assert client2_video_inbound is not None
            assert client2_video_inbound["packetsReceived"] > 0
            assert client2_video_inbound["bytesReceived"] > 0

            # audio codec を確認
            client2_audio_codec = get_codec(client2_stats, "audio/opus")
            assert client2_audio_codec is not None

            # video codec を確認
            client2_video_codec = get_codec(client2_stats, f"video/{video_codec_type}")
            assert client2_video_codec is not None
            assert client2_video_codec["mimeType"] == f"video/{video_codec_type}"

            # transport を確認
            client2_transport = get_transport(client2_stats)
            assert client2_transport is not None
            assert client2_transport["dtlsState"] == "connected"


@pytest.mark.parametrize(
    "resolution,bitrate,width,height",
    [
        ("QVGA", 500, 320, 240),
        ("VGA", 1000, 640, 480),
        ("HD", 2500, 1280, 720),
        ("FHD", 5000, 1920, 1072),
    ],
)
def test_sumomo_resolutions(sora_settings, free_port, resolution, bitrate, width, height):
    """異なる解像度での接続テスト"""
    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        audio=True,
        video=True,
        video_codec_type="VP8",
        video_bit_rate=bitrate,
        resolution=resolution,
    ) as sumomo:
        time.sleep(3)

        stats = sumomo.get_stats()
        assert stats is not None

        # outbound-rtp を確認
        video_outbound = get_outbound_rtp(stats, "video")
        assert video_outbound is not None
        assert video_outbound["frameWidth"] == width
        assert video_outbound["frameHeight"] == height


def test_sumomo_simulcast(sora_settings, free_port):
    """サイマルキャスト有効時の接続テスト"""
    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        audio=True,
        video=True,
        simulcast=True,
        video_codec_type="VP8",
    ) as sumomo:
        stats = sumomo.get_stats()
        assert stats is not None


def test_sumomo_data_channel_signaling(sora_settings, free_port):
    """データチャネルシグナリング有効時の接続テスト"""
    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        audio=True,
        video=True,
        data_channel_signaling=True,
        ignore_disconnect_websocket=True,
    ) as sumomo:
        time.sleep(2)
        stats = sumomo.get_stats()
        assert stats is not None
