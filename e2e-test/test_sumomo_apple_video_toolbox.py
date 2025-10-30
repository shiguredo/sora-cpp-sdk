"""Sumomo の Apple Video Toolbox E2E テスト

macOS で H264/H265 を internal で指定すると Apple Video Toolbox を使用する
"""

import os
import time

import pytest

from helper import (
    get_codec,
    get_inbound_rtp,
    get_outbound_rtp,
    get_simulcast_outbound_rtp,
    get_transport,
)
from sumomo import Sumomo

# Apple Video Toolbox 環境が有効でない場合はスキップ
pytestmark = pytest.mark.skipif(
    not os.environ.get("APPLE_VIDEO_TOOLBOX"),
    reason="APPLE_VIDEO_TOOLBOX not set in environment",
)


@pytest.mark.parametrize(
    "video_codec_type",
    [
        "H264",
        "H265",
    ],
)
def test_sendonly_recvonly(
    sora_settings,
    port_allocator,
    video_codec_type,
):
    """sendonly と recvonly のペアを作成して送受信を確認（Apple Video Toolbox 使用）"""

    expected_mime_type = f"video/{video_codec_type}"

    # エンコーダー設定を準備（macOS では internal が VideoToolbox を使用）
    encoder_params = {}
    if video_codec_type == "H264":
        encoder_params["h264_encoder"] = "internal"
    elif video_codec_type == "H265":
        encoder_params["h265_encoder"] = "internal"

    # デコーダー設定を準備（macOS では internal が VideoToolbox を使用）
    decoder_params = {}
    if video_codec_type == "H264":
        decoder_params["h264_decoder"] = "internal"
    elif video_codec_type == "H265":
        decoder_params["h265_decoder"] = "internal"

    # 送信専用クライアント
    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=next(port_allocator),
        audio=False,
        video=True,
        video_codec_type=video_codec_type,
        initial_wait=10,
        **encoder_params,
    ) as sender:
        # 受信専用クライアント
        with Sumomo(
            signaling_url=sora_settings.signaling_url,
            channel_id=sora_settings.channel_id,
            role="recvonly",
            metadata=sora_settings.metadata,
            http_port=next(port_allocator),
            **decoder_params,
        ) as receiver:
            time.sleep(3)

            # 送信側の統計を確認
            sender_stats = sender.get_stats()
            assert sender_stats is not None

            # video codec を確認
            sender_video_codec = get_codec(sender_stats, expected_mime_type)
            assert sender_video_codec is not None
            assert sender_video_codec["mimeType"] == expected_mime_type

            # video outbound-rtp を確認
            sender_video_outbound = get_outbound_rtp(sender_stats, "video")
            assert sender_video_outbound is not None
            assert sender_video_outbound["packetsSent"] > 0
            assert sender_video_outbound["bytesSent"] > 0
            assert "encoderImplementation" in sender_video_outbound
            assert sender_video_outbound["encoderImplementation"] == "VideoToolbox"

            # 受信側の統計を確認
            receiver_stats = receiver.get_stats()
            assert receiver_stats is not None

            # video codec を確認
            receiver_video_codec = get_codec(receiver_stats, expected_mime_type)
            assert receiver_video_codec is not None
            assert receiver_video_codec["mimeType"] == expected_mime_type

            # video inbound-rtp を確認
            receiver_video_inbound = get_inbound_rtp(receiver_stats, "video")
            assert receiver_video_inbound is not None
            assert receiver_video_inbound["packetsReceived"] > 0
            assert receiver_video_inbound["bytesReceived"] > 0
            assert "decoderImplementation" in receiver_video_inbound
            assert receiver_video_inbound["decoderImplementation"] == "VideoToolbox"


@pytest.mark.parametrize(
    "video_codec_type",
    [
        "H264",
        "H265",
    ],
)
def test_sendrecv(
    sora_settings,
    port_allocator,
    video_codec_type,
):
    """sendrecv 2 つで相互に送受信を確認（Apple Video Toolbox 使用）"""

    expected_mime_type = f"video/{video_codec_type}"

    # エンコーダー設定を準備（macOS では internal が VideoToolbox を使用）
    encoder_params = {}
    if video_codec_type == "H264":
        encoder_params["h264_encoder"] = "internal"
    elif video_codec_type == "H265":
        encoder_params["h265_encoder"] = "internal"

    # デコーダー設定を準備（macOS では internal が VideoToolbox を使用）
    decoder_params = {}
    if video_codec_type == "H264":
        decoder_params["h264_decoder"] = "internal"
    elif video_codec_type == "H265":
        decoder_params["h265_decoder"] = "internal"

    # 両方のパラメータを統合
    codec_params = {**encoder_params, **decoder_params}

    # sendrecv クライアント 1
    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendrecv",
        metadata=sora_settings.metadata,
        http_port=next(port_allocator),
        audio=False,
        video=True,
        video_codec_type=video_codec_type,
        initial_wait=10,
        **codec_params,
    ) as client1:
        # sendrecv クライアント 2
        with Sumomo(
            signaling_url=sora_settings.signaling_url,
            channel_id=sora_settings.channel_id,
            role="sendrecv",
            metadata=sora_settings.metadata,
            http_port=next(port_allocator),
            audio=False,
            video=True,
            video_codec_type=video_codec_type,
            initial_wait=10,
            **codec_params,
        ) as client2:
            time.sleep(3)

            # クライアント 1 の統計を確認
            client1_stats = client1.get_stats()
            assert client1_stats is not None

            # video codec を確認
            client1_video_codec = get_codec(client1_stats, expected_mime_type)
            assert client1_video_codec is not None
            assert client1_video_codec["mimeType"] == expected_mime_type

            # video outbound-rtp を確認
            client1_video_outbound = get_outbound_rtp(client1_stats, "video")
            assert client1_video_outbound is not None
            assert client1_video_outbound["packetsSent"] > 0
            assert client1_video_outbound["bytesSent"] > 0
            assert "encoderImplementation" in client1_video_outbound
            assert client1_video_outbound["encoderImplementation"] == "VideoToolbox"

            # video inbound-rtp を確認
            client1_video_inbound = get_inbound_rtp(client1_stats, "video")
            assert client1_video_inbound is not None
            assert client1_video_inbound["packetsReceived"] > 0
            assert client1_video_inbound["bytesReceived"] > 0
            assert "decoderImplementation" in client1_video_inbound
            assert client1_video_inbound["decoderImplementation"] == "VideoToolbox"

            # クライアント 2 の統計を確認
            client2_stats = client2.get_stats()
            assert client2_stats is not None

            # video codec を確認
            client2_video_codec = get_codec(client2_stats, expected_mime_type)
            assert client2_video_codec is not None
            assert client2_video_codec["mimeType"] == expected_mime_type

            # video outbound-rtp を確認
            client2_video_outbound = get_outbound_rtp(client2_stats, "video")
            assert client2_video_outbound is not None
            assert client2_video_outbound["packetsSent"] > 0
            assert client2_video_outbound["bytesSent"] > 0
            assert "encoderImplementation" in client2_video_outbound
            assert client2_video_outbound["encoderImplementation"] == "VideoToolbox"

            # video inbound-rtp を確認
            client2_video_inbound = get_inbound_rtp(client2_stats, "video")
            assert client2_video_inbound is not None
            assert client2_video_inbound["packetsReceived"] > 0
            assert client2_video_inbound["bytesReceived"] > 0
            assert "decoderImplementation" in client2_video_inbound
            assert client2_video_inbound["decoderImplementation"] == "VideoToolbox"


