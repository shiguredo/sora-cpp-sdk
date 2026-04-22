"""Sumomo プロセス出力設定のテスト"""

import subprocess

from sumomo import Sumomo


class FakeProcess:
    def __init__(self) -> None:
        self.pid = 4321
        self.stderr = None
        self.returncode = 0

    def terminate(self) -> None:
        pass

    def wait(self, timeout: float | None = None) -> int:
        return 0

    def poll(self) -> None:
        return None


def test_sumomo_uses_spooled_output_when_dump_enabled(monkeypatch):
    popen_kwargs = {}

    monkeypatch.setattr(Sumomo, "_get_sumomo_executable_path", lambda self: "/tmp/sumomo")
    monkeypatch.setattr("sumomo.time.sleep", lambda _seconds: None)

    def fake_popen(cmd, **kwargs):
        popen_kwargs.update(kwargs)
        return FakeProcess()

    monkeypatch.setattr("sumomo.subprocess.Popen", fake_popen)

    with Sumomo(
        signaling_url="wss://example.com/signaling",
        channel_id="test-channel",
        role="sendonly",
        initial_wait=0,
        dump_process_output_on_failure=True,
    ):
        pass

    assert popen_kwargs["stdout"] is not None
    assert popen_kwargs["stderr"] == subprocess.STDOUT


def test_sumomo_dumps_output_only_on_failure(monkeypatch, capsys):
    monkeypatch.setattr(Sumomo, "_get_sumomo_executable_path", lambda self: "/tmp/sumomo")
    monkeypatch.setattr("sumomo.time.sleep", lambda _seconds: None)

    def fake_popen(cmd, **kwargs):
        kwargs["stdout"].write(b"sumomo log line\n")
        kwargs["stdout"].flush()
        return FakeProcess()

    monkeypatch.setattr("sumomo.subprocess.Popen", fake_popen)

    try:
        with Sumomo(
            signaling_url="wss://example.com/signaling",
            channel_id="test-channel",
            role="sendonly",
            initial_wait=0,
            dump_process_output_on_failure=True,
        ):
            raise AssertionError("boom")
    except AssertionError:
        pass

    captured = capsys.readouterr()
    assert "===== sumomo output (AssertionError: boom) =====" in captured.out
    assert "sumomo log line" in captured.out
