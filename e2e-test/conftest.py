import itertools
import os
import socket
import time
import uuid
from collections.abc import Iterator
from dataclasses import dataclass

import jwt
import pytest
from dotenv import load_dotenv

# .env ファイルを読み込む
load_dotenv()

# ポート番号を探し始める番号。
#
# Windows / macOS の既定の動的ポート範囲は 49152〜65535、Linux の既定の
# ephemeral range は 32768〜60999 である。この内側を候補にすると、OS が送信元
# ポートとして割り当てたポートや、Hyper-V などが予約した除外ポート範囲
# (excluded port range) と衝突する。Windows では除外ポート範囲への bind は
# SO_REUSEADDR を設定していても WSAEACCES (10013) で失敗するため、sumomo の
# HTTP サーバーが起動できなくなる (Microsoft KB3039044)。
# どの動的ポート範囲にも含まれない 20000 から昇順に探す。
PORT_ALLOCATOR_START = 20000


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


def is_port_bindable(port: int) -> bool:
    """127.0.0.1 の指定ポートに実際に bind できるかを確認する

    sumomo は `--http-host 127.0.0.1` で HTTP サーバーを起動するため、確認先も
    127.0.0.1 に合わせる。

    SO_REUSEADDR は設定しない。設定すると Windows では他プロセスが使用中の
    ポートにも bind できてしまい、ポートが空いているかを確認する意味がなくなる。
    """
    # 実際のソケットで確認する。bind できたら即座に閉じてポートを解放する
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        try:
            sock.bind(("127.0.0.1", port))
        except OSError:
            # 他プロセスが使用中、TIME_WAIT 中、除外ポート範囲のいずれか
            return False
    return True


def iter_available_ports(start_port: int = PORT_ALLOCATOR_START) -> Iterator[int]:
    """bind 可能なポート番号を昇順に払い出すイテレーターを返す

    bind できない候補を読み飛ばし、実際に bind できたポートだけを返す。
    同じイテレーターからは同じポートが二度払い出されないため、1 つの
    イテレーターをセッション内で共有すれば払い出しは一意になる。
    """
    for port in itertools.count(start_port):
        if is_port_bindable(port):
            yield port


@pytest.fixture(scope="session")
def port_allocator() -> Iterator[int]:
    """セッション全体で共有されるポート番号アロケーター

    bind 可能なポート番号を PORT_ALLOCATOR_START から昇順に生成します。
    除外ポート範囲や他プロセスが使用中のポートは読み飛ばします。
    複数のテストが並列実行されても、各テストに一意のポート番号が割り当てられます。
    """
    return iter_available_ports()


@pytest.fixture
def free_port(port_allocator: Iterator[int]) -> int:
    """利用可能なポート番号を提供するフィクスチャ

    各テスト関数で使用すると、自動的に一意のポート番号が割り当てられます。
    """
    return next(port_allocator)


@pytest.fixture
def free_port2(port_allocator: Iterator[int]) -> int:
    """2 つ目のポート番号を提供するフィクスチャ

    同一テスト内で複数の Sumomo プロセスを起動する際に使用する。
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
