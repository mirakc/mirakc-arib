#pragma once

#include <cstdlib>

static_assert(EXIT_SUCCESS == 0);
static_assert(EXIT_FAILURE == 1);

// This exit code is returned when `filter-program` stopped before the program
// starts.  This situation occurs when the program was canceled or rescheduled.
#define EXIT_RETRY 2
