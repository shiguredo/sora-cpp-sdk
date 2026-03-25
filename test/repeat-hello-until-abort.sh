#!/usr/bin/env bash
set -euo pipefail

HELLO_BIN="${1:-_build/ubuntu-24.04_x86_64/release/test/hello}"
PARAM_JSON="${2:-test/hello_intel_vpl_simulcast_repro.json}"
COUNT="${3:-100}"
RUN_SECONDS="${4:-2}"
SLEEP_BETWEEN="${5:-0.2}"
LOG_DIR="${6:-/tmp/sora-hello-repro-$(date +%Y%m%d_%H%M%S)}"
STOP_ON_DETECT="${7:-1}"

mkdir -p "${LOG_DIR}"

if [ ! -x "${HELLO_BIN}" ]; then
  echo "hello binary not found or not executable: ${HELLO_BIN}" >&2
  exit 2
fi

if [ ! -f "${PARAM_JSON}" ]; then
  echo "param json not found: ${PARAM_JSON}" >&2
  exit 2
fi

cleanup() {
  if [ -n "${hello_pid:-}" ] && kill -0 "${hello_pid}" 2>/dev/null; then
    kill -INT "${hello_pid}" 2>/dev/null || true
    wait "${hello_pid}" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

echo "hello bin    : ${HELLO_BIN}"
echo "param json   : ${PARAM_JSON}"
echo "count        : ${COUNT}"
echo "run seconds  : ${RUN_SECONDS}"
echo "sleep between: ${SLEEP_BETWEEN}"
echo "log dir      : ${LOG_DIR}"
echo "stop on detect: ${STOP_ON_DETECT}"

connected_detected=0
disconnect_detected=0
non_abort_failure=0

i=1
while [ "${i}" -le "${COUNT}" ]; do
  log_file="${LOG_DIR}/run-${i}.log"
  echo "===== run ${i}/${COUNT} ====="

  stdbuf -oL -eL "${HELLO_BIN}" "${PARAM_JSON}" >"${log_file}" 2>&1 &
  hello_pid=$!
  phase="connected"

  sleep "${RUN_SECONDS}"

  if kill -0 "${hello_pid}" 2>/dev/null; then
    phase="disconnect"
    kill -INT "${hello_pid}" 2>/dev/null || true
  fi

  set +e
  wait "${hello_pid}"
  status=$?
  set -e
  hello_pid=""

  if grep -Eq "Hardening assertion !empty\(\)|deque::(back|pop_back) called on an empty deque|SIGABRT|Aborted|Segmentation fault|SIGSEGV" "${log_file}" \
     || [ "${status}" -eq 134 ] \
     || [ "${status}" -eq 139 ]; then
    echo "abort detected: run ${i} (exit=${status}, phase=${phase})"
    echo "log: ${log_file}"
    if [ "${phase}" = "connected" ]; then
      connected_detected=$((connected_detected + 1))
    else
      disconnect_detected=$((disconnect_detected + 1))
    fi
    if [ "${STOP_ON_DETECT}" -eq 1 ]; then
      if [ "${status}" -eq 139 ]; then
        exit 139
      fi
      exit 134
    fi
    i=$((i + 1))
    sleep "${SLEEP_BETWEEN}"
    continue
  fi

  if [ ! -s "${log_file}" ] && [ "${status}" -ne 0 ]; then
    echo "non-zero exit with empty log: run ${i} (exit=${status})"
    echo "hint: run once under gdb to capture stack trace"
    echo "log: ${log_file}"
  fi

  if [ "${status}" -ne 0 ]; then
    echo "non-abort failure: run ${i} (exit=${status}), continuing"
    non_abort_failure=$((non_abort_failure + 1))
  else
    echo "ok: run ${i}"
  fi

  sleep "${SLEEP_BETWEEN}"
  i=$((i + 1))
done

echo "done: no abort detected in ${COUNT} runs"
echo "logs: ${LOG_DIR}"
echo "summary:"
echo "  abort during connected : ${connected_detected}"
echo "  abort during disconnect: ${disconnect_detected}"
echo "  non-abort failures     : ${non_abort_failure}"
