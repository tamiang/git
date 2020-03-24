#ifndef SPARSE_CHECKOUT_H
#define SPARSE_CHECKOUT_H

#include "cache.h"
#include "repository.h"
#include "string-list.h"

char *get_sparse_checkout_filename(void);
int load_sparse_checkout_patterns(struct pattern_list *pl);
void write_patterns_to_file(FILE *fp, struct pattern_list *pl);
int update_working_directory(struct pattern_list *pl);
int write_patterns_and_update(struct pattern_list *pl);
void insert_recursive_pattern(struct pattern_list *pl, struct strbuf *path);
void strbuf_to_cone_pattern(struct strbuf *line, struct pattern_list *pl);

/*
 * For a repository, look at the sparse-checkout.inTree config
 * list and gather a list of paths to use for specifying the
 * sparse-checkout cone. Populate the given string list with
 * those path names. The string list is expected to duplicate
 * strings on add.
 *
 * Returns 1 on error, 0 otherwise.
 */
int load_in_tree_from_config(struct repository *r, struct string_list *sl);
int load_in_tree_pattern_list(struct index_state *istate, struct string_list *sl, struct pattern_list *pl);
int set_in_tree_config(struct repository *r, struct string_list *sl);

int update_in_tree_sparse_checkout(struct repository *r, struct index_state *istate);

#endif
