#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "wrappedlibs.h"

#include "debug.h"
#include "wrapper.h"
#include "bridge.h"
#include "librarian/library_private.h"
#include "x86emu.h"
#include "emu/x86emu_private.h"
#include "box86context.h"
#include "librarian.h"
#include "callback.h"

const char* ldaprName =
#ifdef ANDROID
    "libldap_r-2.4.so"
#else
    "libldap_r-2.4.so.2"
#endif
    ;
#define LIBNAME ldapr

#define ADDED_FUNCTIONS()           \

#include "generated/wrappedldaprtypes.h"

#include "wrappercallback.h"

#define SUPER() \
GO(0)   \
GO(1)   \
GO(2)   \
GO(3)   \
GO(4)

// LDAP_SASL_INTERACT_PROC ...
#define GO(A)   \
static uintptr_t my_LDAP_SASL_INTERACT_PROC_fct_##A = 0;                                \
static int my_LDAP_SASL_INTERACT_PROC_##A(void* a, unsigned b, void* c, void* d)        \
{                                                                                       \
    return RunFunction(my_context, my_LDAP_SASL_INTERACT_PROC_fct_##A, 4, a, b, c, d);  \
}
SUPER()
#undef GO
static void* find_LDAP_SASL_INTERACT_PROC_Fct(void* fct)
{
    if(!fct) return fct;
    if(GetNativeFnc((uintptr_t)fct))  return GetNativeFnc((uintptr_t)fct);
    #define GO(A) if(my_LDAP_SASL_INTERACT_PROC_fct_##A == (uintptr_t)fct) return my_LDAP_SASL_INTERACT_PROC_##A;
    SUPER()
    #undef GO
    #define GO(A) if(my_LDAP_SASL_INTERACT_PROC_fct_##A == 0) {my_LDAP_SASL_INTERACT_PROC_fct_##A = (uintptr_t)fct; return my_LDAP_SASL_INTERACT_PROC_##A; }
    SUPER()
    #undef GO
    printf_log(LOG_NONE, "Warning, no more slot for libldap_r LDAP_SASL_INTERACT_PROC callback\n");
    return NULL;
}
#undef SUPER

EXPORT int my_ldap_sasl_interactive_bind_s(x86emu_t* emu, void* ld, void* dn, void* mechs, void* sctrls, void* cctrls, unsigned flags, void* f, void* defaults)
{
    (void)emu;
    return my->ldap_sasl_interactive_bind_s(ld, dn, mechs, sctrls, cctrls, flags, find_LDAP_SASL_INTERACT_PROC_Fct(f), defaults);
}

#ifdef ANDROID
#define NEEDED_LIB "liblber-2.4.so"
#else
#define NEEDED_LIB "liblber-2.4.so.2"
#endif

#define CUSTOM_INIT \
    getMy(lib);         \
    setNeededLibs(lib, 1, NEEDED_LIB);

#define CUSTOM_FINI \
    freeMy();

#include "wrappedlib_init.h"
