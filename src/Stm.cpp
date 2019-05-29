#include "Galois/Runtime/Stm.h"
#include "Galois/Runtime/Context.h"

#include <stdio.h>

void Galois::Runtime::Stm::stm_on_abort(void* arg) {
  applyReleasable();
  clearReleasable();
}
