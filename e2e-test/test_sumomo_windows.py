"""Windows 上で Sumomo を詳細ログ付きで検証する E2E テスト"""

import platform
import time

import pytest

from sumomo_windows import SumomoWindows


@pytest.mark.skipif(platform.system().lower() != "windows", reason="Windows 以外では実行しません")
@pytest.mark.timeout(60)  # 1 分でタイムアウト
def test_sumomo_windows_collects_stats(sora_settings, port_allocator):
    """SumomoWindows が統計情報と診断情報を取得できるか検証"""
    print("\n=== TEST START ===")
    print(f"Platform: {platform.system()}")
    print(f"Machine: {platform.machine()}")

    http_port = next(port_allocator)
    print(f"Allocated HTTP port: {http_port}")

    print("\n=== CREATING SUMOMO INSTANCE ===")
    sumomo = SumomoWindows(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=http_port,
        http_host="127.0.0.1",
        video=True,
        audio=True,
        log_level="verbose",
    )

    print("\n=== ENTERING CONTEXT ===")
    with sumomo as client:
        print("\n=== CONTEXT ENTERED ===")
        print(f"Process PID: {client.process.pid if client.process else 'None'}")
        print(f"HTTP port: {client.http_port}")
        print(f"Executable: {client.executable_path}")

        print("\n=== WAITING FOR STATS ===")
        # 安定するまでリトライしながら統計情報を取得
        stats = client.wait_for_stats(attempts=15, delay=2.0)
        print(f"\n=== STATS RECEIVED: {len(stats)} entries ===")

        assert isinstance(stats, list)
        assert len(stats) > 0
        print("✓ Stats is a non-empty list")

        # 取得した統計情報に基本的なフィールドが存在することを確認
        kinds = {entry.get("type") for entry in stats if isinstance(entry, dict)}
        print(f"Stats types: {kinds}")
        assert "inbound-rtp" in kinds or "outbound-rtp" in kinds
        print("✓ Stats contains RTP entries")

        print("\n=== GETTING DIAGNOSTICS ===")
        diagnostics = client.diagnostics()
        assert diagnostics["process"] is not None
        assert diagnostics["process"]["running"] is True
        assert diagnostics["executable_path"]
        print("✓ Diagnostics OK")

        # 結果が安定するように少し待機して追加の統計を確認
        print("\n=== WAITING 2s BEFORE FOLLOW-UP ===")
        time.sleep(2)

        print("\n=== GETTING FOLLOW-UP STATS ===")
        follow_up_stats = client.get_stats()
        print(f"Follow-up stats: {len(follow_up_stats)} entries")
        assert isinstance(follow_up_stats, list)
        assert len(follow_up_stats) >= len(stats)
        print("✓ Follow-up stats OK")

    print("\n=== TEST COMPLETED SUCCESSFULLY ===")
