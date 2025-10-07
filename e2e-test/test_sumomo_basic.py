"""Sumomo の基本的な E2E テスト"""

import time
from typing import Any

import pytest

from sumomo import Sumomo


def test_sumomo_sendonly_recvonly(sora_settings, port_allocator):
    """sendonly と recvonly のペアでの統合テスト"""
    # 同じチャンネル ID を使用
    channel_id = sora_settings.channel_id

    # 送信側を起動
    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=next(port_allocator),
        video=True,
        audio=False,
        video_codec_type="VP9",  # 明示的にコーデックを指定
    ) as sender:
        # 受信側を起動
        with Sumomo(
            signaling_url=sora_settings.signaling_url,
            channel_id=channel_id,
            role="recvonly",
            metadata=sora_settings.metadata,
            http_port=next(port_allocator),
            video=True,
            audio=False,
        ) as receiver:
            # 接続が確立して安定するまで待つ
            time.sleep(3)

            # ========== Sender の統計情報を検証 ==========
            sender_stats: list[dict[str, Any]] = sender.get_stats()
            assert sender_stats is not None
            assert isinstance(sender_stats, list), "Sender stats should be a list"
            assert len(sender_stats) > 0, "Sender stats should not be empty"

            print("=== Sender Stats ===")
            sender_stat_types = set(stat.get("type") for stat in sender_stats if "type" in stat)
            print(f"Sender stat types: {sender_stat_types}")

            # sendonly では video の outbound-rtp が一つ存在することを確認
            sender_video_outbound = next(
                (
                    stat
                    for stat in sender_stats
                    if stat.get("type") == "outbound-rtp" and stat.get("kind") == "video"
                ),
                None,
            )
            assert sender_video_outbound is not None, "Sender should have a video outbound-rtp stat"
            assert sender_video_outbound["packetsSent"] > 0, "Sender should have sent packets"
            assert sender_video_outbound["bytesSent"] > 0, "Sender should have sent bytes"

            print(
                f"Sender Video Outbound - ssrc: {sender_video_outbound['ssrc']}, "
                f"packets: {sender_video_outbound['packetsSent']}, "
                f"bytes: {sender_video_outbound['bytesSent']}"
            )

            # VP9 codec 情報の確認
            sender_vp9_codec = next(
                (
                    stat
                    for stat in sender_stats
                    if stat.get("type") == "codec" and "video/VP9" in stat.get("mimeType", "")
                ),
                None,
            )
            assert sender_vp9_codec is not None, "Sender should have VP9 codec stat"
            assert sender_vp9_codec["clockRate"] == 90000, "VP9 should have 90000 Hz clock rate"

            # codecId の一致確認
            assert (
                sender_video_outbound.get("codecId") == sender_vp9_codec["id"]
            ), "Sender video outbound codecId should match VP9 codec id"

            # ========== Receiver の統計情報を検証 ==========
            receiver_stats: list[dict[str, Any]] = receiver.get_stats()
            assert receiver_stats is not None
            assert isinstance(receiver_stats, list), "Receiver stats should be a list"
            assert len(receiver_stats) > 0, "Receiver stats should not be empty"

            print("\n=== Receiver Stats ===")
            receiver_stat_types = set(stat.get("type") for stat in receiver_stats if "type" in stat)
            print(f"Receiver stat types: {receiver_stat_types}")

            # recvonly では video の inbound-rtp が存在することを確認（送信者がいるため）
            receiver_video_inbound = next(
                (
                    stat
                    for stat in receiver_stats
                    if stat.get("type") == "inbound-rtp" and stat.get("kind") == "video"
                ),
                None,
            )
            assert (
                receiver_video_inbound is not None
            ), "Receiver should have a video inbound-rtp stat"
            assert (
                receiver_video_inbound["packetsReceived"] > 0
            ), "Receiver should have received packets"
            assert (
                receiver_video_inbound["bytesReceived"] > 0
            ), "Receiver should have received bytes"

            print(
                f"Receiver Video Inbound - ssrc: {receiver_video_inbound['ssrc']}, "
                f"packets: {receiver_video_inbound['packetsReceived']}, "
                f"bytes: {receiver_video_inbound['bytesReceived']}"
            )

            # 受信側にも codec 情報が存在することを確認
            receiver_codec = next(
                (
                    stat
                    for stat in receiver_stats
                    if stat.get("type") == "codec" and "video" in stat.get("mimeType", "").lower()
                ),
                None,
            )
            assert receiver_codec is not None, "Receiver should have video codec stat"
            print(f"Receiver Codec - mimeType: {receiver_codec['mimeType']}")

            # Transport 情報の確認（両方とも connected であること）
            sender_transport = next(
                (stat for stat in sender_stats if stat.get("type") == "transport"), None
            )
            receiver_transport = next(
                (stat for stat in receiver_stats if stat.get("type") == "transport"), None
            )

            assert sender_transport is not None, "Sender should have transport stat"
            assert receiver_transport is not None, "Receiver should have transport stat"
            assert (
                sender_transport.get("dtlsState") == "connected"
            ), "Sender DTLS should be connected"
            assert (
                receiver_transport.get("dtlsState") == "connected"
            ), "Receiver DTLS should be connected"

            print(
                f"\nSender transport: dtlsState={sender_transport['dtlsState']}, "
                f"bytesSent={sender_transport.get('bytesSent', 0)}"
            )
            print(
                f"Receiver transport: dtlsState={receiver_transport['dtlsState']}, "
                f"bytesReceived={receiver_transport.get('bytesReceived', 0)}"
            )


