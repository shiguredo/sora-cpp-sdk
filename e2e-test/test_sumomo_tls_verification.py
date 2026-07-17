"""TLS 検証の E2E テスト"""

import subprocess
from pathlib import Path

import pytest

from helper import get_transport
from sumomo import Sumomo


# ISRG Root X1 PEM (Let's Encrypt のルート CA、有効期限 2035-06-04)
# https://letsencrypt.org/certs/isrgrootx1.pem
ISRG_ROOT_X1_PEM = """\
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
"""


def _generate_dummy_ca_cert(cert_path: Path) -> None:
    """openssl req -x509 で自己署名のダミー CA 証明書を生成する"""
    key_path = cert_path.with_suffix(".key")
    subprocess.run(
        [
            "openssl",
            "req",
            "-x509",
            "-newkey",
            "rsa:2048",
            "-keyout",
            str(key_path),
            "-out",
            str(cert_path),
            "-days",
            "365",
            "-nodes",
            "-subj",
            "/CN=Test Dummy CA For TLS Verification Test",
        ],
        check=True,
        capture_output=True,
    )
    key_path.unlink(missing_ok=True)


def _assert_system_ca_log_present(stderr: str) -> None:
    assert "LoadSystemSSLRootCertificates: added=" in stderr, (
        f"システム CA の読み込みログが見つからない: {stderr[:500]}"
    )


def _assert_system_ca_log_absent(stderr: str) -> None:
    assert "LoadSystemSSLRootCertificates: added=" not in stderr, (
        f"ca_cert 指定時にシステム CA の読み込みログが残っている: {stderr[:500]}"
    )


def test_tls_system_ca_success(sora_settings, free_port):
    """ca_cert 未指定: システム CA 経由で TLS 検証が成功し、DTLS が接続される"""
    sumomo = Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        audio=True,
        video=True,
        capture_stderr=True,
        log_level="info",
    )
    with sumomo:
        stats = sumomo.get_stats()
        transport = get_transport(stats)
        assert transport is not None, "transport が取得できない"
        assert transport["dtlsState"] == "connected", (
            f"DTLS が未接続: {transport['dtlsState']}"
        )

    assert sumomo.stderr_output is not None, "stderr が取得できていない"
    _assert_system_ca_log_present(sumomo.stderr_output)
    assert "X509_verify_cert failed" not in sumomo.stderr_output, (
        f"システム CA 経由で X509 検証エラーが出ている: {sumomo.stderr_output[:500]}"
    )


def test_tls_invalid_ca_cert_fails(sora_settings, free_port, tmp_path):
    """誤った自己署名 CA を ca_cert に指定: TLS 検証が失敗し接続できない"""
    ca_path = tmp_path / "dummy_ca.pem"
    _generate_dummy_ca_cert(ca_path)

    sumomo = Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        audio=True,
        video=True,
        ca_cert=str(ca_path),
        capture_stderr=True,
        log_level="info",
    )
    with pytest.raises(RuntimeError):
        sumomo.__enter__()

    assert sumomo.stderr_output is not None, "stderr が取得できていない"
    assert "X509_verify_cert failed" in sumomo.stderr_output, (
        f"X509_verify_cert failed のログが見つからない: {sumomo.stderr_output[:500]}"
    )
    _assert_system_ca_log_absent(sumomo.stderr_output)


def test_tls_empty_ca_cert_fails(sora_settings, free_port, tmp_path):
    """空の PEM ファイルを ca_cert に指定: 証明書追加に失敗し接続できない"""
    ca_path = tmp_path / "empty.pem"
    ca_path.write_text("")

    sumomo = Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        audio=True,
        video=True,
        ca_cert=str(ca_path),
        capture_stderr=True,
        log_level="info",
    )
    with pytest.raises(RuntimeError):
        sumomo.__enter__()

    assert sumomo.stderr_output is not None, "stderr が取得できていない"
    assert "Failed to add ca_cert" in sumomo.stderr_output, (
        f"Failed to add ca_cert のログが見つからない: {sumomo.stderr_output[:500]}"
    )
    _assert_system_ca_log_absent(sumomo.stderr_output)


def test_tls_insecure_skips_verification(sora_settings, free_port, tmp_path):
    """insecure=true + 誤った ca_cert: TLS 検証がスキップされ DTLS が接続される"""
    ca_path = tmp_path / "dummy_ca.pem"
    _generate_dummy_ca_cert(ca_path)

    sumomo = Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        audio=True,
        video=True,
        ca_cert=str(ca_path),
        insecure=True,
        capture_stderr=True,
        log_level="info",
    )
    with sumomo:
        stats = sumomo.get_stats()
        transport = get_transport(stats)
        assert transport is not None, "transport が取得できない"
        assert transport["dtlsState"] == "connected", (
            f"DTLS が未接続: {transport['dtlsState']}"
        )

    assert sumomo.stderr_output is not None, "stderr が取得できていない"
    assert "X509_verify_cert failed" not in sumomo.stderr_output, (
        f"insecure=true なのに X509 検証エラーが出ている: {sumomo.stderr_output[:500]}"
    )
    _assert_system_ca_log_absent(sumomo.stderr_output)


def test_tls_correct_ca_cert_success(sora_settings, free_port, tmp_path):
    """ISRG Root X1 を ca_cert に指定: 正しい CA で TLS 検証が成功し DTLS が接続される"""
    ca_path = tmp_path / "isrg_root_x1.pem"
    ca_path.write_text(ISRG_ROOT_X1_PEM)

    sumomo = Sumomo(
        signaling_url=sora_settings.signaling_url,
        channel_id=sora_settings.channel_id,
        role="sendonly",
        metadata=sora_settings.metadata,
        http_port=free_port,
        audio=True,
        video=True,
        ca_cert=str(ca_path),
        capture_stderr=True,
        log_level="info",
    )
    with sumomo:
        stats = sumomo.get_stats()
        transport = get_transport(stats)
        assert transport is not None, "transport が取得できない"
        assert transport["dtlsState"] == "connected", (
            f"DTLS が未接続: {transport['dtlsState']}"
        )

    assert sumomo.stderr_output is not None, "stderr が取得できていない"
    assert "X509_verify_cert failed" not in sumomo.stderr_output, (
        f"正しい ca_cert なのに X509 検証エラーが出ている: {sumomo.stderr_output[:500]}"
    )
    _assert_system_ca_log_absent(sumomo.stderr_output)
