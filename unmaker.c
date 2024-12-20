#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Build name
#define TARGET "build"

// Compiler and linker commands for target
#define COMPILER "cc"
#define LINKER "cc"

// Source folder and extension
#define SRC_DIR "src"
#define SRC_EXT ".c"

// Include, object and bin folders.
// Object and bin folders are flushed on clean.
#define INC_DIR "include"
#define OBJ_DIR "obj"
#define BIN_DIR "bin"

// Compiler commands
#define CFLAGS "-Wall"
#define INCLUDE "-I"

// Linker commands
#define LIB_DIR "lib"
#define LIB_FLAGS ""
#define LD_FLAGS "-L" LIB_DIR

// rpath will set the initial search path
#define R_PATH "-Wl,-rpath='$ORIGIN/" LIB_DIR "'"

// lib copy after build
#define BIN_LIB_DIR BIN_DIR "/" LIB_DIR
#define LIB_COPY_CMD "cp -u " LIB_DIR "/* " BIN_LIB_DIR

// System command invoked on clean
#define CLEAN_CMD "rm -rf"

// Additional command invoked after -init
#define EXTRA_INIT "git init"

// Run command options.
#define RUN_CMD_PREFIX "./"
#define RUN_CMD_SUFFIX ""

// Name of this source file, used to check if rebuild is required.
#define UNMAKER_SRC "unmaker.c"
#define UNMAKER_CC "cc"

// Set to 1 to create a compile_commands.json
#define EXPORT_COMPILE_COMMANDS 1

#if EXPORT_COMPILE_COMMANDS
char **compile_commands = NULL;
int compile_commands_count = 0;
int compile_commands_capacity = 16;
#endif

const char *options[][2] = {{"", "Build default settings."},
                            {"-clean", "Clean build directories."},
                            {"-full", "Clean, build and run."},
                            {"-init", "Initialize the project directory."},
                            {"-run", "Build default settings and run."},
                            {"-usage", "Display this usage message."}};

void print_usage(char *exec_name);
int file_newer(char *a_file, char *b_file);
int try_rebuild_self(char *argv[]);
int try_copy_all_library_files();
void write_compile_commands();
int clean_exit(int code);

// Stringbuilder functions
struct Sb {
  char *str;
  size_t length;
  size_t capacity;
};
struct Sb
    *string_builders[10]; // store the pointers to the sbs for freeing on exit
int sb_count = 0;
char *string_sb_copy(const struct Sb *sb);
struct Sb *string_sb_create(size_t capacity);
int string_sb_append_f(struct Sb *sb, const char *fmt, ...);
int string_sb_append(struct Sb *sb, const char *str);
const char *string_sb_get(const struct Sb *sb);
void string_sb_clear(struct Sb *sb);
void string_sb_free(struct Sb **sb);

