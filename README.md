Simple builder for C-like programs. 

## Quickstart

Put the source file in your root project directory and build it:

```
cc unmaker.c -o unmaker
```

If using default settings, initialise the paths and git:

```
./unmaker -init
```

Then put your .c files in /src and run unmaker:

```
./unmaker
```

The output binary will be in /bin. 

To customise, edit the defines in the unmaker.c. For complex changes, alter the code directly.

## Features

- Recompiles itself if changes are made
- Generates a compile_commands.json file for clangd and other tools
- Copies libraries into bin/libs and sets the rpath
- Runs on Linux and probably other POSIX systems
