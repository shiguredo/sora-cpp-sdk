"""WebRTC 統計情報のユーティリティ関数"""

import time
from collections.abc import Callable
from typing import Any


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


def get_simulcast_outbound_rtp(stats: list[dict[str, Any]], kind: str) -> dict[str, dict[str, Any]]:
    """サイマルキャスト用の outbound-rtp 統計情報を rid をキーとした辞書で取得する"""
    outbound_rtp_stats = [
        stat for stat in stats if stat.get("type") == "outbound-rtp" and stat.get("kind") == kind
    ]
    return {stat.get("rid", ""): stat for stat in outbound_rtp_stats}


def wait_for_dtls_connected(
    get_stats: Callable[[], list[dict[str, Any]]], timeout: float, interval: float
) -> None:
    """DTLS が connected になるまで get_stats() をポーリングして待つ"""
    deadline = time.time() + timeout
    last_state = "unknown"
    while True:
        stats = get_stats()
        transport = get_transport(stats)
        if transport is not None:
            state = transport["dtlsState"]
            last_state = state
            if state == "connected":
                return
            if state in ("failed", "closed"):
                assert False, f"DTLS が {state} 状態で終了しました"
        # transport が None の場合は connecting 相当とみなしリトライ継続
        if time.time() >= deadline:
            break
        time.sleep(interval)
    assert False, f"DTLS 接続がタイムアウトしました (最後の状態: {last_state})"
