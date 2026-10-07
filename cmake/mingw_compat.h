/* Force-included when cross-building with mingw-w64.
 * MSVC's <stdlib.h> provides min/max macros; mingw-w64 disables them (#if 0). */
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

#include <ctype.h> /* stb_image relies on tolower; MSVC headers pull it in transitively */
#include <stddef.h> /* offsetof() without pulling in any other header */

#ifndef _inline
#define _inline static inline
#endif
