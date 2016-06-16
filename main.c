#include <unistd.h>

int main(int argc, char **argv) {
    int i = 0;
    for (; i < 1000; ++i) {
        sync();
    }
    return 0;
}
