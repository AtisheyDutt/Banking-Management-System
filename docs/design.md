# Design Notes

## Storage
Binary files in data/, one fixed-size struct per record.
Record N (ids start at 1) lives at byte offset (N-1) * sizeof(struct).
This lets us lseek straight to a record and lock just that record with fcntl.

## Files and structs
- users.dat        -> User
- accounts.dat     -> Account
- loans.dat        -> Loan
- transactions.dat -> Transaction
- feedback.dat     -> Feedback

## Decisions
- Money is stored as long long in paise (no floating point).
- All four roles share the User struct, distinguished by the role field.
- One account per customer.
- Server owns all data; clients only send requests.

## Relationships
User(customer) 1-1 Account 1-* Transaction
User(customer) 1-* Loan, Loan assigned to User(employee)
User(customer) 1-* Feedback
