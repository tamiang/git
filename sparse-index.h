#ifndef SPARSE_INDEX_H__
#define SPARSE_INDEX_H__

struct index_state;

int convert_to_sparse(struct repository *repo, struct index_state *istate);

#endif
