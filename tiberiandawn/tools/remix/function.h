/*
 * Minimal function.h for remix host builds (must appear before $(TDROOT) on -I path).
 */
#ifndef FUNCTION_H
#define FUNCTION_H

#include <stddef.h>

typedef int BOOL;
#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif

#include "memflag.h"

#endif /* FUNCTION_H */
