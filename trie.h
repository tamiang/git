#ifndef __TRIE_H__
#define __TRIE_H__

struct trie;

struct trie *trie_init(uint32_t capacity);

/*
 * Returns 1 if a prefix of 's' is contained in 't'.
 * Returns 0 otherwise.
 */
int trie_prefix_match(struct trie *t, const char *s);

/*
 * Generate a trie from a file containing a sorted list of strings,
 * separated by newline characters (\n). Will return NULL if there
 * are errors reading the file, or if the file is not sorted.
 */
struct trie *trie_build_from_file(struct trie *t, const char *fname);

#endif
