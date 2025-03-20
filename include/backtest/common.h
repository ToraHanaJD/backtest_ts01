#pragma once
#if defined(__linux__)
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ERROR(cleanup, ret, show_errstr, msgs...)                           \
({                                                                          \
    const char* _errstr_ = (show_errstr) ? strerror(errno) : "";            \
    (cleanup);                                                              \
    fprintf(stderr, "[<%s> @ %s: %d]: ", __FUNCTION__, __FILE__, __LINE__); \
    fprintf(stderr, ##msgs);                                                \
    fprintf(stderr, "%s\n", _errstr_);                                      \
    return (ret);                                                           \
})

#ifdef NDEBUG
#define DO_WITH_ASSERT(statement, expection)    \
    (statement)
#else
#define DO_WITH_ASSERT(statement, expection)    \
({                                              \
    auto _ret_ = (statement);                   \
    assert(expection);                          \
})
#endif

#define DIV_CEIL(n, p)                          \
({                                              \
    assert((n) >= 0);                           \
    assert((p) > 0);                            \
    ((n) == 0 ? 0 : ((n) - 1) / (p) + 1);       \
})

#define DIV_FLOOR(n, p)                         \
({                                              \
    assert((n) >= 0);                           \
    assert((p) > 0);                            \
    (n) / (p);                                  \
})
#endif
