#include "cache.h"
#include "thread-utils.h"
#include "trace2/tr2_tls.h"
#include "trace2/tr2_ctr.h"

/*
 * Define metadata for each global counter.  This list must match the
 * set defined in "enum trace2_counter_id".
 */
struct tr2_counter_def {
	const char *category;
	const char *name;

	unsigned int want_thread_events:1;
};

static struct tr2_counter_def tr2_counter_def_block[TRACE2_NUMBER_OF_COUNTERS] = {
	[TRACE2_COUNTER_ID_TEST1] = { "test", "test1", 0 },
	[TRACE2_COUNTER_ID_TEST2] = { "test", "test2", 1 },
};

void tr2_counter_increment(enum trace2_counter_id cid, uint64_t value)
{
	struct tr2tls_thread_ctx *ctx = tr2tls_get_self();
	struct tr2_counter *c = &ctx->counters.counter[cid];

	c->value += value;
}

void tr2_merge_counter_block(struct tr2_counter_block *merged,
			     const struct tr2_counter_block *src)
{
	enum trace2_counter_id cid;

	for (cid = 0; cid < TRACE2_NUMBER_OF_COUNTERS; cid++) {
		struct tr2_counter *c_merged = &merged->counter[cid];
		const struct tr2_counter *c = &src->counter[cid];

		c_merged->is_aggregate = 1;

		c_merged->value += c->value;
	}

	merged->is_aggregate = 1;
}

void tr2_emit_counter_block(tr2_tgt_evt_counter_t *pfn,
			    uint64_t us_elapsed_absolute,
			    const struct tr2_counter_block *blk,
			    const char *thread_name)
{
	enum trace2_counter_id cid;

	for (cid = 0; cid < TRACE2_NUMBER_OF_COUNTERS; cid++) {
		const struct tr2_counter *c = &blk->counter[cid];
		const struct tr2_counter_def *d = &tr2_counter_def_block[cid];

		if (!c->value)
			continue; /* counter was not used */

		if (!d->want_thread_events && !c->is_aggregate)
			continue; /* per-thread events not wanted */

		pfn(us_elapsed_absolute, thread_name, d->category, d->name,
		    c->value);
	}
}
