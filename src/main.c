/**
 * @file main.c
 * @author Daniel Calabria
 *
 * Main driver for smash.
 **/

#include <stdio.h>
#include <stdlib.h>

#include "smash.h"
#include "debug.h"

int main(int argc, char *argv[], char *envp[])
{
    DEBUG("main() - ENTER [argc=%d, *argv[] @ %p, *envp[] @ %p]", argc, argv, envp);
    int retval = EXIT_SUCCESS;

    if(smash_setup(argc, argv) < 0)
    {
        ERROR("smash_setup() failed");
        retval = EXIT_FAILURE;
        goto main_end;
    }

    if(smash_main() < 0)
    {
        ERROR("smash_main() failed");
        retval = EXIT_FAILURE;
        goto main_end;
    }

main_end:
    DEBUG("main() - EXIT [%d]", retval);;
    return retval;
}
