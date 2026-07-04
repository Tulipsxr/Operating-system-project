#!/usr/bin/env bash
set -euo pipefail

cleanup() {
    if [[ -n "${SERVER_PID-}" ]]; then
        kill "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

echo "Cleaning and building binaries..."
make clean
make all

echo "Generating 1GB random test file original.dat..."
rm -f original.dat
if ! dd if=/dev/urandom of=original.dat bs=1M count=1024 status=none; then
    echo "dd failed to generate original.dat" >&2
    exit 1
fi

echo "Starting server in background..."
./server original.dat &
SERVER_PID=$!
sleep 2

echo "Running client..."
./client -p 4

echo "Waiting for server to finish..."
wait "${SERVER_PID}"
unset SERVER_PID

echo "Checking file integrity..."
if [[ ! -f original.dat ]]; then
    echo "[FAIL] original.dat not found" >&2
    exit 1
fi
if [[ ! -f reassembled.dat ]]; then
    echo "[FAIL] reassembled.dat not found" >&2
    exit 1
fi

orig_md5=$(md5sum original.dat | awk '{print $1}')
reass_md5=$(md5sum reassembled.dat | awk '{print $1}')
if [[ "$orig_md5" == "$reass_md5" ]]; then
    echo "[PASS] original.dat matches reassembled.dat"
else
    echo "[FAIL] original.dat does not match reassembled.dat" >&2
    exit 1
fi

if [[ -f result_min.txt ]]; then
    echo "result_min.txt contents:"
    cat result_min.txt
else
    echo "[FAIL] result_min.txt not found" >&2
    exit 1
fi

if [[ -f result_max.txt ]]; then
    echo "result_max.txt contents:"
    cat result_max.txt
else
    echo "[FAIL] result_max.txt not found" >&2
    exit 1
fi

if [[ -f result_sorted.dat ]]; then
    orig_size=$(stat -c%s original.dat)
    sorted_size=$(stat -c%s result_sorted.dat)
    echo "original.dat size: ${orig_size} bytes"
    echo "result_sorted.dat size: ${sorted_size} bytes"
    if [[ "$orig_size" != "$sorted_size" ]]; then
        echo "[FAIL] result_sorted.dat size does not match original.dat" >&2
        exit 1
    fi
else
    echo "[FAIL] result_sorted.dat not found" >&2
    exit 1
fi

if [[ -f execution_log.txt ]]; then
    echo "execution_log.txt contents:"
    cat execution_log.txt
else
    echo "[FAIL] execution_log.txt not found" >&2
    exit 1
fi

echo "[ALL TESTS PASSED SUCCESSFULLY]"
