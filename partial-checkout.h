#ifndef PARTIAL_CHECKOUT_H
#define PARTIAL_CHECKOUT_H

struct index_state;
struct repository;

int use_partial_checkout(struct repository *r);
char *get_partial_checkout_filename(struct repository *r);

/*
 * Update the CE_SKIP_WORKTREE bits based on the partial-checkout file.
 */
void apply_partial_checkout(struct repository *r, struct index_state *istate);

/*
 * Return 1 if the requested item is included by the partial-checkout file
 * 0 for not found and -1 for undecided.
 */
int is_included_in_partial_checkout(struct repository *r,
				    const char *pathname, int pathlen);

/*
 * Free the partial-checkout data structures.
 */
void free_partial_checkout(struct repository *r);

void get_partial_checkout_data(struct repository *r,
			       struct strbuf *pc_data);

#endif