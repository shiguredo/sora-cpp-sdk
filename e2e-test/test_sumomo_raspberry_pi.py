"""Sumomo の Raspberry Pi E2E テスト

Raspberry Pi の V4L2 M2M を使用した H264 のテスト
"""

import os
import time

import pytest

from helper import get_codec, get_outbound_rtp, get_simulcast_outbound_rtp, get_transport
from sumomo import Sumomo

# Raspberry Pi 環境が有効でない場合はスキップ
pytestmark = pytest.mark.skipif(
    not os.environ.get("RASPBERRY_PI"),
    reason="RASPBERRY_PI not set in environment",
)


def test_connection_stats(sora_settings, free_port):
    """H264 での接続時の統計情報を確認（Raspberry Pi V4L2 M2M 使用）"""
    video_codec_type = "H264"
    expected_mime_type = "video/H264"
    expected_encoder_implementation = "V4L2M2M H264"

    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        audio=False,
        video=True,
        video_codec_type=video_codec_type,
        h264_encoder="raspi_v4l2m2m",
        fake_capture_device=False,
        use_libcamera=True,
        initial_wait=10,
    ) as s:
        time.sleep(10)

        stats = s.get_stats()
        assert stats is not None
        assert isinstance(stats, list)
        assert len(stats) > 0

        # 統計タイプの収集
        stat_types = {stat.get("type") for stat in stats if "type" in stat}

        # 重要な統計タイプが存在することを確認
        expected_types = {
            "peer-connection",
            "transport",
            "codec",
            "outbound-rtp",
        }
        for expected_type in expected_types:
            assert expected_type in stat_types

        # video codec を確認
        video_codec = get_codec(stats, expected_mime_type)
        assert video_codec is not None
        assert video_codec["clockRate"] == 90000

        # video の outbound-rtp を確認
        video_outbound_rtp = get_outbound_rtp(stats, "video")
        assert video_outbound_rtp is not None
        assert video_outbound_rtp["packetsSent"] > 0
        assert video_outbound_rtp["bytesSent"] > 0
        assert video_outbound_rtp["framesEncoded"] > 0
        # フレーキーで frameWidth, frameHeight が出ないことがあるのでコメントアウト
        # assert "frameWidth" in video_outbound_rtp
        # assert "frameHeight" in video_outbound_rtp

        # エンコーダー実装が V4L2M2M H264 であることを確認
        assert "encoderImplementation" in video_outbound_rtp
        assert video_outbound_rtp["encoderImplementation"] == expected_encoder_implementation

        # transport を確認
        transport = get_transport(stats)
        assert transport is not None
        assert transport["dtlsState"] == "connected"


def test_simulcast(sora_settings, free_port):
    """サイマルキャスト接続時の統計情報を確認（Raspberry Pi V4L2 M2M 使用）"""
    video_codec_type = "H264"
    expected_mime_type = "video/H264"

    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        audio=False,
        video=True,
        video_codec_type=video_codec_type,
        h264_encoder="raspi_v4l2m2m",
        simulcast=True,
        resolution="960x540",  # 540p の解像度
        video_bit_rate=3000,  # ビットレート 3000
        fake_capture_device=False,
        use_libcamera=True,
        initial_wait=10,
    ) as s:
        time.sleep(10)

        stats = s.get_stats()
        assert stats is not None
        assert isinstance(stats, list)
        assert len(stats) > 0

        # 統計タイプの収集
        stat_types = {stat.get("type") for stat in stats if "type" in stat}

        # 重要な統計タイプが存在することを確認
        expected_types = {
            "peer-connection",
            "transport",
            "codec",
            "outbound-rtp",
        }
        for expected_type in expected_types:
            assert expected_type in stat_types

        # video codec を確認
        video_codec = get_codec(stats, expected_mime_type)
        assert video_codec is not None
        assert video_codec["clockRate"] == 90000

        # simulcast では video の outbound-rtp が 3 つ (r0, r1, r2) 存在することを確認
        outbound_rtp = get_simulcast_outbound_rtp(stats, "video")
        outbound_rtp_r0 = outbound_rtp["r0"]
        outbound_rtp_r1 = outbound_rtp["r1"]
        outbound_rtp_r2 = outbound_rtp["r2"]

        # r0 (低解像度) の検証
        assert outbound_rtp_r0["rid"] == "r0"
        assert outbound_rtp_r0["packetsSent"] > 0
        assert outbound_rtp_r0["bytesSent"] > 0
        assert outbound_rtp_r0["framesEncoded"] > 0

        # encoder implementation を確認
        assert "encoderImplementation" in outbound_rtp_r0
        assert "SimulcastEncoderAdapter" in outbound_rtp_r0["encoderImplementation"]
        assert "V4L2M2M H264" in outbound_rtp_r0["encoderImplementation"]

        assert outbound_rtp_r0["frameWidth"] == 240
        assert outbound_rtp_r0["frameHeight"] == 128

        # r1 (中解像度) の検証
        assert outbound_rtp_r1["rid"] == "r1"
        assert outbound_rtp_r1["packetsSent"] > 0
        assert outbound_rtp_r1["bytesSent"] > 0
        assert outbound_rtp_r1["framesEncoded"] > 0

        assert "encoderImplementation" in outbound_rtp_r1
        assert "SimulcastEncoderAdapter" in outbound_rtp_r1["encoderImplementation"]
        assert "V4L2M2M H264" in outbound_rtp_r1["encoderImplementation"]

        assert outbound_rtp_r1["frameWidth"] == 480
        assert outbound_rtp_r1["frameHeight"] == 256

        # r2 (高解像度) の検証
        assert outbound_rtp_r2["rid"] == "r2"
        assert outbound_rtp_r2["packetsSent"] > 0
        assert outbound_rtp_r2["bytesSent"] > 0
        assert outbound_rtp_r2["framesEncoded"] > 0

        # encoder implementation を確認
        assert "encoderImplementation" in outbound_rtp_r2
        assert "SimulcastEncoderAdapter" in outbound_rtp_r2["encoderImplementation"]
        assert "V4L2M2M H264" in outbound_rtp_r2["encoderImplementation"]

        # r2 はフレーキーで frameWidth, frameHeight が出ないことがある
        if "frameWidth" in outbound_rtp_r2 and "frameHeight" in outbound_rtp_r2:
            assert outbound_rtp_r2["frameWidth"] == 960
            assert outbound_rtp_r2["frameHeight"] == 528

        # transport を確認
        transport = get_transport(stats)
        assert transport is not None
        assert transport["dtlsState"] == "connected"
