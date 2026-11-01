#ifndef PS3_COMPAT_H
#define PS3_COMPAT_H

#include <stdio.h>
#include <unistd.h>
#include <zlib.h>

FILE *ps3_msx_fopen(const char *path, const char *mode);
gzFile ps3_msx_gzopen(const char *path, const char *mode);

#define gzopen(N,M) ps3_msx_gzopen(N,M)
#undef fopen
#define fopen(N,M) ps3_msx_fopen(N,M)
#define chdir(P) (0)

#endif
