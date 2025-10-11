"""WebRTC 統計情報のユーティリティ関数"""

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
