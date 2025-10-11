"""OpenH264 を使用した Sumomo の E2E テスト"""

import os
import time

import pytest

from helper import get_codec, get_outbound_rtp, get_simulcast_outbound_rtp
from sumomo import Sumomo

# OpenH264 を使用するテストは OPENH264_PATH が設定されていない場合スキップ
pytestmark = [
    pytest.mark.skipif(
        not os.environ.get("OPENH264_PATH"),
        reason="OPENH264_PATH not set in environment",
    ),
]


def test_sumomo_sendonly_with_openh264_encoder(sora_settings, free_port):
    """sendonly モードで OpenH264 を使用した H.264 エンコーダーが動作することを確認"""

    openh264_path = os.environ.get("OPENH264_PATH")

    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        audio=True,
        video=True,
        audio_codec_type="OPUS",
        video_codec_type="H264",
        h264_encoder="cisco_openh264",  # OpenH264 エンコーダーを使用
        openh264=openh264_path,  # 明示的にパスを指定
    ) as s:
        time.sleep(3)

        stats = s.get_stats()
        assert stats is not None
        assert isinstance(stats, list)
        assert len(stats) > 0

        # H.264 codec が存在することを確認
        h264_codec = get_codec(stats, "video/H264")
        assert h264_codec is not None, "H.264 codec not found in stats"
        assert h264_codec["mimeType"] == "video/H264"
        assert h264_codec["clockRate"] == 90000

        # video の outbound-rtp で OpenH264 が使用されていることを確認
        video_outbound = get_outbound_rtp(stats, "video")
        assert video_outbound is not None, "Video outbound-rtp not found"
        assert "encoderImplementation" in video_outbound
        assert (
            "OpenH264" in video_outbound["encoderImplementation"]
        ), f"Expected OpenH264 encoder, but got {video_outbound.get('encoderImplementation')}"

        # OpenH264 エンコーダーの統計情報を検証
        assert video_outbound["codecId"] == h264_codec["id"]
        assert "ssrc" in video_outbound
        assert "packetsSent" in video_outbound
        assert "bytesSent" in video_outbound
        assert "framesEncoded" in video_outbound
        assert video_outbound["packetsSent"] > 0
        assert video_outbound["bytesSent"] > 0
        assert video_outbound["framesEncoded"] > 0


def test_sumomo_sendrecv_with_openh264_encoder(sora_settings, port_allocator):
    """sendrecv モードで OpenH264 を使用した H.264 エンコーダーが動作することを確認"""

    openh264_path = os.environ.get("OPENH264_PATH")

    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendrecv",
        metadata=sora_settings.metadata,
        http_port=next(port_allocator),
        audio=True,
        video=True,
        audio_codec_type="OPUS",
        video_codec_type="H264",
        h264_encoder="cisco_openh264",
        h264_decoder="cisco_openh264",
        openh264=openh264_path,
    ) as s1:
        with Sumomo(
            signaling_url=sora_settings.signaling_url,
            channel_id=sora_settings.channel_id,
            role="sendrecv",
            metadata=sora_settings.metadata,
            http_port=next(port_allocator),
            audio=True,
            video=True,
            audio_codec_type="OPUS",
            video_codec_type="H264",
            h264_encoder="cisco_openh264",
            h264_decoder="cisco_openh264",
            openh264=openh264_path,
        ) as s2:
            time.sleep(3)

            # Client1 の統計情報を確認
            stats1 = s1.get_stats()
            assert stats1 is not None

            # OpenH264 エンコーダーが使用されていることを確認
            video_outbound1 = get_outbound_rtp(stats1, "video")
            assert video_outbound1 is not None
            assert "OpenH264" in video_outbound1["encoderImplementation"]
            assert video_outbound1["packetsSent"] > 0
            assert video_outbound1["bytesSent"] > 0

            # Client2 の統計情報を確認
            stats2 = s2.get_stats()
            assert stats2 is not None

            # Client2 も正常に送受信できていることを確認
            video_outbound2 = get_outbound_rtp(stats2, "video")
            assert video_outbound2 is not None
            assert video_outbound2["packetsSent"] > 0
            assert video_outbound2["bytesSent"] > 0


