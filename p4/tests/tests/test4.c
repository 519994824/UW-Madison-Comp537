#include "types.h"
#include "stat.h"
#include "user.h"
#include "pstat.h"
#include "test_helper.h"
 
int main(int argc, char* argv[]) {
    struct pstat ps;
 
    // Set minimum tickets (1)
    ASSERT(settickets(1) != -1, "settickets failed for minimum tickets");
 
    // check curproc stat
    if (getpinfo(&ps) != 0) {
        printf(1, "Failed to get process info\n");
        exit();
    }
 
    // check current proc index
    int my_idx = find_my_stats_index(&ps);
    ASSERT(my_idx != -1, "Could not get process stats from pgetinfo");
 
    // check ticket amout 1
    ASSERT(ps.tickets[my_idx] == 1, "Expected tickets to be 1, but got %d", ps.tickets[my_idx]);
 
    // Set maximum tickets (32)
    ASSERT(settickets(32) != -1, "settickets failed for maximum tickets");
 
    // check curproc stat again
    if (getpinfo(&ps) != 0) {
        printf(1, "Failed to get process info\n");
        exit();
    }
 
    // check curproc index again
    my_idx = find_my_stats_index(&ps);
    ASSERT(my_idx != -1, "Could not get process stats from pgetinfo");
 
    // confirm ticket amout 32
    ASSERT(ps.tickets[my_idx] == 32, "Expected tickets to be 32, but got %d", ps.tickets[my_idx]);
 
    test_passed();
    exit();
}
