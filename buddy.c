#include "buddy.h"
#include <string.h>

#define NULL ((void *)0)
#define MAX_RANK 16
#define PAGE_SIZE 4096
#define MAX_PAGES (1 << 20)

/* ---- global state ---- */
static char  is_boundary[MAX_PAGES];      /* 1 if this page is a block start */
static char  block_rank[MAX_PAGES];       /* rank of the block (1..16) */
static char  block_alloc[MAX_PAGES];      /* 1 if allocated, 0 if free */
static int   free_head[MAX_RANK + 1];     /* head of free list per rank, -1 = empty */
static int   free_next[MAX_PAGES];        /* next pointer in free list */
static int   free_cnt[MAX_RANK + 1];      /* count of free blocks per rank */

static void *pool_start;
static int   total_pages;

/* ---- free-list helpers ---- */
static void fl_add(int rank, int idx) {
    free_next[idx] = free_head[rank];
    free_head[rank] = idx;
    free_cnt[rank]++;
}

static int fl_pop(int rank) {
    int idx = free_head[rank];
    if (idx < 0) return -1;
    free_head[rank] = free_next[idx];
    free_next[idx] = -1;
    free_cnt[rank]--;
    return idx;
}

static void fl_remove(int rank, int idx) {
    int prev = -1, cur = free_head[rank];
    while (cur >= 0) {
        if (cur == idx) {
            if (prev < 0)
                free_head[rank] = free_next[cur];
            else
                free_next[prev] = free_next[cur];
            free_next[cur] = -1;
            free_cnt[rank]--;
            return;
        }
        prev = cur;
        cur = free_next[cur];
    }
}

/* ---- implementation ---- */

int init_page(void *p, int pgcount) {
    int r, page_idx, remaining;

    if (!p || pgcount <= 0 || pgcount > MAX_PAGES)
        return -EINVAL;

    pool_start = p;
    total_pages = pgcount;

    /* clear all state */
    memset(is_boundary, 0, (size_t)total_pages);
    memset(block_rank,  0, (size_t)total_pages);
    memset(block_alloc,  0, (size_t)total_pages);
    memset(free_head, -1, sizeof(free_head));
    memset(free_next, -1, sizeof(free_next));
    memset(free_cnt,   0, sizeof(free_cnt));

    /* decompose pgcount into power-of-2 blocks, largest rank first */
    remaining = pgcount;
    page_idx  = 0;
    for (r = MAX_RANK; r >= 1 && remaining > 0; r--) {
        int blk_sz = 1 << (r - 1);          /* pages in a rank-r block */
        while (remaining >= blk_sz) {
            is_boundary[page_idx] = 1;
            block_rank[page_idx]  = (char)r;
            block_alloc[page_idx] = 0;
            fl_add(r, page_idx);
            page_idx  += blk_sz;
            remaining -= blk_sz;
        }
    }

    return OK;
}

void *alloc_pages(int rank) {
    int page_idx, r;

    if (rank < 1 || rank > MAX_RANK)
        return ERR_PTR(-EINVAL);

    /* try exact match first */
    page_idx = fl_pop(rank);
    if (page_idx < 0) {
        /* find a larger block to split */
        for (r = rank + 1; r <= MAX_RANK; r++) {
            page_idx = fl_pop(r);
            if (page_idx >= 0) break;
        }
        if (page_idx < 0)
            return ERR_PTR(-ENOSPC);       /* nothing large enough */

        /* split down from r-1 to rank */
        for ( ; r > rank; r--) {
            int buddy = page_idx + (1 << (r - 2));   /* half of current block */
            /* right half becomes a free block of rank r-1 */
            is_boundary[buddy] = 1;
            block_rank[buddy]  = (char)(r - 1);
            block_alloc[buddy] = 0;
            fl_add(r - 1, buddy);
            /* left half continues (rank lowered) */
            is_boundary[page_idx] = 1;
            block_rank[page_idx]  = (char)(r - 1);
        }
    }

    /* mark allocated */
    block_alloc[page_idx] = 1;
    return (char *)pool_start + (unsigned long)page_idx * PAGE_SIZE;
}

int return_pages(void *p) {
    int page_idx, rank, buddy_idx, blk_sz;

    if (!p) return -EINVAL;

    /* range & alignment check */
    if ((char *)p < (char *)pool_start) return -EINVAL;
    {
        unsigned long off = (unsigned long)((char *)p - (char *)pool_start);
        if (off % PAGE_SIZE != 0) return -EINVAL;
        page_idx = (int)(off / PAGE_SIZE);
    }
    if (page_idx >= total_pages) return -EINVAL;

    /* must be the start of an allocated block */
    if (!is_boundary[page_idx] || !block_alloc[page_idx])
        return -EINVAL;

    rank     = (int)block_rank[page_idx];
    block_alloc[page_idx] = 0;          /* mark free */

    /* try to coalesce with buddy */
    while (rank < MAX_RANK) {
        blk_sz    = 1 << (rank - 1);
        buddy_idx = page_idx ^ blk_sz;

        if (buddy_idx >= total_pages) break;
        if (!is_boundary[buddy_idx]) break;
        if (block_alloc[buddy_idx]) break;
        if (block_rank[buddy_idx] != rank) break;

        /* buddy is free, same rank → merge */
        fl_remove(rank, buddy_idx);

        /* clear both old boundaries, keep the left one */
        if (buddy_idx < page_idx) {
            is_boundary[page_idx] = 0;
            block_rank[page_idx]  = 0;
            page_idx = buddy_idx;
        } else {
            is_boundary[buddy_idx] = 0;
            block_rank[buddy_idx]  = 0;
        }
        rank++;
        is_boundary[page_idx] = 1;
        block_rank[page_idx]  = (char)rank;
    }

    /* insert the (possibly merged) block */
    fl_add(rank, page_idx);
    return OK;
}

int query_ranks(void *p) {
    int page_idx;

    if (!p) return -EINVAL;

    if ((char *)p < (char *)pool_start) return -EINVAL;
    {
        unsigned long off = (unsigned long)((char *)p - (char *)pool_start);
        if (off % PAGE_SIZE != 0) return -EINVAL;
        page_idx = (int)(off / PAGE_SIZE);
    }
    if (page_idx >= total_pages) return -EINVAL;

    /* walk back to the containing block's start */
    while (page_idx >= 0 && !is_boundary[page_idx])
        page_idx--;

    if (page_idx < 0) return -EINVAL;
    return (int)block_rank[page_idx];
}

int query_page_counts(int rank) {
    if (rank < 1 || rank > MAX_RANK) return -EINVAL;
    return free_cnt[rank];
}
