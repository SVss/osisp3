#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <alloca.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>

#include <unistd.h>
#include <sys/wait.h>

#define ARGS_COUNT 3
#define INODES_SIZE 255

char *script_name = NULL;
int MAX_PROC_COUNT = 0;
int status = 0;


void print_error(const char *s_name, const char *msg, const char *f_name);
void process(char *dir_name);


int main(int argc, char *argv[]) {
    script_name = basename(argv[0]);

    if (argc < ARGS_COUNT) {
        print_error(script_name, "Not enough arguments.", 0);
        return 1;
    }

    char *dir_name = realpath(argv[1], NULL);
    if (dir_name == NULL) {
        print_error(script_name, "Error opening directory", argv[1]);
        return 1;
    }

    MAX_PROC_COUNT = atoi(argv[2]);
    if (MAX_PROC_COUNT == 0) {
        print_error(script_name, "Invalid MAX_PROC_COUNT value", NULL);
        return EXIT_FAILURE;
    }

    process(dir_name);

    while (wait(&status) != -1) { }

    return EXIT_SUCCESS;
}


void print_error(const char *s_name, const char *msg, const char *f_name) {
    fprintf(stderr, "%s: %s %s\n", s_name, msg, (f_name)? f_name : "");
}


int proc_count = 0;

int count_words(char *curr_name, int *bytes_count);

void process(char *dir_name) {
    DIR *cd = opendir(dir_name);

    if (!cd) {
        print_error(script_name, strerror(errno), dir_name);
        return;
    }

    char *curr_name = alloca(strlen(dir_name) + NAME_MAX + 3);
    curr_name[0] = 0;
    strcat(curr_name, dir_name);
    strcat(curr_name, "/");
    size_t curr_name_len = strlen(curr_name);

    struct dirent *entry = alloca(sizeof(struct dirent) );
    struct stat st;

    size_t ilist_len = INODES_SIZE;
    ino_t *ilist = malloc(ilist_len * sizeof(ino_t) );
    int ilist_next = 0;

    errno = 0;

    while ( (entry = readdir(cd) ) ) {
        curr_name[curr_name_len] = 0;
        strcat(curr_name, entry->d_name);

        if (lstat(curr_name, &st) == -1) {
            print_error(script_name, strerror(errno), curr_name);

            continue;
        }

        ino_t ino = st.st_ino;

        int i = 0;
        while ( (i < ilist_next) && (ino != ilist[i]) )
            ++i;

        if (i == ilist_next) {
            if (ilist_next == ilist_len) {
                ilist_len *= 2;
                ilist = (ino_t*)realloc(ilist, ilist_len*sizeof(ino_t) );
            }

            ilist[ilist_next] = ino;
            ++ilist_next;

            if ( S_ISDIR(st.st_mode) ) {

                if ( (strcmp(entry->d_name, ".") != 0) && (strcmp(entry->d_name, "..") != 0) )  {
                    process(curr_name);
                }
            }
            else if ( S_ISREG(st.st_mode) ) {

                if (proc_count >= MAX_PROC_COUNT) {
                    int status;

                    if (wait(&status) != 0) {
                        --proc_count;
                    }
                }

                pid_t pid = fork();
                if (pid == 0) {

                    int bytes_count = 0, words_count = 0;
                    words_count = count_words(curr_name, &bytes_count);

                    if (words_count == -1) {
                        exit(EXIT_FAILURE);
                    }

                    printf("%d %s %d %d\n", getpid(), curr_name, bytes_count, words_count);
                    exit(EXIT_SUCCESS);
                }
                else if (pid < 0) {
                    print_error(script_name, "Error while forking", NULL);
                }

                proc_count++;   // parent process

            }   // S_ISREG

        }   // unique inode

    }   // directory read

    if (errno != 0) {
        print_error(script_name, strerror(errno), curr_name);
    }

    if (closedir(cd) == -1) {
        print_error(script_name, strerror(errno), dir_name);
    }

    free(ilist);
    return;
}


typedef enum state_t {
    ST_OUT = 0,
    ST_IN_WORD = 1
} state_t;


int is_separator(char c) {
    static const char SEPARATORS[] = " \n\t";
    return !(strchr(SEPARATORS, c) == NULL);
}


int count_words(char *curr_name, int *bytes_count) {
    int fd = open(curr_name,  O_RDONLY);

    if (fd == -1) {
        print_error(script_name, "Error while opening file ", curr_name);
        return -1;
    }

    int result = 0;
    *bytes_count = 0;

    state_t st = ST_OUT;

    char c = 0;
    while (read(fd, &c, sizeof(char) ) != 0) {
        if (!is_separator(c) ) {

            if (st == ST_OUT) {
                st = ST_IN_WORD;
                ++result;
            }
        }
        else {

            st = ST_OUT;
        }

        ++(*bytes_count);
    }

    if (close(fd) == -1) {
        print_error(script_name, "Error while closing file ", curr_name);
        return -1;
    }

    return result;
}