def test_sumomo_sendrecv_pair(sora_settings, port_allocator):
    """sendrecv モードのペアテスト（送受信の双方向通信）"""
    # 同じチャンネル ID を使用
    channel_id = sora_settings.channel_id

    # 両方 sendrecv で起動
    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=channel_id,
        role="sendrecv",
        metadata=sora_settings.metadata,
        http_port=next(port_allocator),
        video=True,
        audio=False,
    ) as peer1:
        with Sumomo(
            signaling_url=sora_settings.signaling_url,
            channel_id=channel_id,
            role="sendrecv",
            metadata=sora_settings.metadata,
            http_port=next(port_allocator),
            video=True,
            audio=False,
        ) as peer2:
            # 接続が確立するまで少し待つ
            time.sleep(3)

            # 両方の統計情報を取得
            peer1_stats = peer1.get_stats()
            peer2_stats = peer2.get_stats()

            # 両方の統計情報が取得できることを確認
            assert peer1_stats is not None
            assert peer2_stats is not None

            # Peer1 は outbound と inbound の両方を持つはず
            peer1_outbound = next(
                (
                    stat
                    for stat in peer1_stats
                    if stat.get("type") == "outbound-rtp" and stat.get("kind") == "video"
                ),
                None,
            )
            peer1_inbound = next(
                (
                    stat
                    for stat in peer1_stats
                    if stat.get("type") == "inbound-rtp" and stat.get("kind") == "video"
                ),
                None,
            )

            # Peer2 も outbound と inbound の両方を持つはず
            peer2_outbound = next(
                (
                    stat
                    for stat in peer2_stats
                    if stat.get("type") == "outbound-rtp" and stat.get("kind") == "video"
                ),
                None,
            )
            peer2_inbound = next(
                (
                    stat
                    for stat in peer2_stats
                    if stat.get("type") == "inbound-rtp" and stat.get("kind") == "video"
                ),
                None,
            )

            print("=== Peer1 Stats ===")
            if peer1_outbound:
                print(
                    f"Peer1 Outbound - packets: {peer1_outbound['packetsSent']}, bytes: {peer1_outbound['bytesSent']}"
                )
            if peer1_inbound:
                print(
                    f"Peer1 Inbound - packets: {peer1_inbound['packetsReceived']}, bytes: {peer1_inbound['bytesReceived']}"
                )

            print("\n=== Peer2 Stats ===")
            if peer2_outbound:
                print(
                    f"Peer2 Outbound - packets: {peer2_outbound['packetsSent']}, bytes: {peer2_outbound['bytesSent']}"
                )
            if peer2_inbound:
                print(
                    f"Peer2 Inbound - packets: {peer2_inbound['packetsReceived']}, bytes: {peer2_inbound['bytesReceived']}"
                )

            # sendrecv では両方向の通信が確立していることを確認
            assert peer1_outbound is not None, "Peer1 should have outbound-rtp"
            assert peer1_inbound is not None, "Peer1 should have inbound-rtp"
            assert peer2_outbound is not None, "Peer2 should have outbound-rtp"
            assert peer2_inbound is not None, "Peer2 should have inbound-rtp"


@pytest.mark.parametrize("codec_type", ["VP8", "VP9", "H264", "H265", "AV1"])
def test_sumomo_video_codec_types(sora_settings, free_port, codec_type):
    """異なるビデオコーデックタイプのテスト"""
    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        video=True,
        audio=False,
        video_codec_type=codec_type,
    ) as sumomo:
        stats = sumomo.get_stats()
        assert stats is not None
        print(f"Stats with {codec_type}: {stats}")


def test_sumomo_with_audio(sora_settings, free_port):
    """音声付きの接続テスト"""
    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        video=True,
        audio=True,  # 音声も送信
        audio_codec_type="OPUS",
    ) as sumomo:
        stats = sumomo.get_stats()
        assert stats is not None
        print(f"Stats with audio: {stats}")


@pytest.mark.parametrize("resolution", ["QVGA", "VGA", "HD", "FHD"])
def test_sumomo_resolutions(sora_settings, free_port, resolution):
    """異なる解像度での接続テスト"""
    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        video=True,
        audio=False,
        resolution=resolution,
    ) as sumomo:
        stats = sumomo.get_stats()
        assert stats is not None
        print(f"Stats with resolution {resolution}: {stats}")


def test_sumomo_simulcast(sora_settings, free_port):
    """サイマルキャスト有効時の接続テスト"""
    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        video=True,
        audio=False,
        simulcast=True,
        video_codec_type="VP8",  # サイマルキャストは VP8/VP9/AV1 でサポート
    ) as sumomo:
        stats = sumomo.get_stats()
        assert stats is not None
        print(f"Stats with simulcast: {stats}")


def test_sumomo_data_channel_signaling(sora_settings, free_port):
    """データチャネルシグナリング有効時の接続テスト"""
    with Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        video=True,
        audio=False,
        data_channel_signaling=True,
        ignore_disconnect_websocket=True,  # WebSocket 切断を無視
    ) as sumomo:
        # データチャネルシグナリングの場合、接続が安定するまで少し待つ
        time.sleep(2)
        stats = sumomo.get_stats()
        assert stats is not None
        print(f"Stats with data channel signaling: {stats}")
