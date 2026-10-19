#!/bin/bash
# Run from the project root after `make`:   bash tests/e2e.sh
ROOT=$PWD
WORK=/tmp/bank_e2e
PORT=18080
CLIENT="$ROOT/bin/client 127.0.0.1 $PORT"
FAILS=0

rm -rf "$WORK"; mkdir -p "$WORK/data"; cd "$WORK" || exit 1
"$ROOT/bin/seed" > /dev/null || exit 1
"$ROOT/bin/server" $PORT > server.log 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT

for i in $(seq 50); do                      # wait until the server accepts connections
    (echo > /dev/tcp/127.0.0.1/$PORT) 2>/dev/null && break
    sleep 0.1
done

check() {   # label, text, expected substring
    if grep -qF -- "$3" <<< "$2"; then printf '%-62s PASS\n' "$1"
    else printf '%-62s FAIL\n' "$1"; FAILS=$((FAILS+1)); fi
}
session() {   # user password command...   (logs in, runs commands, quits)
    local u=$1 p=$2; shift 2
    { echo "LOGIN $u $p"; for c in "$@"; do echo "$c"; done; echo "QUIT"; } | $CLIENT
}
bulk() {      # user password count command   (one session sending the same command many times)
    { echo "LOGIN $1 $2"; for ((i=0; i<$3; i++)); do echo "$4"; done; echo "QUIT"; } | $CLIENT > /dev/null
}

echo "--- one customer, a normal session ---"
OUT=$(session alice alice123 "BALANCE" "DEPOSIT 250.50" "WITHDRAW 50" "TRANSFER 3 100" "BALANCE" "HISTORY" "LOGOUT")
check "login works"            "$OUT" "Welcome Alice Sharma. Role: CUSTOMER."
check "opening balance"        "$OUT" "Balance: Rs 1000.00"
check "deposit"                "$OUT" "Deposited Rs 250.50. New balance: Rs 1250.50"
check "withdraw"               "$OUT" "Withdrew Rs 50.00. New balance: Rs 1200.50"
check "transfer"               "$OUT" "Transferred Rs 100.00 to account 3."
check "balance after"          "$OUT" "Balance: Rs 1100.50"
check "history has 3 entries"  "$OUT" "OK 3 transaction(s), newest first"

echo "--- bad input is refused ---"
OUT=$(printf 'LOGIN alice wrong\nQUIT\n' | $CLIENT)
check "wrong password rejected"            "$OUT" "Invalid username or password."
OUT=$(printf 'BALANCE\nQUIT\n' | $CLIENT)
check "commands need a login first"        "$OUT" "Please LOGIN first."
OUT=$(session carol carol123 "WITHDRAW 99999" "DEPOSIT abc" "DEPOSIT 1.234" "TRANSFER 3 1" "TRANSFER 99 1" "BALANCE")
check "overdraw rejected"                  "$OUT" "Insufficient funds."
check "garbage amount rejected"            "$OUT" "Invalid amount."
check "transfer to a missing account"      "$OUT" "Account not found."
check "transfer to yourself rejected"      "$OUT" "Cannot transfer to the same account."
check "failures did not change the balance" "$OUT" "Balance: Rs 600.00"

echo "--- one session per user ---"
{ echo "LOGIN dave dave123"; sleep 3; echo "QUIT"; } | $CLIENT > "$WORK/first.txt" &
FIRST=$!
sleep 1
OUT=$(printf 'LOGIN dave dave123\nQUIT\n' | $CLIENT)
check "second login of the same user blocked" "$OUT" "already logged in elsewhere"
wait $FIRST
printf 'LOGIN erin erin123\n' | $CLIENT > /dev/null      # disconnects without LOGOUT or QUIT
sleep 0.5
OUT=$(session erin erin123 "BALANCE")
check "dropped connection frees the session"  "$OUT" "Welcome Erin Mehta"

echo "--- many clients at the same time ---"
PIDS=()
for u in alice bob dave erin; do
    bulk $u ${u}123 50 "TRANSFER 3 1" &
    PIDS+=($!)
done
wait "${PIDS[@]}"
OUT=$(session carol carol123 "BALANCE")
check "4 clients x 50 transfers all arrived (carol 800.00)" "$OUT" "Balance: Rs 800.00"

bulk alice alice123 50 "TRANSFER 2 1" & P1=$!
bulk bob   bob123   50 "TRANSFER 1 1" & P2=$!
wait $P1 $P2
OUT=$(session alice alice123 "BALANCE")
check "opposite transfers: no deadlock, alice unchanged"   "$OUT" "Balance: Rs 1050.50"
OUT=$(session bob bob123 "BALANCE")
check "opposite transfers: no deadlock, bob unchanged"     "$OUT" "Balance: Rs 950.00"

echo
if [ $FAILS -eq 0 ]; then echo "All end-to-end checks passed."
else echo "$FAILS check(s) FAILED. Server log: $WORK/server.log"; fi
