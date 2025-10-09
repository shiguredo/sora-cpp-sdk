"""Windows 調査用に Sumomo を詳細ログ付きでラップするヘルパー"""

from __future__ import annotations

import json
import platform
import time
from typing import Any, Iterable

from sumomo import Sumomo


class SumomoWindows:
    """Windows 調査を支援するための Sumomo ラッパークラス"""

    def __init__(self, *, log_interval: float = 1.0, **kwargs: Any) -> None:
        self._log_prefix = "[SumomoWindows]"
        self._platform = platform.system().lower()
        self._log(f"Detected platform: {self._platform}")

        if self._platform != "windows":
            self._log(
                f"Warning: SumomoWindows is intended for Windows, running on {self._platform}"
            )

        self._log_interval = log_interval
        self._kwargs = kwargs
        self._sumomo = Sumomo(**kwargs)
        self._last_stats: list[dict[str, Any]] | None = None

        sanitized = self._sanitize(self._kwargs)
        self._log(f"Initialized with sanitized config: {json.dumps(sanitized, indent=2)}")

    def _log(self, message: str) -> None:
        timestamp = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime())
        print(f"{timestamp} {self._log_prefix} {message}", flush=True)

    def _is_sensitive(self, key_path: str) -> bool:
        key_lower = key_path.lower()
        sensitive_keywords = ("secret", "token", "password", "key", "metadata")
        return any(keyword in key_lower for keyword in sensitive_keywords)

    def _sanitize(self, value: Any, key_path: str = "") -> Any:
        if self._is_sensitive(key_path):
            if isinstance(value, str):
                return f"<redacted len={len(value)}>"
            if isinstance(value, Iterable) and not isinstance(value, (str, bytes, bytearray)):
                return ["<redacted>" for _ in value]
            if isinstance(value, dict):
                return {k: "<redacted>" for k in value}
            return "<redacted>"

        if isinstance(value, dict):
            return {
                k: self._sanitize(v, f"{key_path}.{k}" if key_path else k) for k, v in value.items()
            }
        if isinstance(value, list):
            return [self._sanitize(v, key_path) for v in value]
        if isinstance(value, tuple):
            return tuple(self._sanitize(v, key_path) for v in value)
        if isinstance(value, str) and len(value) > 256:
            return f"{value[:128]}... (truncated, total_length={len(value)})"
        return value

    def __enter__(self) -> "SumomoWindows":
        self._log("Entering SumomoWindows context")
        self._sumomo.__enter__()
        self._log(f"Sumomo executable path: {self._sumomo.executable_path}")
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self._log("Exiting SumomoWindows context")
        result = self._sumomo.__exit__(exc_type, exc_val, exc_tb)
        self._log("SumomoWindows context exited")
        return result

    def get_stats(self) -> list[dict[str, Any]]:
        self._log("Fetching stats from Sumomo")
        stats = self._sumomo.get_stats()
        self._last_stats = stats
        self._log(f"Stats fetched, entries={len(stats)}")
        return stats

    def wait_for_stats(self, attempts: int = 10, delay: float = 1.0) -> list[dict[str, Any]]:
        self._log(f"Waiting for stats: attempts={attempts}, delay={delay}s")
        last_error: Exception | None = None
        for attempt in range(1, attempts + 1):
            try:
                stats = self.get_stats()
                if stats:
                    self._log(f"Stats retrieved on attempt {attempt}")
                    return stats
                self._log(f"Attempt {attempt}: stats list is empty")
            except Exception as exc:
                last_error = exc
                self._log(f"Attempt {attempt} failed with error: {exc!r}")
            time.sleep(delay)

        if last_error:
            raise RuntimeError("Failed to retrieve stats") from last_error
        raise RuntimeError("Failed to retrieve stats within expected attempts")

    def diagnostics(self) -> dict[str, Any]:
        process = self._sumomo.process
        process_info = None
        if process:
            process_info = {
                "pid": process.pid,
                "returncode": process.returncode,
                "running": process.poll() is None,
            }

        snapshot = {
            "platform": self._platform,
            "config": self._sanitize(self._kwargs),
            "process": process_info,
            "executable_path": self._sumomo.executable_path,
        }

        if isinstance(self._last_stats, list) and self._last_stats:
            snapshot["last_stats_sample"] = self._sanitize(
                self._last_stats[: min(3, len(self._last_stats))], key_path="stats"
            )
        else:
            snapshot["last_stats_sample"] = None

        self._log(f"Diagnostics snapshot: {json.dumps(snapshot, indent=2)}")
        return snapshot

    def __getattr__(self, item: str) -> Any:
        """Sumomo の公開 API へフォールバック"""
        return getattr(self._sumomo, item)
