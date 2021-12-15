#include "cache.h"
#include "thread-utils.h"
#include "trace2/tr2_tls.h"
#include "trace2/tr2_tmr.h"

#define MY_MAX(a, b) ((a) > (b) ? (a) : (b))
#define MY_MIN(a, b) ((a) < (b) ? (a) : (b))

/*
 * Define metadata for each stopwatch timer.  This list must match the
 * set defined in "enum trace2_timer_id".
 */
struct tr2_timer_def {
	const char *category;
	const char *name;

	unsigned int want_thread_events:1;
};

static struct tr2_timer_def tr2_timer_def_block[TRACE2_NUMBER_OF_TIMERS] = {
	[TRACE2_TIMER_ID_TEST1] = { "test", "test1", 0 },
	[TRACE2_TIMER_ID_TEST2] = { "test", "test2", 1 },
};

void tr2_start_timer(enum trace2_timer_id tid)
{
	struct tr2tls_thread_ctx *ctx = tr2tls_get_self();
	struct tr2_timer *t = &ctx->timers.timer[tid];

	t->recursion_count++;
	if (t->recursion_count > 1)
		return; /* ignore recursive starts */

	t->start_ns = getnanotime();
}

void tr2_stop_timer(enum trace2_timer_id tid)
{
	struct tr2tls_thread_ctx *ctx = tr2tls_get_self();
	struct tr2_timer *t = &ctx->timers.timer[tid];
	uint64_t ns_now;
	uint64_t ns_interval;

	assert(t->recursion_count > 0);

	t->recursion_count--;
	if (t->recursion_count)
		return; /* still in recursive call(s) */

	ns_now = getnanotime();
	ns_interval = ns_now - t->start_ns;

	t->total_ns += ns_interval;

	/*
	 * min_ns was initialized to zero (in the xcalloc()) rather
	 * than "(unsigned)-1" when the block of timers was allocated,
	 * so we should always set both the min_ns and max_ns values
	 * the first time that the timer is used.
	 */
	if (!t->interval_count) {
		t->min_ns = ns_interval;
		t->max_ns = ns_interval;
	} else {
		t->min_ns = MY_MIN(ns_interval, t->min_ns);
		t->max_ns = MY_MAX(ns_interval, t->max_ns);
	}

	t->interval_count++;
}

void tr2_merge_timer_block(struct tr2_timer_block *merged,
			   const struct tr2_timer_block *src)
{
	enum trace2_timer_id tid;

	for (tid = 0; tid < TRACE2_NUMBER_OF_TIMERS; tid++) {
		struct tr2_timer *t_merged = &merged->timer[tid];
		const struct tr2_timer *t = &src->timer[tid];

		t_merged->is_aggregate = 1;

		if (t->recursion_count) {
			/*
			 * A thread exited with a stopwatch running.
			 *
			 * NEEDSWORK: should we assert or throw a warning
			 * for the open interval.  I'm going to ignore it
			 * and keep going because we may have valid data
			 * for previously closed intervals on this timer.
			 *
			 * That is, I'm going to ignore the value of
			 * "now - start_ns".
			 */
		}

		if (!t->interval_count)
			continue; /* this timer was not used by this thread. */

		t_merged->total_ns += t->total_ns;

		if (!t_merged->interval_count) {
			t_merged->min_ns = t->min_ns;
			t_merged->max_ns = t->max_ns;
		} else {
			t_merged->min_ns = MY_MIN(t->min_ns, t_merged->min_ns);
			t_merged->max_ns = MY_MAX(t->max_ns, t_merged->max_ns);
		}

		t_merged->interval_count += t->interval_count;
	}

	merged->is_aggregate = 1;
}

void tr2_emit_timer_block(tr2_tgt_evt_timer_t *pfn,
			  uint64_t us_elapsed_absolute,
			  const struct tr2_timer_block *blk,
			  const char *thread_name)
{
	enum trace2_timer_id tid;

	for (tid = 0; tid < TRACE2_NUMBER_OF_TIMERS; tid++) {
		const struct tr2_timer *t = &blk->timer[tid];
		const struct tr2_timer_def *d = &tr2_timer_def_block[tid];

		if (!t->interval_count)
			continue; /* timer was not used */

		if (!d->want_thread_events && !t->is_aggregate)
			continue; /* per-thread events not wanted */

		pfn(us_elapsed_absolute, thread_name, d->category, d->name,
		    t->interval_count, t->total_ns, t->min_ns, t->max_ns);
	}
}
