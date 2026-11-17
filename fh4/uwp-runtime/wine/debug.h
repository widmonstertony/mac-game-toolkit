#pragma once
#include <stdio.h>
/* Standalone PE adapter: retain diagnostic sites without Wine's Unix logger. */
#define WINE_DEFAULT_DEBUG_CHANNEL(x)
#define TRACE(...) ((void)0)
#define FIXME(...) fprintf(stderr, "fh4-uwp: %s: partial implementation\n", __func__)
#define ERR(...) fprintf(stderr, "fh4-uwp: %s: failed\n", __func__)