int main(int argc, char *argv[]) {
  if (try_rebuild_self(argv) == EXIT_FAILURE) {
    return clean_exit(EXIT_FAILURE);
  }

  if (argc > 2) {
    print_usage(argv[0]);
    return clean_exit(EXIT_FAILURE);
  }

  int build = argc == 1 ? 1 : 0; // assume args do not build
  int clean = 0;
  int init = 0;
  int run = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-clean") == 0) {
      clean = 1;
    } else if (strcmp(argv[i], "-run") == 0) {
      run = 1;
      build = 1;
    } else if (strcmp(argv[i], "-init") == 0) {
      init = 1;
    } else if (strcmp(argv[i], "-full") == 0) {
      clean = 1;
      build = 1;
      run = 1;
    } else if (strcmp(argv[i], "-usage") == 0) {
      print_usage(argv[0]);
      return clean_exit(EXIT_SUCCESS);
    } else {
      fprintf(stderr, "Unknown flag: %s\n", argv[i]);
      print_usage(argv[0]);
      return clean_exit(EXIT_FAILURE);
    }
  }

  // Directories to create
  const char *dirs_to_make[] = {SRC_DIR, OBJ_DIR,     BIN_DIR,
                                INC_DIR, BIN_LIB_DIR, LIB_DIR};
  const size_t num_dirs = sizeof(dirs_to_make) / sizeof(dirs_to_make[0]);

  // Clean directories if requested
  if (clean) {
    struct Sb *clean_sb = string_sb_create(256);
    string_sb_append(clean_sb, CLEAN_CMD);
    const char *dirs_to_clean[] = {OBJ_DIR, BIN_DIR};
    const size_t num_dirs_to_clean =
        sizeof(dirs_to_clean) / sizeof(dirs_to_clean[0]);

    if (num_dirs_to_clean > 0) {
      for (size_t i = 0; i < num_dirs_to_clean; i++) {
        string_sb_append_f(clean_sb, " %s", dirs_to_clean[i]);
      }
      printf("Cleaning: %s\n", string_sb_get(clean_sb));
      system(string_sb_get(clean_sb));
    }
  }

  // Create necessary directories
  for (size_t i = 0; i < num_dirs; ++i) {
    const char *dir = dirs_to_make[i];
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
      perror("mkdir failed");
      return clean_exit(EXIT_FAILURE);
    }
  }

  if (init) {
    struct Sb *init_sb = string_sb_create(256);
    string_sb_append(init_sb, EXTRA_INIT);
    const char *init_cmd = string_sb_get(init_sb);
    if (init_cmd == NULL || strlen(init_cmd) == 0) {
      fprintf(stdout, "No additional init specified\n");
    } else {
      fprintf(stdout, "Additional init: %s\n", init_cmd);
      if (system(init_cmd) != 0) {
        perror("init failed");
        return clean_exit(EXIT_FAILURE);
      }
    }
  }

  // If initializing or cleaning, exit
  if (build == 0) {
    return clean_exit(EXIT_SUCCESS);
  }

  // Determine target binary
  char *target_binary_input = TARGET;
  if (target_binary_input == NULL) {
    fprintf(stderr, "No target binary specified\n");
    return clean_exit(EXIT_FAILURE);
  }

  printf("Target binary: %s\n", target_binary_input);
  struct Sb *target_sb = string_sb_create(256);
  string_sb_append_f(target_sb, "%s/%s", BIN_DIR, target_binary_input);
  const char *target_binary = string_sb_copy(target_sb);

  DIR *dir;
  struct dirent *entry;
  dir = opendir(SRC_DIR);
  if (dir == NULL) {
    perror("Failed to open source directory");
    return clean_exit(EXIT_FAILURE);
  }

#if EXPORT_COMPILE_COMMANDS
  compile_commands = malloc(sizeof(char *) * compile_commands_capacity);
  if (!compile_commands) {
    perror("Couldn't allocate memory for compile commands");
    return clean_exit(EXIT_FAILURE);
  }
#endif

  // Compiler string builder
  struct Sb *compile_sb = string_sb_create(256);

  // String builder to collect all object file paths for linking
  struct Sb *object_files_sb = string_sb_create(8192);

  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_type == DT_REG) {
      const char *dot = strrchr(entry->d_name, '.');
      if (dot && strcmp(dot, SRC_EXT) == 0) {
        // Construct full source file
        string_sb_clear(compile_sb);
        string_sb_append_f(compile_sb, "%s/%s", SRC_DIR, entry->d_name);
        char *source_path = string_sb_copy(compile_sb);

        // Extract base name without extension
        string_sb_clear(compile_sb);
        size_t base_len = dot - entry->d_name;
        string_sb_append_f(compile_sb, "%.*s", (int)base_len, entry->d_name);
        char *base_name = string_sb_copy(compile_sb);

        // Construct corresponding object file path
        string_sb_clear(compile_sb);
        string_sb_append_f(compile_sb, "%s/%s.o", OBJ_DIR, base_name);
        char *object_path = string_sb_copy(compile_sb);

        // Compile the source file into object file
        string_sb_clear(compile_sb);
        string_sb_append_f(compile_sb, "%s %s %s -c %s -o %s", COMPILER, CFLAGS,
                           INCLUDE, source_path, object_path);
        char *compile_cmd = string_sb_copy(compile_sb);

        // Check if recompilation is needed
        if (file_newer(source_path, object_path)) {

          printf("Compiling: %s\n", compile_cmd);
          if (system(compile_cmd) != 0) {
            fprintf(stderr, "Compilation failed for %s\n", source_path);
            closedir(dir);
            return clean_exit(EXIT_FAILURE);
          }

        } else {
          printf("Skipping (up-to-date): %s\n", source_path);
        }
#if EXPORT_COMPILE_COMMANDS
        if (compile_commands_count >= compile_commands_capacity) {
          compile_commands_capacity *= 2;
          char **temp = realloc(compile_commands,
                                sizeof(char *) * compile_commands_capacity);
          if (!temp) {
            perror("Couldn't grow compile commands array");
            return clean_exit(EXIT_FAILURE);
          }
          compile_commands = temp;
        }

        compile_commands[compile_commands_count++] = compile_cmd;

#endif
        // Append the object file path to the object_files string
        string_sb_append_f(object_files_sb, "%s ", object_path);
      }
    }
  }
  closedir(dir);

