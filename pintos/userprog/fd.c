#include "userprog/fd.h"
#include "threads/malloc.h"
#include "lib/debug.h"
#include "filesys/file.h"
#include <limits.h>
#include <stdbool.h>

static int fd_expaned (struct fd_table *fdt, const size_t new_size);

struct fd_table *
fd_table_init (void) {
    struct fd_table *fdt = malloc (sizeof (struct fd_table));
    if (fdt == NULL) {
        // TODO: calloc 예외처리
        ASSERT (fdt != NULL);
        return NULL;
    }
    fdt->fds = calloc (FD_ENTRY_N, sizeof (struct fd_entry *));
    if (fdt->fds == NULL) {
        // TODO: calloc 예외처리
        free(fdt);
        ASSERT (false);
        return NULL;
    }
    fdt->size = FD_ENTRY_N;
    struct fd_entry *fd_stdin = malloc(sizeof (struct fd_entry));
    struct fd_entry *fd_stdout = malloc(sizeof (struct fd_entry));
    if (fd_stdin == NULL || fd_stdout == NULL) {
        // TODO: malloc 예외처리
        free(fdt->fds);
        free(fdt);
        free(fd_stdin);
        free(fd_stdout);
        ASSERT (false);
        return NULL;
    }

    fd_stdin->type = FD_STDIN;
    fd_stdin->file = NULL;

    fd_stdout->type = FD_STDOUT;
    fd_stdout->file = NULL;

    fdt->fds[0] = fd_stdin;
    fdt->fds[1] = fd_stdout;
    return fdt;
}

/* 할당 가능한 fd를 반환합니다. */
int
fd_find_blank (struct fd_table *fdt) {

    ASSERT (fdt != NULL);

    for(size_t i = 0; i < fdt->size; i++) {
        if(fdt->fds[i] == NULL) {
            if (i > INT_MAX) {
                return -1;
            }
            return (int) i;
        }
    }
    return fd_expaned(fdt, fdt->size << 1);
}

/* 사이즈를 NEW_SIZE로 확장합니다. 만약 더 작은 사이즈로 
   expaned하려고 하면, -1를 리턴합니다. */
static int
fd_expaned (struct fd_table *fdt, const size_t new_size) {

    ASSERT (fdt != NULL);

    if(fdt->size < new_size) {
        if (fdt->size > INT_MAX) {
            return -1;
        }
        struct fd_entry **tmp = calloc (new_size, sizeof (struct fd_entry *));
        if (tmp == NULL) {
            // TODO:예외처리
            ASSERT (tmp != NULL);
            return -1;
        }
        for(size_t i = 0; i < fdt->size; i++) {
            tmp[i] = fdt->fds[i];
        }
        free(fdt->fds);
        fdt->fds = tmp;
        size_t ret = fdt->size;
        fdt->size = new_size;
        return (int) ret;
    } else {
        return -1;
    }
}

void
fd_entry_free (struct fd_table *fdt, size_t index) {
    // TODO: e 안의 요소 free해야할 것 있으면 하기
    file_close (fdt->fds[index]->file);
    free (fdt->fds[index]);
    fdt->fds[index] = NULL;
}

/* fd_table을 할당 해제합니다. 그 안의 요소들도 해제합니다. */
void
fd_table_free (struct fd_table *fdt) {
    /* TODO: fd_table 안의 멤버들도 free 해줘야함. */
    if (fdt == NULL) {
        return;
    }
    for(size_t i = 0; i < fdt->size; i++) {
        if(fdt->fds[i] != NULL) {
            fd_entry_free(fdt, i);
        }
    }
    free(fdt->fds);
    free(fdt);
}
