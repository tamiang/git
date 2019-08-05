#ifndef SPARSE_CHECKOUT_H
#define SPARSE_CHECKOUT_H

struct exclude_list;
struct index_state;
struct repository;
struct strbuf;

int use_sparse_checkout(struct repository *r);
int get_sparse_checkout_data(char *sparse_filename,
			     struct exclude_list *ed);

/*
 * Given an exclude_list, scan the list to discover if
 * the patterns match the "fast cone" patterns. That is,
 * we expect to see a set of patterns such as
 *
 * Type 1:
 *
 * <directory>/[*]
 * !<directory>/[*]/[*]
 *
 * and Type 2:
 *
 * <directory>/[*]
 *
 * (Ignore brackets around asterisks. They exist to avoid
 * build breaks.)
 *
 * The Type 1 pattern pairs say "I want all files in
 * directory, and none of the subdirectories". The
 * Type 2 pattern says "I want every file in this directory,
 * recursively through the subdirectories". These patterns
 * appear in an ordered list, and if <dir1> is an ancestor
 * of <dir2>, then a Type 1 pattern for <dir1> should appear
 * before either pattern for <dir2>.
 *
 * excludes_are_strict() returns 1 exactly when all patterns
 * match those type above, including the order restriction.
 */
int excludes_are_strict(struct exclude_list *el);

#endif
