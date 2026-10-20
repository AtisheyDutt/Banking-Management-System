#!/bin/bash
# Run from the project root after `make`:   bash tests/e2e_roles.sh
ADMIN_USER=${ADMIN_USER:-admin}
ADMIN_PASS=${ADMIN_PASS:-admin123}

ROOT=$PWD
WORK=/tmp/bank_roles
PORT=18081
CLIENT="$ROOT/bin/client 127.0.0.1 $PORT"
FAILS=0; TOTAL=0

rm -rf "$WORK"; mkdir -p "$WORK/data"; cd "$WORK" || exit 1
"$ROOT/bin/seed" > /dev/null || exit 1
"$ROOT/bin/server" $PORT > server.log 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT
for i in $(seq 50); do
    (echo > /dev/tcp/127.0.0.1/$PORT) 2>/dev/null && break
    sleep 0.1
done

check() {      # label, text, expected substring
    TOTAL=$((TOTAL+1))
    if grep -qF -- "$3" <<< "$2"; then printf '%-62s PASS\n' "$1"
    else printf '%-62s FAIL\n' "$1"; echo "   transcript: $2" | head -c 600; echo; FAILS=$((FAILS+1)); fi
}
check_not() {  # label, text, substring that must NOT appear
    TOTAL=$((TOTAL+1))
    if grep -qF -- "$3" <<< "$2"; then printf '%-62s FAIL\n' "$1"; echo "   transcript: $2" | head -c 600; echo; FAILS=$((FAILS+1))
    else printf '%-62s PASS\n' "$1"; fi
}
session() {    # user password command...
    local u=$1 p=$2; shift 2
    { echo "LOGIN $u $p"; for c in "$@"; do echo "$c"; done; echo "QUIT"; } | $CLIENT
}
id_of() {      # role username  -> numeric user id taken from LISTUSERS
    session "$ADMIN_USER" "$ADMIN_PASS" "LISTUSERS $1" | grep -w "$2" | grep -oE '[0-9]+' | head -1
}

echo "--- administrator ---"
OUT=$(session "$ADMIN_USER" "$ADMIN_PASS" "ADDEMPLOYEE empa emp123 Emp Alpha" "ADDEMPLOYEE mgra mgr123 Mgr Alpha" "ADDEMPLOYEE empa emp123 Emp Alpha" "LISTUSERS EMPLOYEE" "LISTUSERS BOGUS" "SETACTIVE 1")
check "admin login"                       "$OUT" "Role: ADMIN."
check "add employee, listed afterwards"   "$OUT" "empa"
check "duplicate username refused"        "$OUT" "That username is already taken."
check "bad role name refused"             "$OUT" "Role must be CUSTOMER, EMPLOYEE, MANAGER or ADMIN."
check "wrong argument count -> usage"     "$OUT" "Usage: SETACTIVE <user_id> <0|1>"

EMPA=$(id_of EMPLOYEE empa); MGRA=$(id_of EMPLOYEE mgra); ADMID=$(id_of ADMIN "$ADMIN_USER")
OUT=$(session "$ADMIN_USER" "$ADMIN_PASS" "MODUSER $EMPA - Emp Renamed" "LISTUSERS EMPLOYEE" "PROMOTE $MGRA" "LISTUSERS MANAGER" "PROMOTE $MGRA" "DEMOTE 99999" "SETACTIVE $ADMID 0")
check "modify an employee's name"         "$OUT" "Emp Renamed"
check "promote employee to manager"       "$OUT" "mgra"
check "promote a non-employee refused"    "$OUT" "That user is not an employee"
check "demote a missing user refused"     "$OUT" "User not found."
check "admin cannot deactivate itself"    "$OUT" "The administrator account cannot be deactivated."

echo "--- employee ---"
OUT=$(session empa emp123 "ADDCUSTOMER frank frank123 2000 Frank Rao" "ADDCUSTOMER frank x 10 Dup" "LISTCUSTOMERS" "MYLOANS" "APPROVE 999" "APPROVE abc" "CUSTHISTORY 99" "CUSTHISTORY 1" "ADDEMPLOYEE hack h 1 Hack")
check "employee login"                    "$OUT" "Role: EMPLOYEE."
check "add customer, listed afterwards"   "$OUT" "frank"
check "duplicate customer refused"        "$OUT" "That username is already taken."
check "approve a missing loan"            "$OUT" "Loan not found."
check "bad loan id refused"               "$OUT" "Invalid loan id."
check "history of a missing account"      "$OUT" "Account not found."
check "employee cannot use admin command" "$OUT" "ERR"

echo "--- the new customer and loans ---"
OUT=$(session frank frank123 "BALANCE" "APPLYLOAN 5000" "APPLYLOAN abc" "FEEDBACK Great service" "ADDCUSTOMER x x 1 X")
check "new customer login"                "$OUT" "Role: CUSTOMER."
check "opening balance from employee"     "$OUT" "Balance: Rs 2000.00"
check "bad loan amount refused"           "$OUT" "Invalid amount."
check "customer cannot add customers"     "$OUT" "ERR"
OUT=$(session empa emp123 "APPROVE 1")
check "unassigned employee cannot approve" "$OUT" "That loan is not assigned to you."

echo "--- manager ---"
OUT=$(session mgra mgr123 "LISTCUSTOMERS" "EMPLOYEES" "PENDINGLOANS" "FEEDBACKS" "LISTCUSTOMERS extra" "ASSIGN 1")
check "manager login after promotion"     "$OUT" "Role: MANAGER."
check "manager sees customers"            "$OUT" "frank"
check "manager sees employees"            "$OUT" "empa"
check "manager sees customer feedback"    "$OUT" "Great service"
check "extra argument -> usage"           "$OUT" "Usage: LISTCUSTOMERS"
check "ASSIGN with missing arg -> usage"  "$OUT" "Usage: ASSIGN <loan_id> <employee_user_id>"

OUT=$(session mgra mgr123 "ASSIGN 1 $EMPA" "ASSIGN 999 $EMPA")
check "assign a loan to an employee"      "$OUT" "OK"
check "assign a missing loan refused"     "$OUT" "ERR"

echo "--- loan approval, end to end ---"
OUT=$(session empa emp123 "MYLOANS" "APPROVE 1" "APPROVE 1")
check "assigned employee sees the loan"   "$OUT" "5000"
check "second decision refused"           "$OUT" "That loan is not awaiting a decision."
OUT=$(session frank frank123 "BALANCE")
check "approved loan credited the account" "$OUT" "Balance: Rs 7000.00"

echo "--- account activation ---"
OUT=$(session mgra mgr123 "DEACTIVATE 6" "ACTIVATE 6" "DEACTIVATE abc" "DEACTIVATE 999")
check "deactivate an account"             "$OUT" "OK"
check "bad account number refused"        "$OUT" "Invalid account number."
check "missing account refused"           "$OUT" "Account not found."
OUT=$(session mgra mgr123 "DEACTIVATE 6")
OUT=$(session frank frank123 "DEPOSIT 10")
check "deactivated account cannot transact" "$OUT" "ERR"
OUT=$(session mgra mgr123 "ACTIVATE 6")
OUT=$(session frank frank123 "DEPOSIT 10")
check "reactivated account works again"   "$OUT" "Deposited Rs 10.00"

echo
if [ $FAILS -eq 0 ]; then echo "All $TOTAL role checks passed."
else echo "$FAILS of $TOTAL check(s) FAILED. Server log: $WORK/server.log"; fi
