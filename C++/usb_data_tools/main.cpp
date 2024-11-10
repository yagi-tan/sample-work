#include "main.h"

#include <cstdio>

#if !defined(TARGET_WASM)
int main() {
	printf("nothing to do\n");
	return 0;
}
#endif

