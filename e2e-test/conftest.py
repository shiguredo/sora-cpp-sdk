import itertools
import os
import time
import uuid
from dataclasses import dataclass

import jwt
import pytest
from dotenv import load_dotenv

# .env ファイルを読み込む
load_dotenv()


@dataclass
class SoraSettings:
    """Sora 用の設定"""

    signaling_url: str
    channel_id_prefix: str
    secret_key: str
    channel_id: str
    metadata: dict


@pytest.fixture
def sora_settings():
    """Sora 用の設定を提供するフィクスチャ（各テストごとに新しいchannel_idを生成）"""
    # 環境変数から設定を取得（必須）
    signaling_url = os.environ.get("TEST_SIGNALING_URL")
    if not signaling_url:
        raise ValueError("TEST_SIGNALING_URL environment variable is required")

    channel_id_prefix = os.environ.get("TEST_CHANNEL_ID_PREFIX")
    if not channel_id_prefix:
        raise ValueError("TEST_CHANNEL_ID_PREFIX environment variable is required")

    secret_key = os.environ.get("TEST_SECRET_KEY")
    if not secret_key:
        raise ValueError("TEST_SECRET_KEY environment variable is required")

    # チャンネルIDを生成
    channel_id = f"{channel_id_prefix}{uuid.uuid4().hex[:8]}"

    # メタデータを生成
    payload = {
        "channel_id": channel_id,
        "exp": int(time.time()) + 300,
    }
    access_token = jwt.encode(payload, secret_key, algorithm="HS256")
    metadata = {"access_token": access_token}

    return SoraSettings(
        signaling_url=signaling_url,
        channel_id_prefix=channel_id_prefix,
        secret_key=secret_key,
        channel_id=channel_id,
        metadata=metadata,
    )


@pytest.fixture(scope="session")
def port_allocator():
    """セッション全体で共有されるポート番号アロケーター

    エフェメラルポート開始の 55000 から始まるポート番号を順番に生成します。
    複数のテストが並列実行されても、各テストに一意のポート番号が割り当てられます。
    """
    return itertools.count(55000)


@pytest.fixture
def free_port(port_allocator):
    """利用可能なポート番号を提供するフィクスチャ

    各テスト関数で使用すると、自動的に一意のポート番号が割り当てられます。
    """
    return next(port_allocator)


@pytest.fixture
def sumomo(sora_settings, free_port):
    """Sumomo インスタンスを提供するフィクスチャ

    sora_settings と free_port を使用して設定済みの Sumomo インスタンスを作成します。
    テスト終了時には自動的にクリーンアップされます。

    使用例:
        def test_sumomo_connection(sumomo):
            with sumomo:
                stats = sumomo.get_stats()
                assert "connections" in stats
    """
    from sumomo import Sumomo

    return Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",  # デフォルトは sendonly、テストで上書き可能
        metadata=sora_settings.metadata,
        http_port=free_port,
        # デフォルトでビデオのみ送信
        video=True,
        audio=False,
    )
