#ifndef SNAPSHOTS_H
#define SNAPSHOTS_H

struct repository;
int create_ref_snapshot(const struct repository *repo);

#endif /* SNAPSHOTS_H */