@pytest.mark.parametrize(
    "video_codec_type",
    [
        "H264",
        "H265",
    ],
)
def test_simulcast(sora_settings, free_port, video_codec_type):
    """サイマルキャスト接続時の統計情報を確認（Apple Video Toolbox 使用）"""
    # エンコーダー設定を準備（macOS では internal が VideoToolbox を使用）
    encoder_params = {}
    if video_codec_type == "H264":
        encoder_params["h264_encoder"] = "internal"
    elif video_codec_type == "H265":
        encoder_params["h265_encoder"] = "internal"

    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        audio=False,
        video=True,
        video_codec_type=video_codec_type,
        simulcast=True,
        resolution="960x540",  # 540p の解像度
        video_bit_rate=3000,  # ビットレート 3000
        initial_wait=10,
        **encoder_params,
    ) as s:
        time.sleep(3)

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
        expected_mime_type = f"video/{video_codec_type}"
        video_codec = get_codec(stats, expected_mime_type)
        assert video_codec is not None
        assert video_codec["clockRate"] == 90000

        # simulcast では video の outbound-rtp が 3 つ存在することを確認
        video_outbound_rtp_by_rid = get_simulcast_outbound_rtp(stats, "video")
        assert len(video_outbound_rtp_by_rid) == 3
        assert set(video_outbound_rtp_by_rid.keys()) == {"r0", "r1", "r2"}

        # r0 (低解像度) の検証
        outbound_rtp_r0 = video_outbound_rtp_by_rid["r0"]
        assert outbound_rtp_r0["rid"] == "r0"
        assert outbound_rtp_r0["packetsSent"] > 0
        assert outbound_rtp_r0["bytesSent"] > 0
        assert outbound_rtp_r0["framesEncoded"] > 0
        assert outbound_rtp_r0["frameWidth"] == 240
        assert outbound_rtp_r0["frameHeight"] == 128

        # encoder implementation を確認
        assert "encoderImplementation" in outbound_rtp_r0
        assert "SimulcastEncoderAdapter" in outbound_rtp_r0["encoderImplementation"]
        assert "VideoToolbox" in outbound_rtp_r0["encoderImplementation"]

        # r1 (中解像度) の検証
        outbound_rtp_r1 = video_outbound_rtp_by_rid["r1"]
        assert outbound_rtp_r1["rid"] == "r1"
        assert outbound_rtp_r1["packetsSent"] > 0
        assert outbound_rtp_r1["bytesSent"] > 0
        assert outbound_rtp_r1["framesEncoded"] > 0
        assert outbound_rtp_r1["frameWidth"] == 480
        assert outbound_rtp_r1["frameHeight"] == 256

        assert "encoderImplementation" in outbound_rtp_r1
        assert "SimulcastEncoderAdapter" in outbound_rtp_r1["encoderImplementation"]
        assert "VideoToolbox" in outbound_rtp_r1["encoderImplementation"]

        # r2 (高解像度) の検証
        outbound_rtp_r2 = video_outbound_rtp_by_rid["r2"]
        assert outbound_rtp_r2["rid"] == "r2"
        assert outbound_rtp_r2["packetsSent"] > 0
        assert outbound_rtp_r2["bytesSent"] > 0
        assert outbound_rtp_r2["framesEncoded"] > 0
        assert outbound_rtp_r2["frameWidth"] == 960
        assert outbound_rtp_r2["frameHeight"] == 528

        assert "encoderImplementation" in outbound_rtp_r2
        assert "SimulcastEncoderAdapter" in outbound_rtp_r2["encoderImplementation"]
        assert "VideoToolbox" in outbound_rtp_r2["encoderImplementation"]

        # transport を確認
        transport = get_transport(stats)
        assert transport is not None
        assert transport["dtlsState"] == "connected"
