#!/bin/bash
# Run from the project root after `make`:   bash tests/crash.sh
ROOT=$PWD
WORK=/tmp/bank_crash
PORT=18083
CLIENT="$ROOT/bin/client 127.0.0.1 $PORT"
FAILS=0

check() {
    if grep -qF -- "$3" <<< "$2"; then printf '%-62s PASS\n' "$1"
    else printf '%-62s FAIL\n' "$1"; FAILS=$((FAILS+1)); fi
}
session() {
    local u=$1 p=$2; shift 2
    { echo "LOGIN $u $p"; for c in "$@"; do echo "$c"; done; echo "QUIT"; } | $CLIENT
}
start() {
    BANK_CRASH_AT=$1 "$ROOT/bin/server" $PORT >> "$WORK/server.log" 2>&1 &
    SERVER=$!
    for i in $(seq 50); do (echo > /dev/tcp/127.0.0.1/$PORT) 2>/dev/null && break; sleep 0.1; done
}
stop() { kill $SERVER 2>/dev/null; wait $SERVER 2>/dev/null; }
trap 'kill $SERVER 2>/dev/null' EXIT

for N in 0 1 2 3 4 5; do
    echo "--- transfer Rs 100, crash at point $N (0 = no crash) ---"
    rm -rf "$WORK"; mkdir -p "$WORK/data"; cd "$WORK" || exit 1
    "$ROOT/bin/seed" > /dev/null || exit 1
    start $N
    session alice alice123 "TRANSFER 2 100" > /dev/null 2>&1
    stop
    start 0                                   # restart: recovery runs here
    A=$(session alice alice123 "BALANCE" "HISTORY")
    B=$(session bob bob123 "BALANCE" "HISTORY")
    stop
    LOG=$(cat "$WORK/server.log")
    if [ "$N" -eq 1 ]; then
        check "crash before journaling: transfer did not happen (alice)" "$A" "Balance: Rs 1000.00"
        check "crash before journaling: transfer did not happen (bob)"   "$B" "Balance: Rs 1000.00"
    else
        check "alice debited exactly once"          "$A" "Balance: Rs 900.00"
        check "bob credited exactly once"           "$B" "Balance: Rs 1100.00"
        check "alice passbook has exactly 1 line"   "$A" "OK 1 transaction(s)"
        check "bob passbook has exactly 1 line"     "$B" "OK 1 transaction(s)"
    fi
    if [ "$N" -ge 2 ] && [ "$N" -le 4 ]; then
        check "recovery replayed the unfinished operation" "$LOG" "replayed 1"
    fi
done

echo
if [ $FAILS -eq 0 ]; then echo "Crash recovery test passed."
else echo "$FAILS check(s) FAILED. Server log: $WORK/server.log"; fi
