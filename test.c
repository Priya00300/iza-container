#include <stdio.h>
#include <unistd.h>

int main() {
    char hostname[256];
    gethostname(hostname, 256);
    printf("🎉 SUCCESS! Container is working!\n");
    printf("Hostname: %s\n", hostname);
    printf("Process ID: %d\n", getpid());
    return 0;
}
