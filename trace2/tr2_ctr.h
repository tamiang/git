#ifndef TR2_CTR_H
#define TR2_CTR_H

#include "trace2.h"
#include "trace2/tr2_tgt.h"

/*
 * Define a mechanism to allow global "counters".
 *
 * Counters can be used count interesting activity that does not fit
 * the "region and data" model, such as code called from many
 * different regions and/or where you want to count a number of items,
 * but don't have control of when the last item will be processed,
 * such as counter the number of calls to `lstat()`.
 *
 * Counters differ from Trace2 "data" events.  Data events are emitted
 * immediately and are appropriate for documenting loop counters and
 * etc.  Counter values are accumulated during the program and the final
 * counter value event is emitted at program exit.
 *
 * To make this model efficient, we define a compile-time fixed set
 * of counters and counter ids.  This lets us avoid the complexities
 * of dynamically allocating a counter and sharing that definition
 * with other threads.
 *
 * We define (at compile time) a set of "counter ids" to access the
 * various counters inside of a fixed size "counter block".
 *
 * A counter defintion table provides the counter category and name
 * so we can eliminate those arguments from the public counter API.
 * These are defined in a parallel tabel in `tr2_ctr.c`.
 *
 * Each thread has a private block of counters in its thread local
 * storage data so no locks are required for a thread to increment
 * it's version of the counter.  At program exit, the counter blocks
 * from all of the per-thread counters are added together to give the
 * final summary value for the each global counter.
 */

/*
 * The definition of an individual counter.
 */
struct tr2_counter {
	uint64_t value;

	unsigned int is_aggregate:1;
};

/*
 * Compile time fixed block of all defined counters.
 */
struct tr2_counter_block {
	struct tr2_counter counter[TRACE2_NUMBER_OF_COUNTERS];

	unsigned int is_aggregate:1;
};

/*
 * Add "value" to the global counter.
 */
void tr2_counter_increment(enum trace2_counter_id cid, uint64_t value);

/*
 * Accumulate counter data from the source block into the merged block.
 */
void tr2_merge_counter_block(struct tr2_counter_block *merged,
			       const struct tr2_counter_block *src);

/*
 * Send counter data for all counters in this block to the target.
 *
 * This will generate an event record for each counter that had activity.
 */
void tr2_emit_counter_block(tr2_tgt_evt_counter_t *pfn,
			    uint64_t us_elapsed_absolute,
			    const struct tr2_counter_block *blk,
			    const char *thread_name);

#endif /* TR2_CTR_H */
