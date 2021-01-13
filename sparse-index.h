#ifndef SPARSE_INDEX_H__
#define SPARSE_INDEX_H__

int set_sparse_index_config(struct repository *repo, int enable);
int convert_to_sparse(struct index_state *istate);

#endif