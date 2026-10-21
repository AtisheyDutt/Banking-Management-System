#!/bin/bash
# Run from the project root after `make`:   bash tests/stress.sh
ROOT=$PWD
WORK=/tmp/bank_stress
PORT=18082
CLIENT="$ROOT/bin/client 127.0.0.1 $PORT"
ROUNDS=${ROUNDS:-40}
FAILS=0

rm -rf "$WORK"; mkdir -p "$WORK/data"; cd "$WORK" || exit 1
"$ROOT/bin/seed" > /dev/null || exit 1
"$ROOT/bin/server" $PORT > server.log 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT
for i in $(seq 50); do
    (echo > /dev/tcp/127.0.0.1/$PORT) 2>/dev/null && break
    sleep 0.1
done

check() {
    if [ "$2" = "$3" ]; then printf '%-58s PASS\n' "$1"
    else printf '%-58s FAIL (got "%s", expected "%s")\n' "$1" "$2" "$3"; FAILS=$((FAILS+1)); fi
}
total() {   # sum of all five customers' balances
    local sum=0 b
    for u in alice bob carol dave erin; do
        b=$({ echo "LOGIN $u ${u}123"; echo "BALANCE"; echo "QUIT"; } | $CLIENT | grep -oE 'Balance: Rs [0-9.]+' | awk '{print $3}')
        sum=$(awk -v a="$sum" -v b="$b" 'BEGIN{printf "%.2f", a+b}')
    done
    echo "$sum"
}
worker() {  # user next_account
    { echo "LOGIN $1 ${1}123"
      for ((i=0; i<ROUNDS; i++)); do echo "DEPOSIT 2"; echo "WITHDRAW 1"; echo "TRANSFER $2 1"; done
      echo "QUIT"; } | $CLIENT > /dev/null
}

echo "--- mixed load: 5 clients x $ROUNDS rounds ---"
check "total before" "$(total)" "4500.00"
worker alice 2 & worker bob 3 & worker carol 4 & worker dave 5 & worker erin 1 &
for p in $(jobs -p); do [ "$p" != "$SERVER" ] && wait "$p"; done
EXPECT=$(awk -v n="$ROUNDS" 'BEGIN{printf "%.2f", 4500 + 5*n}')
check "money conserved after concurrent load" "$(total)" "$EXPECT"

echo "--- login race: 8 simultaneous logins as one user ---"
rm -f ok_*.txt
for i in $(seq 8); do
    ( { echo "LOGIN alice alice123"; sleep 2; echo "QUIT"; } | $CLIENT > ok_$i.txt ) &
done
for p in $(jobs -p); do [ "$p" != "$SERVER" ] && wait "$p"; done
WINNERS=$(grep -l "Welcome Alice" ok_*.txt | wc -l)
check "exactly one of 8 simultaneous logins succeeds" "$WINNERS" "1"

echo
if [ $FAILS -eq 0 ]; then echo "Stress test passed."
else echo "$FAILS check(s) FAILED. Server log: $WORK/server.log"; fi
