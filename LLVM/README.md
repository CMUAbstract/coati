# Coati-pass

LLVM Pass for Coati programming model reference implementation

Build:

	$ mkdir build
	$ cd build
	$ cmake ..
	$ make

Run:

	$ clang -Xclang -load -Xclang build/src/libCoatiPass.* something.c
