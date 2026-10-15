/* Just what the msun sources need of FreeBSD's <sys/cdefs.h> */
#pragma once
#define __FBSDID(s)
#define __weak_reference(sym, alias)
#define __strong_reference(sym, alias)
#define __BEGIN_DECLS
#define __END_DECLS
#define __unused __attribute__((unused))
#define __pure2
#define __always_inline __attribute__((always_inline))