#if EXPORT_COMPILE_COMMANDS
  // Write compile_commands.json
  write_compile_commands();
#endif

  // Construct the linking command.
  // Use the compiler cmd builder
  string_sb_clear(compile_sb);
  string_sb_append_f(compile_sb, "%s %s -o %s %s %s %s", LINKER,
                     string_sb_get(object_files_sb), target_binary, LIB_FLAGS,
                     LD_FLAGS, R_PATH);
  char *link_cmd = string_sb_copy(compile_sb);

  printf("Linking: %s\n", link_cmd);
  if (system(link_cmd) != 0) {
    fprintf(stderr, "Linking failed\n");
    return clean_exit(EXIT_FAILURE);
  }

  printf("Copying libraries: %s\n", LIB_COPY_CMD);
  if (try_copy_all_library_files() == EXIT_FAILURE) {
    fprintf(stderr, "Some or all library files could not be copied)");
  }

  printf("Success: Executable created at %s\n", target_binary);

  // Clean up our allocations
  clean_exit(0);

  // If we're not running, then just clean up and return
  if (!run) {
    return EXIT_SUCCESS;
  }

  // This will run the binary. Clean up the sb manually.
  struct Sb *sb_run = string_sb_create(16);
  string_sb_append_f(sb_run, "%s%s%s", RUN_CMD_PREFIX, target_binary,
                     RUN_CMD_SUFFIX);
  char *run_cmd = string_sb_copy(sb_run);
  string_sb_free(&sb_run);
  printf("Executing: %s\n--- RUN OUTPUT ---\n", run_cmd);
  if (system(run_cmd) != 0) {
    fprintf(stderr, "Execution failed for %s\n", target_binary);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

int file_newer(char *a_file, char *b_file) {
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
  char *unmaker_src = UNMAKER_SRC;
  if (strlen(unmaker_src) == 0) {
    return EXIT_SUCCESS;
  }

  if (file_newer(unmaker_src, argv[0])) {
    char rebuild_cmd[256];
    snprintf(rebuild_cmd, sizeof(rebuild_cmd), "%s %s -o %s", UNMAKER_CC,
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
  } else {
    perror("LIB_DIR not found");
    return EXIT_FAILURE;
  }

  if (files_exist) {
    fprintf(stdout, "Copying files from %s to %s\n", LIB_DIR, BIN_LIB_DIR);
    if (system(LIB_COPY_CMD) != 0) {
      fprintf(stderr, "Copying failed\n");
      return EXIT_FAILURE;
    }
  } else {
    fprintf(stdout, "Nothing to copy\n");
  }

  return EXIT_SUCCESS;
}

void print_usage(char *exec_name) {
  int num_options = sizeof(options) / sizeof(options[0]);

  size_t max_command_length = strlen(exec_name);
  for (int i = 0; i < num_options; i++) {
    size_t current = strlen(exec_name);
    if (strlen(options[i][0]) > 0) {
      current += 1 + strlen(options[i][0]);
    }

    if (current > max_command_length)
      max_command_length = current;
  }

  int padding = max_command_length + 4;

  fprintf(stdout, "Usage:\n");
  for (int i = 0; i < num_options; i++) {
    char command[256];
    if (strlen(options[i][0]) == 0) {
      snprintf(command, sizeof(command), "%s", exec_name);
    } else {
      snprintf(command, sizeof(command), "%s %s", exec_name, options[i][0]);
    }

    fprintf(stdout, "  %-*s %s\n", (int)padding, command, options[i][1]);
  }
}

#if EXPORT_COMPILE_COMMANDS
void write_compile_commands() {
  // Buffer to store the current working directory
  char cwd[PATH_MAX];

  // Retrieve the current working directory
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    perror("getcwd() error");
    return;
  }

  FILE *f = fopen("compile_commands.json", "w");
  if (!f) {
    perror("Failed to create compile_commands.json");
    return;
  }
  fprintf(f, "[\n");
  for (int i = 0; i < compile_commands_count; i++) {
    fprintf(f, "  {\n");
    fprintf(f, "    \"directory\": \"%s\",\n", cwd);
    fprintf(f, "    \"command\": \"%s\",\n", compile_commands[i]);
    // Extract source file from command
    char *file = strstr(compile_commands[i], SRC_DIR);
    char *end = strchr(file, ' ');
    if (end)
      *end = '\0';
    fprintf(f, "    \"file\": \"%s\"\n", file);
    if (i < compile_commands_count - 1)
      fprintf(f, "  },\n");
    else
      fprintf(f, "  }\n");
  }
  fprintf(f, "]\n");
  fclose(f);
}
#endif

int clean_exit(int code) {
  for (int i = 0; i < sb_count; i++) {
    string_sb_free(&string_builders[i]);
  }

#if EXPORT_COMPILE_COMMANDS
  for (int i = 0; i < compile_commands_count; i++) {
    free(compile_commands[i]);
  }
  free(compile_commands);
#endif

  return code;
}

struct Sb *string_sb_create(size_t capacity) {
  struct Sb *sb = (struct Sb *)malloc(sizeof(struct Sb));
  if (sb == NULL) {
    perror("Couldn't create stringbuilder");
    return NULL;
  }
  char *tmp = (char *)malloc(capacity * sizeof(char));
  if (tmp == NULL) {
    perror("Couldn't create stringbuilder");
    free(sb);
    return NULL;
  }
  sb->str = tmp;
  sb->length = 0;
  sb->capacity = capacity;
  sb->str[0] = '\0';
  string_builders[sb_count++] = sb;
  return sb;
}

int string_sb_append_f(struct Sb *sb, const char *fmt, ...) {
  if (!sb || !fmt) {
    fprintf(stderr, "Invalid sb or fmt string");
    return EXIT_FAILURE;
  }

  va_list args;
  va_start(args, fmt);

  va_list args_copy;
  va_copy(args_copy, args);

  size_t required_length = vsnprintf(NULL, 0, fmt, args_copy);

  va_end(args_copy);

  if (required_length < 0) {
    perror("vsnprintf failed to count fmt string");
    va_end(args);
    return EXIT_FAILURE;
  }

  size_t size_requested = required_length + sb->length + 1;
  if (size_requested > sb->capacity) {
    size_t new_capacity = size_requested;
    if (new_capacity < sb->capacity * 2) {
      new_capacity = sb->capacity * 2;
    }
    char *temp = (char *)realloc(sb->str, new_capacity * sizeof(char));
    if (temp == NULL) {
      perror("Couldn't allocate memory for string append");
      return EXIT_FAILURE;
    }
    sb->str = temp;
    sb->capacity = new_capacity;
  }

  size_t append_length =
      vsnprintf(sb->str + sb->length, sb->capacity - sb->length, fmt, args);
  va_end(args);

  if (append_length < 0) {
    perror("Couldn't append formatted string");
    return EXIT_FAILURE;
  }

  sb->length += append_length;
  return EXIT_SUCCESS;
}

int string_sb_append(struct Sb *sb, const char *str) {
  size_t new_str_len = strlen(str);
  size_t size_requested =
      new_str_len + sb->length + 1; // +1 for null terminator
  if (size_requested > sb->capacity) {
    size_t new_capacity = size_requested;
    if (new_capacity < sb->capacity * 2)
      new_capacity = sb->capacity * 2;
    char *temp = (char *)realloc(sb->str, new_capacity * sizeof(char));
    if (temp == NULL) {
      perror("Couldn't append string");
      return EXIT_FAILURE;
    }
    sb->str = temp;
    sb->capacity = new_capacity;
  }

  strcpy(sb->str + sb->length, str);
  sb->length += new_str_len;
  sb->str[sb->length] = '\0';

  return EXIT_SUCCESS;
}

const char *string_sb_get(const struct Sb *sb) {
  if (sb == NULL || sb->str == NULL) {
    return NULL;
  }
  return sb->str;
}

void string_sb_clear(struct Sb *sb) {
  if (sb == NULL || sb->str == NULL) {
    fprintf(stderr, "Couldn't clear string builder\n");
    return;
  }
  sb->length = 0;
  sb->str[0] = '\0';
}

char *string_sb_copy(const struct Sb *sb) {
  if (sb == NULL || sb->str == NULL) {
    fprintf(stderr, "Can't copy sb string: invalid sb or sb string\n");
    return NULL;
  }

  // Allocate memory: length + 1 for null terminator
  char *temp = malloc(sb->length + 1);
  if (temp == NULL) {
    perror("Couldn't allocate memory for string copy\n");
    return NULL;
  }

  strcpy(temp, sb->str);
  temp[sb->length] = '\0'; // Ensure null termination
  return temp;
}

void string_sb_free(struct Sb **sb) {
  if (sb == NULL || (*sb) == NULL || (*sb)->str == NULL) {
    fprintf(stderr, "Couldn't free string builder\n");
    return;
  }
  free((*sb)->str);
  free(*sb);
  *sb = NULL;
}
