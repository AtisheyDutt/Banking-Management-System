#ifndef SERVICES_H
#define SERVICES_H

#include "models.h"

/* Result codes (a positive return from an apply/add function is the new id) */
#define SVC_OK               0
#define SVC_ERR            (-1)   /* I/O or locking failure */
#define SVC_NOT_FOUND      (-2)
#define SVC_INVALID        (-3)   /* bad input or wrong kind of user */
#define SVC_BAD_STATE      (-4)   /* loan is not in the right state for this action */
#define SVC_NOT_ALLOWED    (-5)   /* loan is assigned to a different employee */
#define SVC_ACCOUNT_PROBLEM (-6)  /* customer account missing or deactivated */

/* ----- Loans (customer_id and employee_id are USER ids) ----- */
int loan_apply(int customer_id, long long amount, Loan *out);       /* new loan id or SVC_* */
int loan_get(int loan_id, Loan *out);                               /* SVC_OK or SVC_NOT_FOUND */
int loan_assign(int loan_id, int employee_id);                      /* manager: PENDING -> ASSIGNED */
int loan_decide(int loan_id, int employee_id, int approve);         /* employee: ASSIGNED -> APPROVED/REJECTED */

/* Filter loans. status < 0 means any status; employee_id <= 0 means any employee.
 * Returns the number stored in out[] (up to max). */
int loan_list(int status, int employee_id, Loan *out, int max);

/* ----- Feedback ----- */
int feedback_add(int customer_id, const char *message);             /* new id or SVC_* */
int feedback_list(Feedback *out, int max);                          /* NEWEST FIRST, count or SVC_ERR */

/* ----- Manager: activate / deactivate a customer account ----- */
int account_set_active(int account_id, int active);

#endif
