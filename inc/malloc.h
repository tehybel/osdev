#ifndef JOS_INC_MALLOC_H
#define JOS_INC_MALLOC_H 1

void *malloc(size_t size);
void free(void *addr);
void *calloc(size_t n, size_t size);

#endif
