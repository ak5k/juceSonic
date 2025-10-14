/*
 * JSFX Standalone Stubs
 * Provides stub implementations for functions that would normally be provided by a host
 */

#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32
typedef long long INT_PTR;
#else
typedef long INT_PTR;
#endif

extern "C"
{
    // SWELL expects this with C linkage
    INT_PTR SWELLAppMain(int msg, INT_PTR parm1, INT_PTR parm2)
    {
        return 0;
    }
}
