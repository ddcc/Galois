#ifndef GALOIS_RUNTIME_STM_H
#define GALOIS_RUNTIME_STM_H

#include "Galois/config.h"

#ifdef GALOIS_USE_TINYSTM
#include "Galois/Runtime/Support.h"
#include <stm.h>
#include <mod_stats.h>
#include <mod_mem.h>
#include <wrappers.h>
#endif

#ifndef XTM_DECL_LOCKABLE
#  define XTM_DECL_LOCKABLE(type, name) type name
#  define XTM_LOCKABLE_VALUE(expr) (expr)
#  define XTM_LOCKABLE_INIT(expr) 
#endif

namespace Galois {
namespace Runtime {
namespace Stm {

void stm_on_abort(void *arg);

#ifdef GALOIS_USE_TINYSTM
static inline void start() {
  stm_init();
  mod_mem_init(0);
  mod_stats_init();
  if (stm_register(NULL, NULL, NULL, NULL, NULL, stm_on_abort, NULL) == 0)
    abort();
}

static inline void stop() {
  unsigned long commits = 0;
  unsigned long aborts = 0;
  unsigned long retries = 0;
  stm_get_global_stats("global_nb_commits", &commits);
  stm_get_global_stats("global_nb_aborts", &aborts);
  stm_get_global_stats("global_max_retries", &retries);
  printf("STAT,(NULL),TinySTMCommits,1,%lu,%lu\n", commits, commits);
  printf("STAT,(NULL),TinySTMAborts,1,%lu,%lu\n", aborts, aborts);
  printf("STAT,(NULL),TinySTMRetries,1,%lu,%lu\n", retries, retries);
  stm_exit();
}

static inline void threadEnter() {
  stm_init_thread();
}

static inline void threadExit() {
  stm_exit_thread();
}

#  define GALOIS_STM_BEGIN() do { \
  stm_tx_attr_t _a; \
  _a.attrs = 0; \
  sigjmp_buf *_e = stm_start(_a); \
  if (_e != NULL) sigsetjmp(*_e, 0); \
} while (0)

#  define GALOIS_STM_END() stm_commit()
#  define GALOIS_STM_READ_WORD(var) stm_load((volatile stm_word_t*)(void*)&XTM_LOCKABLE_VALUE(var))
#  define GALOIS_STM_READ_PTR(var) stm_load_ptr((volatile void**)(void*)&XTM_LOCKABLE_VALUE(var))
#  define GALOIS_STM_WRITE_WORD(var, val) stm_store((volatile stm_word_t*)(void*)&XTM_LOCKABLE_VALUE(var), (stm_word_t)val)
#  define GALOIS_STM_WRITE_PTR(var, val) stm_store_ptr((volatile void**)(void*)&XTM_LOCKABLE_VALUE(var), val)
#else
static inline void start() { }
static inline void stop() { }
static inline void threadEnter() { }
static inline void threadExit() { }
#  define GALOIS_STM_BEGIN()
#  define GALOIS_STM_END()
#  define GALOIS_STM_READ_WORD(var) (var)
#  define GALOIS_STM_READ_PTR(var) (var)
#  define GALOIS_STM_WRITE_WORD(var, val) do { var = val; } while (0)
#  define GALOIS_STM_WRITE_PTR(var, val) do { var = val; } while (0)
#endif
}
}
}

#endif
