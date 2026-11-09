/* Shared crash breadcrumb; available to both desktop and Web builds. */
#include "common.h"
#include "diagnostics.h"
volatile int g_dbgMark = 0;
