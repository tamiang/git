#ifndef TR2_TM_H
#define TR2_TM_H

#include "trace2.h"
#include "trace2/tr2_tgt.h"

/*
 * Define a mechanism to allow "stopwatch" timers.
 *
 * Timers can be used to measure "interesting" activity that does not
 * fit the "region" model, such as code called from many different
 * regions (like zlib) and/or where data for individual calls are not
 * interesting or are too numerous to be efficiently logged.
 *
 * Timer values are accumulated during program execution and emitted
 * to the Trace2 logs at program exit.
 *
 * To make this model efficient, we define a compile-time fixed set of
 * timers and timer ids.  This lets us avoid the complexities of
 * dynamically allocating a timer on demand and sharing that
 * definition with other threads.
 *
 * Timer values are stored in a fixed size "timer block" inside thread
 * local storage.  This allows data to be collected on a
 * thread-by-thread basis without locking.
 *
 * Using this "timer block" model costs ~48 bytes per timer per thread
 * (we have about six uint64 fields per timer).  This does increase
 * the size of the thread local storage block, but it is allocated (at
 * thread create time) and not on the thread stack, so I'm not worried
 * about the size.  Using an array of timers in this block gives us
 * constant time access to each timer within each thread, so we don't
 * need to do expensive lookups (like hashmaps) to start/stop a timer.
 *
 * We define (at compile time) a set of "timer ids" to access the
 * various timers inside the fixed size "timer block".  See
 * `trace2_timer_id` in `trace2/trace2.h`.
 *
 * Timer definitions also include "category", "name", and similar
 * fields.  These are defined in a parallel table in `tr2_tmr.c` and
 * eliminate the need to include those args in the various timer APIs.
 *
 * Timer results are summarized and emitted by the main thread at
 * program exit by iterating over the global list of thread local
 * storage data blocks.
 */

/*
 * The definition of an individual timer and used by an individual
 * thread.
 */
struct tr2_timer {
	/*
	 * Total elapsed time for this timer in this thread in nanoseconds.
	 */
	uint64_t total_ns;

	/*
	 * The maximum and minimum interval values observed for this
	 * timer in this thread.
	 */
	uint64_t min_ns;
	uint64_t max_ns;

	/*
	 * The value of the clock when this timer was started in this
	 * thread.  (Undefined when the timer is not active in this
	 * thread.)
	 */
	uint64_t start_ns;

	/*
	 * Number of times that this timer has been started and stopped
	 * in this thread.  (Recursive starts are ignored.)
	 */
	size_t interval_count;

	/*
	 * Number of nested starts on the stack in this thread.  (We
	 * ignore recursive starts and use this to track the recursive
	 * calls.)
	 */
	unsigned int recursion_count;

	/*
	 * Has data from multiple threads been combined into this object.
	 */
	unsigned int is_aggregate:1;
};

/*
 * A compile-time fixed-size block of timers to insert into thread
 * local storage.
 *
 * We use this simple wrapper around the array of timer instances to
 * avoid C syntax quirks and the need to pass around an additional size_t
 * argument.
 */
struct tr2_timer_block {
	struct tr2_timer timer[TRACE2_NUMBER_OF_TIMERS];

	/*
	 * Has data from multiple threads been combined into this block.
	 */
	unsigned int is_aggregate:1;
};

/*
 * Private routines used by trace2.c to actually start/stop an individual
 * timer in the current thread.
 */
void tr2_start_timer(enum trace2_timer_id tid);
void tr2_stop_timer(enum trace2_timer_id tid);

/*
 * Accumulate timer data for all of the individual timers in the source
 * block into the corresponding timers in the merged block.
 *
 * This will aggregate data from one block (from an individual thread)
 * into the merge block.
 */
void tr2_merge_timer_block(struct tr2_timer_block *merged,
			   const struct tr2_timer_block *src);

/*
 * Send stopwatch data for all of the timers in this block to the
 * trace target destination.
 *
 * This will generate an event record for each timer in the block that
 * had activity during the program's execution.  (If this is called
 * with a per-thread block, we emit the per-thread data; if called
 * with a aggregate block, we emit summary data.)
 */
void tr2_emit_timer_block(tr2_tgt_evt_timer_t *pfn,
			  uint64_t us_elapsed_absolute,
			  const struct tr2_timer_block *blk,
			  const char *thread_name);

#endif /* TR2_TM_H */
