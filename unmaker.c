#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

// Default build name
#define TARGET "build"

// Default compiler and linker commands for target
#define COMPILER "cc"
#define LINKER "cc"

// Default source folder and extension 
#define SRC_DIR "src"
#define SRC_EXT ".c"

// Default include folder
#define INC_DIR "include"

// Object and bin folders, flushed on clean
#define OBJ_DIR "obj"
#define BIN_DIR "bin"

// Compiler commands
#define CFLAGS "-Wall"
#define INCLUDE "-I" INC_DIR

// Linker commands
// Default project lib folder.
// Also used for setting rpath
#define LIB_DIR "libs"
#define LIB_FLAGS ""
#define LD_FLAGS "-L" LIB_DIR

// rpath will set the inital search path
// for libraries, relative to the executable
#define R_PATH "-Wl,-rpath='$ORIGIN/" LIB_DIR "'"

// lib copy after build
#define BIN_LIB_DIR BIN_DIR "/" LIB_DIR
#define LIB_COPY_CMD "cp -u " LIB_DIR "/* " BIN_LIB_DIR

// System command invoked on clean
#define CLEAN_CMD "rm -rf"

// Run command options.
// Executes this format: RUN_CMD_PREFIX+BUILD_NAME+RUN_CMD_SUFFIX
// Remember to include space if required.
// Also, remember that the CWD is where unmaker is executed from.
#define RUN_CMD_PREFIX "./"
#define RUN_CMD_SUFFIX ""

// Name of this source file, used to check if rebuild is required.
// Leave blank to turn off self-rebuilding.
#define UNMAKER_SRC "unmaker.c"
#define UNMAKER_CC "cc"

void print_usage(char *exec_name){
  char * usage_string = "Usage:"\
    "\t%s                    Build default settings.\n"           \
    "\t%s -init              Initialize the project directory.\n" \
    "\t%s -run               Build default settings and run.\n"   \
    "\t%s -clean             Clean build directories.\n"          \
    "\t%s -usage             This message.\n";
  printf(usage_string, exec_name, exec_name, exec_name, exec_name, exec_name, exec_name);
}

int file_newer(char* a_file, char* b_file);

int try_rebuild_self(char *argv[]);

int try_copy_all_library_files();

