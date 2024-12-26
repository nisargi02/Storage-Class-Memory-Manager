/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * scm.c
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include "scm.h"

/**
 * Needs:
 *   fstat()
 *   S_ISREG()
 *   open()
 *   close()
 *   sbrk()
 *   mmap()
 *   munmap()
 *   msync()
 */

/* research the above Needed API and design accordingly */

#define VIRT_ADDR 0x600000000000
#define INT_SIZE 2 * sizeof(int)
#define METADATA_SIZE 2 * sizeof(size_t)

struct scm {
    int fd; /* file descriptor */
    void *mem; /* start of the mapped region */
    void *base; /* memory after the meta data */
    size_t size; /* utilized */
    size_t length; /* fixed - file size */ /* capacity */
};

void file_size(struct scm *scm) {
    struct stat st;
    size_t page = page_size();
    fstat(scm->fd, &st);

    if(S_ISREG(st.st_mode)) {
        scm->length = st.st_size;
        scm->length = (scm->length / page) * page;
    }
    else {
        printf("Error in fstat\n");
        close(scm->fd);
        free(scm);
    }

    return;
}

struct scm *scm_open(const char *pathname, int truncate) {
    size_t curr, vm_addr;
    size_t page = page_size();
    int *size_info;

    struct scm *scm = (struct scm*) malloc(sizeof(struct scm));
    /* assign 0's to scm? */

    if(scm == NULL) {
        printf("Error with scm\n");
        close(scm->fd);
        return NULL;
    }

    scm->fd = open(pathname, O_RDWR);
    if(scm->fd == -1) {
        printf("Error with fd\n");
        return NULL;
    }

    file_size(scm);
    /*if(file_size(scm) == 0) {
        TRACE("Error");
        close(scm->fd);
        free(scm);
        return NULL;
    }*/

    curr = (size_t) sbrk(0);
    vm_addr = (VIRT_ADDR / page) * page;

    if(vm_addr < curr) {
        printf("Error with sbrk\n");
        return NULL;
    }

    scm->mem = mmap((void *) vm_addr, scm->length, 
                    PROT_READ | PROT_WRITE,
                    MAP_FIXED | MAP_SHARED, scm->fd, 0); /* what is the use of last arg? */

    if(MAP_FAILED == scm->mem) {
        printf("Error with mmap\n");
        printf("File Size: %d\n", (int)scm->length);
        FREE(scm);
        return NULL;
    }

    scm->base = (void *)((char *)scm->mem + INT_SIZE);
    size_info = (int *)scm->mem;

    if((1 != size_info[0]) || truncate) {
        size_info[0] = 1;
        size_info[1] = 0;
        scm->size = 0;
    }

    scm->size = size_info[1];
    close(scm->fd);

    return scm;
}

void scm_close(struct scm *scm) {
    if(scm == NULL) {
        return;
    }

    if(scm->mem != MAP_FAILED) {
        if(msync(scm->mem, scm->length, MS_SYNC) == -1) {
            perror("Error with msync");
        }

        if(munmap(scm->mem, scm->length) == -1) {
            perror("Error with munmap");
        }
    }

    if(scm->fd != -1) {
        close(scm->fd);
    }

    FREE(scm);
}

void *scm_malloc(struct scm *scm, size_t n) {
    size_t *local_metadata;
    int *total_metadata;
    void *ptr;

    if(scm == NULL || scm->base == MAP_FAILED || n == 0) {
        return NULL;
    }

    if(scm->size + n + METADATA_SIZE > scm->length) {
        printf("scm size: %d\n", (int)scm->size);
        printf("scm length: %d\n", (int)scm->length);
        printf("n: %d\n", (int)n);
        perror("Length exceeded\n");
        return NULL;
    }

    local_metadata = (size_t *) ((char *) scm->base + scm->size);

    local_metadata[0] = 1;
    local_metadata[1] = n;

    scm->size += n + METADATA_SIZE;

    total_metadata = (int *) scm->mem;
    total_metadata[1] = scm->size;

    ptr = (void *) ((char *) local_metadata + METADATA_SIZE);
    return ptr;
}

char *scm_strdup(struct scm *scm, const char *s) {
    char *dup_str;
    int *total_metadata;
    size_t *local_metadata = (size_t *) ((char *) scm->base + scm->size);


    if(s == NULL || scm == NULL || scm->base == MAP_FAILED) {
        return NULL;
    }

    local_metadata[0] = 1;
    local_metadata[1] = strlen(s) + 1;
    dup_str = (char *) local_metadata + METADATA_SIZE;

    strcpy(dup_str, s);
    scm->size += local_metadata[1] + METADATA_SIZE;
    total_metadata = (int *) scm->mem;
    total_metadata[1] = scm->size;

    return dup_str;
}

void scm_free(struct scm *scm, void *p) {
    char *current_ptr;
    char *end_ptr;
    size_t *local_metadata;
    size_t allocated;
    size_t block_size;
    void *data_ptr;

    if (scm == NULL || p == NULL) {
        return;
    }

    current_ptr = (char *)scm->base;
    end_ptr = (char *)scm->base + scm->size;

    while (current_ptr < end_ptr) {
        local_metadata = (size_t *)current_ptr;
        allocated = local_metadata[0];
        block_size = local_metadata[1];
        data_ptr = current_ptr + METADATA_SIZE;

        if (data_ptr == p) {
            if (allocated == 0) {
                printf("Warning: Double free detected.\n");
                return;
            }
           
            local_metadata[0] = 0;
            return;
        }

        current_ptr += METADATA_SIZE + block_size;
    }

    printf("Error: Pointer not found in SCM.\n");
}


size_t scm_utilized(const struct scm *scm) {
    if(scm == NULL) {
        return 0;
    }    
    return scm->size;
}

size_t scm_capacity(const struct scm *scm) {
    if(scm == NULL) {
        return 0;
    }
    return scm->length;
}

void *scm_mbase(struct scm *scm) {
    if(scm == NULL) {
        return NULL;
    }

    if(0 !=scm->size) {
        return (void*)((size_t *) scm->base + 2);
    }

    return scm->base;
}

