#include <stdlib.h>

#include "deet.h"
#include "deet_run.h"

int main(int argc, char *argv[]) {
    // Remember: Do not put any functions other than main() in this file.

    silent_logging = 0;

    run_deet(silent_logging);

    return 0;
}
