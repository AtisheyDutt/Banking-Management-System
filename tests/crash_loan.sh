#!/bin/bash
# Run from the project root after `make`:   bash tests/crash_loan.sh
ROOT=$PWD
WORK=/tmp/bank_crash_loan
PORT=18084
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
id_of() { session admin admin123 "LISTUSERS $1" | grep -w "$2" | grep -oE '[0-9]+' | head -1; }
trap 'kill $SERVER 2>/dev/null' EXIT

for N in 1 2 3 4 5; do
    echo "--- approve a Rs 3000 loan, crash at point $N ---"
    rm -rf "$WORK"; mkdir -p "$WORK/data"; cd "$WORK" || exit 1
    "$ROOT/bin/seed" > /dev/null || exit 1
    start 0
    session admin admin123 "ADDEMPLOYEE empx emp123 Emp X" "ADDEMPLOYEE mgrx mgr123 Mgr X" > /dev/null
    EMPX=$(id_of EMPLOYEE empx); MGRX=$(id_of EMPLOYEE mgrx)
    session admin admin123 "PROMOTE $MGRX" > /dev/null
    session alice alice123 "APPLYLOAN 3000" > /dev/null
    session mgrx mgr123 "ASSIGN 1 $EMPX" > /dev/null
    stop

    start $N
    session empx emp123 "APPROVE 1" > /dev/null 2>&1      # this session dies at point N
    stop

    start 0                                                # restart: recovery runs here
    SECOND=$(session empx emp123 "APPROVE 1")              # a retry, as an impatient employee would
    A=$(session alice alice123 "BALANCE" "HISTORY")
    stop

    check "customer credited exactly once (Rs 4000.00)"  "$A" "Balance: Rs 4000.00"
    check "exactly one loan-credit line in the passbook" "$A" "OK 1 transaction(s)"
    if [ "$N" -ge 2 ]; then
        check "loan is already approved, retry refused"  "$SECOND" "not awaiting a decision"
    fi
done

echo
if [ $FAILS -eq 0 ]; then echo "Loan crash recovery test passed."
else echo "$FAILS check(s) FAILED. Server log: $WORK/server.log"; fi
