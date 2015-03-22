#include <unistd.h>

int main (int argc, char *argv[]) {
   setuid(0);
   return execv("/usr/lib/vdr/vdr-shutdown", argv);
}
