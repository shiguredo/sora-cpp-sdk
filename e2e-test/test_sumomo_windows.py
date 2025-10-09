"""Windows 上で Sumomo を詳細ログ付きで検証する E2E テスト"""

import platform
import time

import pytest

from sumomo_windows import SumomoWindows


@pytest.mark.skipif(platform.system().lower() != "windows", reason="Windows 以外では実行しません")
def test_sumomo_windows_collects_stats(sora_settings, port_allocator):
    """SumomoWindows が統計情報と診断情報を取得できるか検証"""
    http_port = next(port_allocator)

    sumomo = SumomoWindows(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendrecv",
        metadata=sora_settings.metadata,
        http_port=http_port,
        video=True,
        audio=True,
        log_level="verbose",
    )

    with sumomo as client:
        # 安定するまでリトライしながら統計情報を取得
        stats = client.wait_for_stats(attempts=15, delay=2.0)
        assert isinstance(stats, list)
        assert len(stats) > 0

        # 取得した統計情報に基本的なフィールドが存在することを確認
        kinds = {entry.get("type") for entry in stats if isinstance(entry, dict)}
        assert "inbound-rtp" in kinds or "outbound-rtp" in kinds

        diagnostics = client.diagnostics()
        assert diagnostics["process"] is not None
        assert diagnostics["process"]["running"] is True
        assert diagnostics["executable_path"]

        # 結果が安定するように少し待機して追加の統計を確認
        time.sleep(2)
        follow_up_stats = client.get_stats()
        assert isinstance(follow_up_stats, list)
        assert len(follow_up_stats) >= len(stats)