def test_sumomo_openh264_with_simulcast(sora_settings, free_port):
    """OpenH264 を使用したサイマルキャストの動作を確認"""

    openh264_path = os.environ.get("OPENH264_PATH")

    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        audio=True,
        video=True,
        audio_codec_type="OPUS",
        video_codec_type="H264",
        video_bit_rate=3000,
        h264_encoder="cisco_openh264",
        openh264=openh264_path,
        simulcast=True,
        resolution="960x540",
    ) as s:
        time.sleep(10)

        stats = s.get_stats()
        assert stats is not None

        # サイマルキャストで複数の rid が存在することを確認
        video_outbound_by_rid = get_simulcast_outbound_rtp(stats, "video")
        assert len(video_outbound_by_rid) == 3, (
            f"Expected 3 simulcast streams with OpenH264, but got {len(video_outbound_by_rid)}"
        )

        # 全ての rid が存在することを確認
        assert set(video_outbound_by_rid.keys()) == {
            "r0",
            "r1",
            "r2",
        }, f"Expected rid r0, r1, r2, but got {set(video_outbound_by_rid.keys())}"

        # r0 (低解像度) の検証
        outbound_rtp_r0 = video_outbound_by_rid["r0"]
        assert "encoderImplementation" in outbound_rtp_r0
        # OpenH264 では SimulcastEncoderAdapter と OpenH264 の組み合わせ
        assert "SimulcastEncoderAdapter" in outbound_rtp_r0["encoderImplementation"]
        assert "OpenH264" in outbound_rtp_r0["encoderImplementation"]
        assert outbound_rtp_r0["rid"] == "r0"
        assert "ssrc" in outbound_rtp_r0
        assert "packetsSent" in outbound_rtp_r0
        assert "bytesSent" in outbound_rtp_r0
        assert "frameWidth" in outbound_rtp_r0
        assert "frameHeight" in outbound_rtp_r0
        assert outbound_rtp_r0["packetsSent"] > 0
        assert outbound_rtp_r0["bytesSent"] > 0
        assert outbound_rtp_r0["frameWidth"] == 240
        assert outbound_rtp_r0["frameHeight"] == 128
        print(f"r0: {outbound_rtp_r0['frameWidth']}x{outbound_rtp_r0['frameHeight']}")

        # r1 (中解像度) の検証
        outbound_rtp_r1 = video_outbound_by_rid["r1"]
        assert "encoderImplementation" in outbound_rtp_r1
        assert "SimulcastEncoderAdapter" in outbound_rtp_r1["encoderImplementation"]
        assert "OpenH264" in outbound_rtp_r1["encoderImplementation"]
        assert outbound_rtp_r1["rid"] == "r1"
        assert "ssrc" in outbound_rtp_r1
        assert "packetsSent" in outbound_rtp_r1
        assert "bytesSent" in outbound_rtp_r1
        assert "frameWidth" in outbound_rtp_r1
        assert "frameHeight" in outbound_rtp_r1
        assert outbound_rtp_r1["packetsSent"] > 0
        assert outbound_rtp_r1["bytesSent"] > 0
        assert outbound_rtp_r1["frameWidth"] == 480
        assert outbound_rtp_r1["frameHeight"] == 256
        print(f"r1: {outbound_rtp_r1['frameWidth']}x{outbound_rtp_r1['frameHeight']}")

        # r2 (高解像度) の検証
        outbound_rtp_r2 = video_outbound_by_rid["r2"]
        assert "encoderImplementation" in outbound_rtp_r2
        assert "SimulcastEncoderAdapter" in outbound_rtp_r2["encoderImplementation"]
        assert "OpenH264" in outbound_rtp_r2["encoderImplementation"]
        assert outbound_rtp_r2["rid"] == "r2"
        assert "ssrc" in outbound_rtp_r2
        assert "packetsSent" in outbound_rtp_r2
        assert "bytesSent" in outbound_rtp_r2
        assert "frameWidth" in outbound_rtp_r2
        assert "frameHeight" in outbound_rtp_r2
        assert outbound_rtp_r2["packetsSent"] > 0
        assert outbound_rtp_r2["bytesSent"] > 0
        assert outbound_rtp_r2["frameWidth"] == 960
        assert outbound_rtp_r2["frameHeight"] == 528
        print(f"r2: {outbound_rtp_r2['frameWidth']}x{outbound_rtp_r2['frameHeight']}")


def test_sumomo_explicit_openh264_path(sora_settings, free_port):
    """明示的に OpenH264 パスを指定した場合の動作を確認"""

    openh264_path = os.environ.get("OPENH264_PATH")

    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        audio=True,
        video=True,
        audio_codec_type="OPUS",
        video_codec_type="H264",
        h264_encoder="cisco_openh264",
        openh264=openh264_path,  # 明示的にパスを指定
    ) as s:
        time.sleep(3)

        stats = s.get_stats()
        assert stats is not None

        # OpenH264 エンコーダーが使用されていることを確認
        video_outbound = get_outbound_rtp(stats, "video")
        assert video_outbound is not None
        assert "encoderImplementation" in video_outbound
        assert (
            "OpenH264" in video_outbound["encoderImplementation"]
        ), "OpenH264 encoder not found with explicit path"
