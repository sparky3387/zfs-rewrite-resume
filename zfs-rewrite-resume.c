#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

// --- Global State ---
char zfs_opts[512] = "";
int xdev_flag = 0;
int dry_run_flag = 0;
char *resume_file = NULL;
int process_files = 1;

// --- Directory Queue for Breadth-First Traversal ---
char **dir_queue = NULL;
size_t dir_count = 0;
size_t dir_capacity = 0;

// --- Function Prototypes ---
void usage(const char *prog_name);

void add_to_dir_queue(const char *path) {
    if (dir_count >= dir_capacity) {
        dir_capacity = (dir_capacity == 0) ? 16 : dir_capacity * 2;
        dir_queue = realloc(dir_queue, dir_capacity * sizeof(char *));
        if (!dir_queue) {
            perror("realloc for dir_queue failed");
            exit(EXIT_FAILURE);
        }
    }
    dir_queue[dir_count++] = strdup(path);
}

// This function ONLY runs the real command. All logic is now in handle_file.
void execute_rewrite(const char *path) {
    char command[PATH_MAX + 1024];
    // Note: We do not pass -x to the child command. The wrapper handles recursion
    // boundaries via stat()/dev_t checks.
    int len = snprintf(command, sizeof(command), "zfs rewrite %s -- \"%s\"", zfs_opts, path);
    if (len >= sizeof(command)) {
        fprintf(stderr, "ERROR: Path is too long to build command: %s\n", path);
        return;
    }

    int ret = system(command);
    if (ret != 0) {
        fprintf(stderr, "ERROR: Command failed for: %s (Exit code: %d)\n", path, ret);
    }
}

// All major logic is now centralized here.
void handle_file(const char *path) {
    // --- Dry Run Logic ---
    if (dry_run_flag) {
        // If verbose, print the filename to simulate the output.
        if (strstr(zfs_opts, "-v")) {
            printf("%s\n", path);
        }
        // Check if this is the resume point, which is our exit condition for a dry run.
        if (resume_file && strcmp(path, resume_file) == 0) {
            fprintf(stderr, "INFO: Dry run successful. Found resume point and will now exit.\n");
            exit(EXIT_SUCCESS);
        }
        // In a dry run, we do nothing else after potentially printing.
        return;
    }

    // --- Real Run Logic ---
    if (!process_files) {
        if (strcmp(path, resume_file) == 0) {
            fprintf(stderr, "INFO: Found resume point. Resuming processing FROM: %s\n", path);
            process_files = 1;
        }
    }
    if (process_files) {
        execute_rewrite(path);
    }
}

int handle_directory(const char *path, dev_t parent_dev) {
    DIR *dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "ERROR: Failed to opendir %s: %s\n", path, strerror(errno));
        return 1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char fullpath[PATH_MAX];
        int len = snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
        if (len >= sizeof(fullpath)) {
            fprintf(stderr, "ERROR: Path too long: %s/%s\n", path, entry->d_name);
            continue;
        }

        struct stat st;
        if (lstat(fullpath, &st) != 0) {
            fprintf(stderr, "ERROR: Failed to lstat %s: %s\n", fullpath, strerror(errno));
            continue;
        }

        if (xdev_flag && st.st_dev != parent_dev) {
            // Do not cross mount points
            continue;
        }

        if (S_ISREG(st.st_mode)) {
            handle_file(fullpath);
        } else if (S_ISDIR(st.st_mode)) {
            add_to_dir_queue(fullpath);
        }
    }
    closedir(dir);
    return 0;
}

int process_path(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        fprintf(stderr, "ERROR: Cannot stat initial path %s: %s\n", path, strerror(errno));
        return 1;
    }

    if (S_ISREG(st.st_mode)) {
        handle_file(path);
    } else if (S_ISDIR(st.st_mode)) {
        handle_directory(path, st.st_dev);
    }
    return 0;
}

void usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [OPTIONS] <file|directory...>\n\n", prog_name);
    fprintf(stderr, "A restartable, recursive wrapper for 'zfs rewrite'.\n");
    fprintf(stderr, "Mimics the traversal order of standard ZFS recursion.\n\n");
    fprintf(stderr, "ZFS Rewrite Options:\n");
    fprintf(stderr, "  -l <length>    Rewrite at most this number of bytes.\n");
    fprintf(stderr, "  -o <offset>    Start at this offset in bytes.\n");
    fprintf(stderr, "  -v             Verbose. Print names of successfully rewritten files.\n");
    fprintf(stderr, "  -x             Don't cross file system mount points when recursing.\n\n");
    fprintf(stderr, "Wrapper Options:\n");
    fprintf(stderr, "  -c <file>      Full path to the file to resume processing FROM. The script\n");
    fprintf(stderr, "                 will skip all files in the traversal order until it finds\n");
    fprintf(stderr, "                 this one, then continue normally.\n");
    fprintf(stderr, "  -n             Dry run. Traverses files, printing names if -v is on, and\n");
    fprintf(stderr, "                 exits successfully once the -c file is found.\n");
    fprintf(stderr, "  -h             Display this help message and exit.\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
    int opt;
    char temp_opts[64];

    // Removed 'P' from getopt string
    while ((opt = getopt(argc, argv, "l:o:vxc:nh")) != -1) {
        switch (opt) {
            case 'l': snprintf(temp_opts, sizeof(temp_opts), "-l %s ", optarg); strcat(zfs_opts, temp_opts); break;
            case 'o': snprintf(temp_opts, sizeof(temp_opts), "-o %s ", optarg); strcat(zfs_opts, temp_opts); break;
            case 'v': strcat(zfs_opts, "-v "); break;
            case 'x': xdev_flag = 1; break; // Handle recursion limits in wrapper, don't pass to file command
            case 'c': resume_file = optarg; break;
            case 'n': dry_run_flag = 1; break;
            case 'h': usage(argv[0]); break;
            default: usage(argv[0]); break;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "ERROR: Missing file or directory target(s).\n\n");
        usage(argv[0]);
    }

    if (resume_file) {
        if (!dry_run_flag) { // Only show resume message for real runs
            fprintf(stderr, "INFO: Resume mode enabled. Will skip files until %s is found.\n", resume_file);
        }
    }
    if (dry_run_flag) {
        fprintf(stderr, "INFO: Dry run mode is active. Simulating traversal...\n");
    }

    // In a real run, set the initial state to "skipping" if a resume file is provided.
    if (resume_file && !dry_run_flag) {
        process_files = 0;
    }

    for (int i = optind; i < argc; i++) {
        process_path(argv[i]);
    }

    size_t current_dir_idx = 0;
    while(current_dir_idx < dir_count) {
        char* dir_to_process = dir_queue[current_dir_idx];
        struct stat st;
        if (lstat(dir_to_process, &st) != 0) {
            fprintf(stderr, "ERROR: Cannot stat queued dir %s: %s\n", dir_to_process, strerror(errno));
        } else {
            handle_directory(dir_to_process, st.st_dev);
        }
        free(dir_to_process);
        current_dir_idx++;
    }
    free(dir_queue);

    if (dry_run_flag && resume_file) {
        // If we finished a dry run with a -c file and never exited, it means the file was not found.
        fprintf(stderr, "WARNING: Dry run finished but resume file '%s' was not found.\n", resume_file);
        return EXIT_FAILURE;
    }

    if (!process_files) {
        fprintf(stderr, "WARNING: Real run finished but resume file '%s' was not found. No files were processed.\n", resume_file);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "INFO: All processing complete.\n");
    return EXIT_SUCCESS;
}