int main(int argc, char *argv[]) {
    if (try_rebuild_self(argv) == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    if (argc > 2) {
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }


    int clean = 0;
    int init = 0;
    int run = 0;

    for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "-clean") == 0) {
        clean = 1;
      } else if (strcmp(argv[i], "-run") == 0) {
        run = 1;
      } else if (strcmp(argv[i], "-init") == 0) {
        init = 1;
      } else if (strcmp(argv[i], "-usage") == 0) {
        print_usage(argv[0]);
        return EXIT_SUCCESS;
      } else {
        fprintf(stderr, "Unknown flag: %s\n", argv[i]);
        print_usage(argv[0]);
        return EXIT_FAILURE;
      }
    }

    // Directories to create
    const char *dirs_to_make[] = {SRC_DIR, OBJ_DIR, BIN_DIR, INC_DIR, BIN_LIB_DIR, LIB_DIR};
    const size_t num_dirs = sizeof(dirs_to_make) / sizeof(dirs_to_make[0]);

    // Clean directories if requested
    if (clean) {
        char rmdir_cmd[256] = CLEAN_CMD;
        const char *dirs_to_clean[] = {OBJ_DIR, BIN_DIR};
        const size_t num_dirs_to_clean = sizeof(dirs_to_clean) / sizeof(dirs_to_clean[0]);

        if (num_dirs_to_clean > 0) {
            for (size_t i = 0; i < num_dirs_to_clean; i++) {
                strcat(rmdir_cmd, " ");
                strcat(rmdir_cmd, dirs_to_clean[i]);
            }
            printf("Cleaning: %s\n", rmdir_cmd);
            system(rmdir_cmd);
        }
    }

    // Create necessary directories
    for (size_t i = 0; i < num_dirs; ++i) {
        const char *dir = dirs_to_make[i];
        if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
            perror("mkdir failed");
            return EXIT_FAILURE;
        }
    }

    // If initializing or cleaning, exit
    if (init || clean) {
        return EXIT_SUCCESS;
    }

    // Determine target binary
    char *target_binary_input = TARGET;
    if (target_binary_input == NULL) {
	fprintf(stderr, "No target binary specified");
	return EXIT_FAILURE;
    }
    
    printf("Target binary: %s\n", target_binary_input);
    char target_binary[256];
    snprintf(target_binary, sizeof(target_binary), "%s/%s", BIN_DIR, target_binary_input);

    DIR *dir;
    struct dirent *entry;
    dir = opendir(SRC_DIR);
    if (dir == NULL) {
        perror("Failed to open source directory");
        return EXIT_FAILURE;
    }

    // Collect all object file paths
    char object_files[8192] = "";

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            const char *dot = strrchr(entry->d_name, '.');
            if (dot && strcmp(dot, SRC_EXT) == 0) {
                // Construct full source file path
                char source_path[512];
                snprintf(source_path, sizeof(source_path), "%s/%s", SRC_DIR, entry->d_name);

                // Extract base name without extension
                size_t base_len = dot - entry->d_name;
                char base_name[256];
                strncpy(base_name, entry->d_name, base_len);
                base_name[base_len] = '\0';

                // Construct corresponding object file path
                char object_path[512];
                snprintf(object_path, sizeof(object_path), "%s/%s.o", OBJ_DIR, base_name);

                // Check if recompilation is needed
                if (file_newer(source_path, object_path)) {
                    // Compile the source file into object file
                    char compile_cmd[2048];
                    snprintf(compile_cmd, sizeof(compile_cmd), "%s %s %s -c %s -o %s",
                             COMPILER, CFLAGS, INCLUDE, source_path, object_path);
                    printf("Compiling: %s\n", compile_cmd);
                    if (system(compile_cmd) != 0) {
                        fprintf(stderr, "Compilation failed for %s\n", source_path);
                        closedir(dir);
                        return EXIT_FAILURE;
                    }
                } else {
                    printf("Skipping (up-to-date): %s\n", source_path);
                }

                // Append the object file path to the object_files string
                strcat(object_files, object_path);
                strcat(object_files, " ");
            }
        }
    }
    closedir(dir);

    // Construct the linking command
    char link_cmd[8192];
    int link_cmd_status = snprintf(link_cmd, sizeof(link_cmd),
                                   "%s %s -o %s %s %s %s",
                                   LINKER,
                                   object_files,
                                   target_binary,
                                   LIB_FLAGS,
                                   LD_FLAGS,
				   R_PATH);

    if (link_cmd_status < 0 || link_cmd_status >= (int)sizeof(link_cmd)) {
        perror("Linking command construction failed");
        return EXIT_FAILURE;
    }

    printf("Linking: %s\n", link_cmd);
    if (system(link_cmd) != 0) {
        fprintf(stderr, "Linking failed\n");
        return EXIT_FAILURE;
    }

    printf("Copying libraries: %s\n", LIB_COPY_CMD);
    if (try_copy_all_library_files() == EXIT_FAILURE) {
        fprintf(stderr, "Some or all library files could not be copied)");
    }

    printf("Success: Executable created at %s\n", target_binary);

    if (run) {
        char run_cmd[512] = RUN_CMD_PREFIX; 
        strcat(run_cmd, target_binary);
	strcat(run_cmd, RUN_CMD_SUFFIX);
        printf("Executing: %s\n--- RUN OUTPUT ---\n", run_cmd);
        if (system(run_cmd) != 0) {
            fprintf(stderr, "Execution failed for %s\n", target_binary);
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

int file_newer(char* a_file, char* b_file) {
    struct stat a_file_buffer;
    if (stat(a_file, &a_file_buffer) == -1) {
        perror("File change test failed");
        return EXIT_FAILURE;
    }

    struct stat b_file_buffer;
    if (stat(b_file, &b_file_buffer) != 0) {
        return EXIT_FAILURE; // b_file doesn't exist, so a must be newer
    }

    // Both files exist, check modification time
    if (a_file_buffer.st_mtime > b_file_buffer.st_mtime) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int try_rebuild_self(char *argv[]) {
    char* unmaker_src = UNMAKER_SRC;
    if (strlen(unmaker_src) == 0){
      return EXIT_SUCCESS;

    }
    
    if (file_newer(unmaker_src, argv[0])) {
      char rebuild_cmd[256] = UNMAKER_CC;
        snprintf(rebuild_cmd, sizeof(rebuild_cmd),
                 "%s %s -o %s",
		 UNMAKER_CC,
                 unmaker_src, argv[0]);

        printf("Rebuilding: %s\n", rebuild_cmd);
        if (system(rebuild_cmd) != 0) {
            fprintf(stderr, "Rebuild failed\n");
            return EXIT_FAILURE;
        }

        printf("Relaunching: ");
        for (int i = 0; argv[i] != NULL; i++) {
            printf("%s ", argv[i]);
        }
        printf("\n");

        execv(argv[0], argv);
        // If execv returns, it must have failed
        perror("Relaunch failed");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
} 

int try_copy_all_library_files() {
    // First check the directory exists
    DIR *d;
    struct dirent *dir;
    int files_exist = 0;

    d = opendir(LIB_DIR);
    if (d) {
	// Roll through the files until we find
	// a regular file.
	while ((dir = readdir(d)) != NULL) {
	    if (dir->d_type == DT_REG) {
		files_exist = 1;
		break;
	    }
	}
	closedir(d);
    }
    else {
	perror("LIB_DIR not found");
	return EXIT_FAILURE;
    }
	
    if (files_exist) {
	fprintf(stdout, "Copying files from %s to %s\n", LIB_DIR, BIN_LIB_DIR);
	if (system(LIB_COPY_CMD) != 0) {
	    fprintf(stderr, "Copying failed\n");
	    return EXIT_FAILURE;
	}
    }
    else {
	fprintf(stdout, "Nothing to copy\n");
    }
	    
    return EXIT_SUCCESS;
}
