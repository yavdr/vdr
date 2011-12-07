//
// Small wrapper arround some administrative functions which have to be
// executed set-root-uid
//

#include <unistd.h>
#include <string.h>
#include <stdlib.h>

int main (int argc, char *argv[])
{
    if (argc == 2)
    {
        if (strcmp(argv[1], "--stop") == 0)
        {
            char* parameters[] = {"vdr", "stop", (char*) 0};
            return execv("/etc/init.d/vdr", parameters);
        }
        else if (strcmp(argv[1], "--restart") == 0)
        {
            char* parameters[] = {"vdr", "restart", (char*) 0};
            return execv("/etc/init.d/vdr", parameters);
        }
    }
    return 1;
}
