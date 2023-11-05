# XLXEcho
This software interlinks (as a peer) with XLX reflector and provides echo/parrot functionality.

# Build
Program is a single C file, no makefile is required. To build, simply run gcc:
```
gcc -o xlxecho xlxecho.c
```
or for old gcc:
```
gcc -std=gnu99 -lrt -o xlxecho xlxecho.c
```

# Usage
Run xlxecho:
```
./xlxecho ECHO 127.0.0.1 &
```
Add this line on xlxd.interlink:
```
ECHO 127.0.0.1 Z
```
