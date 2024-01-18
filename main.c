#include <dlfcn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef int lib_func(void);

int main(void) {

    void *last_module = NULL;
    void *module;

    while (true) {

        if (system("make editor")) {
            printf("fucked up compilation");
            module = last_module;
        } else {
            module = dlopen("./editor.so", RTLD_NOW);
        }

        lib_func *editor = dlsym(module, "main");

        int result = editor();

        if (!result) {
            break;
        }

        dlclose(module);
    }

    system("make clean");
    return 0;
}
