Simple builder for C-like programs. 

Put the source file in your root project directory and build it:

```
cc unmaker.c -o unmaker
```

If using default settings, initialise the paths:

```
./unmaker -init
```

Then put your .c files in /src and run unmaker:

```
./unmaker
```

By default, the output binary will be in /bin. 

It will also copy any libraries from your project's libs/ folder into /bin/libs. The binary's rpath will be set accordingly.

To customise, edit the defines in the unmaker.c. For complex changes, alter the code directly.

You do not need to recompile unmaker after the first build - it will automatically recompile itself if changes are detected in unmaker.c.

Should run on Linux or most POSIX systems.
