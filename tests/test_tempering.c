#include "test_util.h"
#include "tempering.h"

int main(void)
{
    CHECK_CLOSE(tempering_log_ratio(1.0, 2.0, 1.5, 3.0), 1.5 + 3.0 - 1.0 - 2.0, 0.0);

    CHECK(tempering_accept(0.0, 0.999999) == 1);
    CHECK(tempering_accept(0.3, 0.999999) == 1);
    CHECK(tempering_accept(log(0.25), 0.2499) == 1);
    CHECK(tempering_accept(log(0.25), 0.2501) == 0);
    CHECK(tempering_accept(-INFINITY, 0.0) == -1);
    CHECK(tempering_accept(NAN, 0.5) == -1);
    CHECK(tempering_accept(INFINITY, 0.5) == -1);

    CHECK(tempering_first_pair(0) == 0);
    CHECK(tempering_first_pair(1) == 1);
    CHECK(tempering_first_pair(6) == 0);

    TemperingStats st;
    CHECK(tempering_stats_alloc(&st, 1) != 0);  /* a ladder needs >= 2 slots */
    CHECK(tempering_stats_alloc(&st, 3) == 0);
    for (int k = 0; k < 3; k++) {
        CHECK(st.walker_at[k] == k);
        CHECK(st.direction[k] == 0);
        CHECK(st.trip_state[k] == 0);
    }
    /* initial ends: walker 0 at hot is armed, walker 2 at cold is not */
    tempering_stats_update_ends(&st);
    CHECK(st.direction[0] == 1 && st.direction[2] == -1 && st.direction[1] == 0);
    CHECK(st.trip_state[0] == 1 && st.trip_state[2] == 0);

    /* (a) cold-start one-way: walker 2 goes cold -> hot, not a round trip */
    tempering_stats_record(&st, 1, 1);          /* [0,2,1] */
    tempering_stats_update_ends(&st);
    tempering_stats_record(&st, 0, 1);          /* [2,0,1] */
    tempering_stats_update_ends(&st);
    CHECK(st.walker_at[0] == 2);
    CHECK(st.round_trips[2] == 0 && st.trip_state[2] == 1);

    /* (b) interior -> cold -> hot: walker 1 never started at hot, not a trip */
    tempering_stats_record(&st, 1, 1);          /* [2,1,0] */
    tempering_stats_update_ends(&st);
    CHECK(st.trip_state[0] == 2);               /* walker 0: hot, then cold */
    tempering_stats_record(&st, 0, 1);          /* [1,2,0] */
    tempering_stats_update_ends(&st);
    CHECK(st.walker_at[0] == 1);
    CHECK(st.round_trips[1] == 0 && st.trip_state[1] == 1);

    /* (c) full trip: walker 0 hot -> cold -> hot counts once */
    tempering_stats_record(&st, 1, 1);          /* [1,0,2] */
    tempering_stats_update_ends(&st);
    tempering_stats_record(&st, 0, 1);          /* [0,1,2] */
    tempering_stats_update_ends(&st);
    CHECK(st.walker_at[0] == 0);
    CHECK(st.round_trips[0] == 1 && st.trip_state[0] == 1 && st.direction[0] == 1);

    /* (d) staying at an end adds nothing */
    tempering_stats_update_ends(&st);
    CHECK(st.round_trips[0] == 1);
    CHECK(st.attempts[0] == 3 && st.accepted[0] == 3);
    CHECK(st.attempts[1] == 3 && st.accepted[1] == 3);

    /* rejected attempts count but do not move walkers */
    tempering_stats_record(&st, 0, 0);
    CHECK(st.attempts[0] == 4 && st.accepted[0] == 3 && st.walker_at[0] == 0);

    /* sampling by direction label */
    tempering_stats_sample(&st);
    CHECK(st.n_up[0] == 1 && st.n_down[0] == 0);   /* walker 0 at slot 0: +1 */
    CHECK(st.n_down[2] == 1);                       /* walker 2 at slot 2: -1 */

    /* (e) measurement boundary: walker 2 was armed and visited cold during
       warmup; after re-arming, its return to hot is not a completed trip */
    CHECK(st.trip_state[2] == 2);
    tempering_stats_begin_measurement(&st);
    CHECK(st.attempts[0] == 0 && st.round_trips[0] == 0 && st.n_up[0] == 0);
    CHECK(st.walker_at[0] == 0 && st.direction[0] == 1);  /* walkers and labels kept */
    CHECK(st.trip_state[0] == 1 && st.trip_state[1] == 0 && st.trip_state[2] == 0);
    tempering_stats_record(&st, 1, 1);          /* [0,2,1] */
    tempering_stats_update_ends(&st);
    tempering_stats_record(&st, 0, 1);          /* [2,0,1] */
    tempering_stats_update_ends(&st);
    CHECK(st.round_trips[2] == 0 && st.trip_state[2] == 1);
    tempering_stats_free(&st);
    TEST_END();
}
