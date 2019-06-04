#include "Galois/Runtime/Stm.h"
#include "Galois/Runtime/Context.h"

#include <stdio.h>

#ifdef GALOIS_USE_TINYSTM

void Galois::Runtime::Stm::stm_on_abort(const struct stm_tx *tx, const stm_tx_abort_t reason, const void *arg) {
  applyReleasable();
  clearReleasable();
}

#endif
