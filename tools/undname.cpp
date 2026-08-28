#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>

int main(int argc, char** argv) {
    char out[8192];
    for (int i = 1; i < argc; i++) {
        DWORD n = UnDecorateSymbolName(argv[i], out, sizeof(out), UNDNAME_COMPLETE);
        if (n) printf("%s\n", out);
        else printf("FAILED(%lu): %s\n", GetLastError(), argv[i]);
    }
    return 0;
}
