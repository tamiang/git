#ifndef SPARSE_CHECKOUT_H
#define SPARSE_CHECKOUT_H

#include "cache.h"
#include "repository.h"

char *get_sparse_checkout_filename(void);
void write_patterns_to_file(FILE *fp, struct pattern_list *pl);
int update_working_directory(struct pattern_list *pl);
int write_patterns_and_update(struct pattern_list *pl);
void insert_recursive_pattern(struct pattern_list *pl, struct strbuf *path);
void strbuf_to_cone_pattern(struct strbuf *line, struct pattern_list *pl);

#endif
