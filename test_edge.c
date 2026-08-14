#include <stdio.h>
#include <stdlib.h>
#include "buddy.h"

int main() {
    void *pool;
    int ret;

    /* Test 1: init with non-power-of-2 pgcount */
    printf("Test 1: init with pgcount=10\n");
    pool = malloc(10 * 4096);
    ret = init_page(pool, 10);
    printf("  init_page: %d\n", ret);
    printf("  query_page_counts(4)=%d (expect 1)\n", query_page_counts(4));
    printf("  query_page_counts(2)=%d (expect 1)\n", query_page_counts(2));
    printf("  query_page_counts(1)=%d (expect 0)\n", query_page_counts(1));

    /* alloc rank 2 (2 pages) */
    void *a = alloc_pages(2);
    printf("  alloc_pages(2): ptr=%p, PTR_ERR=%ld\n", a, PTR_ERR(a));
    printf("  query_page_counts(4)=%d (expect 1)\n", query_page_counts(4));
    printf("  query_page_counts(2)=%d (expect 0)\n", query_page_counts(2));

    /* alloc rank 1 */
    void *b = alloc_pages(1);
    printf("  alloc_pages(1): ptr diff from pool=%ld (expect 8*4096=%d)\n", 
           (char*)b - (char*)pool, 8*4096);

    /* query ranks */
    printf("  query_ranks(a)=%d (expect 2)\n", query_ranks(a));
    printf("  query_ranks(b)=%d (expect 1)\n", query_ranks(b));
    printf("  query_ranks(pool+6*4096)=%d (expect 4, inside free rank-4 block)\n", 
           query_ranks((char*)pool + 6*4096));

    /* return and merge */
    ret = return_pages(a);
    printf("  return_pages(a): %d\n", ret);
    printf("  query_page_counts(2)=%d (expect 1)\n", query_page_counts(2));
    ret = return_pages(b);
    printf("  return_pages(b): %d\n", ret);
    printf("  query_page_counts(4)=%d (expect 2)\n", query_page_counts(4));

    /* Test query_ranks on merged block interior */
    printf("  query_ranks(pool+2*4096)=%d (expect 4)\n", 
           query_ranks((char*)pool + 2*4096));
    printf("  query_ranks(pool+8*4096)=%d (expect 2)\n", 
           query_ranks((char*)pool + 8*4096));

    free(pool);

    /* Test 2: invalid inputs */
    printf("\nTest 2: invalid inputs\n");
    pool = malloc(4 * 4096);
    ret = init_page(pool, 4);
    printf("  init_page(4)=%d\n", ret);
    printf("  alloc_pages(0): PTR_ERR=%ld (expect -22)\n", PTR_ERR(alloc_pages(0)));
    printf("  alloc_pages(17): PTR_ERR=%ld (expect -22)\n", PTR_ERR(alloc_pages(17)));
    printf("  query_page_counts(0)=%d (expect -22)\n", query_page_counts(0));
    printf("  query_page_counts(17)=%d (expect -22)\n", query_page_counts(17));

    void *p1 = alloc_pages(1);
    printf("  alloc_pages(1) OK, ptr diff=%ld\n", (char*)p1 - (char*)pool);
    void *p2 = alloc_pages(1);
    void *p3 = alloc_pages(1);
    void *p4 = alloc_pages(1);
    printf("  alloc_pages(1) x4 done\n");
    void *p5 = alloc_pages(1);
    printf("  alloc_pages(1) 5th: PTR_ERR=%ld (expect -28)\n", PTR_ERR(p5));

    printf("  return_pages(NULL)=%d (expect -22)\n", return_pages(NULL));
    printf("  return_pages(pool+100)=%d (expect -22, unaligned)\n", return_pages((char*)pool+100));
    printf("  return_pages(pool+99999)=%d (expect -22, out of range)\n", return_pages((char*)pool+99999));
    printf("  query_ranks(NULL)=%d (expect -22)\n", query_ranks(NULL));

    ret = return_pages(p1);
    printf("  return_pages(p1)=%d\n", ret);
    printf("  return_pages(p1 again)=%d (expect -22, already free)\n", return_pages(p1));

    /* query_ranks on interior of free block */
    printf("  query_ranks after free p1 at p1+100=%d (expect -22, unaligned)\n", 
           query_ranks((char*)p1+100));

    free(pool);
    printf("\nAll edge tests done!\n");
    return 0;
}
