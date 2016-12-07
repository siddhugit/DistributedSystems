This is the first MP.
Distributed grep

after downloading from git, on each of the vms copy the required log file to /tmp/vm*.log where * is the vm number (1 to 7).

dgrepd.c implements server (C file).

dgrep.cpp implements client (C++ file).

run Makefile to create the executables (dgrepd and dgrep).

run ./dgrepd to instantiate server function (once on each vm 1 to 7).

run ./dgrep $PATTERN$ where $PATTERN$ is the pattern to search.
- results will be printed to terminal, along with a summary from each server vm.
- a summary will also be printed that gives the total number of lines that were matched across all running vms.

Link to demo video for MP1 : 
https://youtu.be/Jgq8hRjgzWw

