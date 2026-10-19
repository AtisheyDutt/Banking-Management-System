#include <stdio.h>
#include "store.h"

/* Creates demo users. Safe to run twice (duplicates are simply refused). */
int main(void) {
    if (store_init() < 0) return 1;
    customer_create("alice", "alice123", "Alice Sharma", 100000, NULL, NULL);  /* Rs 1000.00 */
    customer_create("bob",   "bob123",   "Bob Verma",    100000, NULL, NULL);  /* Rs 1000.00 */
    customer_create("carol", "carol123", "Carol Dsouza",  50000, NULL, NULL);  /* Rs  500.00 */
    customer_create("dave",  "dave123",  "Dave Nair",    100000, NULL, NULL);  /* Rs 1000.00 */
    customer_create("erin",  "erin123",  "Erin Mehta",   100000, NULL, NULL);  /* Rs 1000.00 */
    user_create("emp1", "emp123", "Employee One", ROLE_EMPLOYEE, NULL);
    user_create("mgr1", "mgr123", "Manager One",  ROLE_MANAGER,  NULL);
    printf("Seeded: alice, bob, carol, dave, erin (customers), emp1, mgr1, admin (admin123)\n");
    store_close();
    return 0;
}
