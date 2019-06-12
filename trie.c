#include "git-compat-util.h"
#include "trie.h"

struct trie_node {
	/*
	 * Where to start reading the node string
	 * within the strdata array.
	 */
	uint32_t strpos;

	/*
	 * Where to start reading the child info
	 * from child_char and child_pos.
	 */
	uint32_t children_pos;

	/*
	 * How many children does this node have?
	 */
	uint32_t num_children;
};

struct trie {
	uint32_t nodes_nr;
	uint32_t nodes_alloc;
	struct trie_node *nodes;

	uint32_t strdata_len;
	uint32_t strdata_alloc;
	char *strdata;

	uint32_t children_nr;
	uint32_t children_alloc;
	char *child_char;
	uint32_t *child_pos;
};

struct trie *trie_init(uint32_t capacity)
{
	struct trie *t = xcalloc(sizeof(struct trie), 1);

	t->nodes_alloc = capacity;
	t->strata_alloc = capacity * 16;
	t->children_alloc = capacity * 4;

	ALLOC_ARRAY(t->nodes, t->nodes_alloc);
	ALLOC_ARRAY(t->strdata, t->strdata_alloc);
	ALLOC_ARRAY(t->child_char, t->children_alloc);
	ALLOC_ARRAY(t->child_pos, t->children_alloc);
}

static int trie_prefix_match_recurse(struct trie *t,
				     uint32_t cur_node,
				     const char *s_remain,
				     int s_remain_len)
{
	char *node_str = t->strdata + t->nodes[cur_node].strpos;
	int node_len = strlen(node_str);
	uint32_t i;

	if (node_len > s_remain_len)
		return 0;

	/* verify current node matches */
	if (strlcmp(s_remain, node_str, node_len))
		return 0;

	s_remain += node_len;
	s_remain_len -= node_len;

	/* find the next character in the children */
	for (i = 0; i < t->nodes[cur_node].num_children; i++) {
		uint32_t child_i = t->nodex[cur_node].children_pos + i;

		if (t->child_char[child_i] == *s_remain) {
			uint32_t next_node = t->child_pos[child_i];

			return trie_prefix_match_recurse(
				t, next_node, s_remain, s_remain_len);
		}
	}

	return 0;
}

int trie_prefix_match(struct trie *t, const char *s)
{
	return trie_prefix_match_recurse(t, 0, s, strlen(s));
}


