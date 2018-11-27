#ifndef __TREE_WALK_SPARSE__
#define __TREE_WALK_SPARSE__

struct commit;
struct rev_info;
typedef void (*show_edge_fn)(struct commit *);

void mark_edges_uninteresting_sparse(struct rev_info *revs,
				     show_edge_fn show_edge);

#endif
