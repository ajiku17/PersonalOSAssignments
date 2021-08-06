#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <unistd.h>

/* Function pointers to hw3 functions */
void* (*mm_malloc)(size_t);
void* (*mm_realloc)(void*, size_t);
void (*mm_free)(void*);

void test_wrap(void(*test_fn)(void));

struct rlimit limits;

void load_alloc_functions() {
    void *handle = dlopen("hw3lib.so", RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "%s\n", dlerror());
        exit(1);
    }

    char* error;
    mm_malloc = dlsym(handle, "mm_malloc");
    if ((error = dlerror()) != NULL)  {
        fprintf(stderr, "%s\n", dlerror());
        exit(1);
    }

    mm_realloc = dlsym(handle, "mm_realloc");
    if ((error = dlerror()) != NULL)  {
        fprintf(stderr, "%s\n", dlerror());
        exit(1);
    }

    mm_free = dlsym(handle, "mm_free");
    if ((error = dlerror()) != NULL)  {
        fprintf(stderr, "%s\n", dlerror());
        exit(1);
    }
}

void mm_malloc_big_simple(){
    size_t size = 1000;
    int* arrays[size];
    for(int i = 0; i < size; i++){
        arrays[i] = mm_malloc(sizeof(int) * 400);
        assert(arrays[i] != NULL);
        arrays[i][0] = 0x41;
        arrays[i][199] = 0x42;
        arrays[i][399] = 0x43;
    }

    for(int i = 0; i < size; i++){
        assert(arrays[i][0] == 0x41);
        assert(arrays[i][199] == 0x42);
        assert(arrays[i][399] == 0x43);
        mm_free(arrays[i]);
    }

    printf("malloc-big-simple test successful!\n");
}

void mm_realloc_small_simple(){
    size_t size = 10000;
    int* arrays[size];
    for(int i = 0; i < size; i++){
        arrays[i] = mm_malloc(sizeof(int));
        assert(arrays[i] != NULL);
        arrays[i][0] = i;
    }

    for(int i = 0; i < size; i += 2){
        arrays[i] = mm_realloc(arrays[i], 2 * sizeof(int));
        assert(arrays[i] != NULL);
    }

    for(int i = 0; i < size; i++){
        assert(arrays[i][0] == i);
        mm_free(arrays[i]);
    }

    printf("realloc-small-simple test successful!\n");
}


void mm_malloc_small_simple(){
    size_t size = 10000;
    int* arrays[size];
    for(int i = 0; i < size; i++){
        arrays[i] = mm_malloc(sizeof(int));
        assert(arrays[i] != NULL);
        arrays[i][0] = i;
    }

    for(int i = 0; i < size; i++){
        assert(arrays[i][0] == i);
        mm_free(arrays[i]);
    }

    printf("malloc-small-simple test successful!\n");
}

/* Implementation specific tests
*/
void mm_realloc_small_reuse(){
    size_t size = 10000;
    int* arrays[size];
    for(int i = 0; i < size; i++){
        arrays[i] = mm_malloc(sizeof(int));
        assert(arrays[i] != NULL);
        arrays[i][0] = i;
    }

    void* break_limit = sbrk(0); // break limit
    
    for(int i = 1; i < size; i += 2){
        mm_free(arrays[i]); // free even indexed arrays
        
        assert(arrays[i] != NULL);
    }

    for(int i = 0; i < size; i += 2){
        // reallocate odd indexed arrays
        arrays[i] = mm_realloc(arrays[i], 2 * sizeof(int)); 
    }

    assert(break_limit == sbrk(0)); // we should not have exceeded break limit

    for(int i = 0; i < size; i += 2){
        assert(arrays[i][0] == i);
        mm_free(arrays[i]);
    }

    printf("realloc-small-reuse test successful!\n");
}

void mm_malloc_small_reuse(){
    size_t size = 10000;
    // hog some space
    int* big_array = mm_malloc(sizeof(int) * size);
    mm_free(big_array); // leave a big free block

    int* arrays[size];
    for(int i = 0; i < size; i++){
        // this should reuse that block, plus metadata for each small block
        arrays[i] = mm_malloc(sizeof(int));
        assert(arrays[i] != NULL);
        arrays[i][0] = i;
    }

    for(int i = 0; i < size; i++){
        assert(arrays[i][0] == i);
        mm_free(arrays[i]);
    }

    printf("malloc-small-reuse test successful!\n");
}

int main(){
    load_alloc_functions();
    int status = getrlimit(RLIMIT_DATA, &limits);
    assert(status == 0);
    printf("%p\n", limits.rlim_max);
    printf("%p\n", limits.rlim_cur);

    int *data = (int*) mm_malloc(sizeof(int));
    assert(data != NULL);
    data[0] = 0x162;
    mm_free(data);
    printf("malloc test successful!\n");

    mm_malloc_small_simple();
    mm_malloc_big_simple();
    mm_malloc_small_reuse();
    mm_realloc_small_simple();
    mm_realloc_small_reuse();
    

    return 0;
}
