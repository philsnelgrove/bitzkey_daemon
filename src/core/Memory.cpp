// Every new and delete in the program goes to mimalloc: the brains' weight vectors are large, and the system heap
// searches its free lists for each one, under one lock, while every core makes and frees them at once (conceiving,
// births, fetching brains from the GPUs). Nothing else changes: the same bytes, only found faster.
#include <mimalloc-new-delete.h>
