/** Sampling implementation -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2013, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 *
 * @author Andrew Lenharth <andrewl@lenharth.org>
 */
#include "Galois/config.h"
#include "Galois/Runtime/Sampling.h"
#include "Galois/Runtime/Support.h"
#include "Galois/Runtime/ll/EnvCheck.h"
#include "Galois/Runtime/ll/TID.h"
#include "Galois/Runtime/ll/gio.h"
#include <cstdlib>

static void endPeriod() {
  int val;
  if (Galois::Runtime::LL::EnvCheck("GALOIS_EXIT_AFTER_SAMPLING", val)) {
    exit(val);
  }
}

static void beginPeriod() {
  int val;
  if (Galois::Runtime::LL::EnvCheck("GALOIS_EXIT_BEFORE_SAMPLING", val)) {
    exit(val);
  }
}

#ifdef GALOIS_USE_VTUNE
#include "ittnotify.h"
#include "Galois/Runtime/ll/TID.h"

namespace vtune {
static bool isOn;
static void begin() {
  if (!isOn && Galois::Runtime::LL::getTID() == 0)
    __itt_resume();
  isOn = true;
}

static void end() {
  if (isOn && Galois::Runtime::LL::getTID() == 0)
    __itt_pause();
  isOn = false;
}
}
#else
namespace vtune {
static void begin() {}
static void end() {}
}
#endif

#ifdef GALOIS_USE_HPCTOOLKIT
#include <hpctoolkit.h>
#include "Galois/Runtime/ll/TID.h"

namespace hpctoolkit {
static bool isOn;
static void begin() {
  if (!isOn && Galois::Runtime::LL::getTID() == 0)
    hpctoolkit_sampling_start();
  isOn = true;
}

static void end() {
  if (isOn && Galois::Runtime::LL::getTID() == 0)
    hpctoolkit_sampling_stop();
  isOn = false;
}
}
#else
namespace hpctoolkit {
static void begin() {}
static void end() {}
}
#endif

#ifdef GALOIS_USE_PAPI
extern "C" {
#include <papi.h>
#include <papiStdEventDefs.h>
}
#include <iostream>
#include <string.h>

namespace papi {
static bool isInit;
static bool isSampling;
static __thread int papiEventSet = PAPI_NULL;

static int papiEvents[3] = {PAPI_L3_TCA, PAPI_L3_TCM, PAPI_TOT_CYC};
static const char* papiNames[3] = {"L3_ACCESSES","L3_MISSES", "CyclesCounter"};

//static int papiEvents[2] = {PAPI_TOT_INS, PAPI_TOT_CYC};
//static const char* papiNames[2] = {"Instructions", "Cycles"};

//static int papiEvents[2] = {PAPI_L1_DCM, PAPI_TOT_CYC};
//static const char* papiNames[2] = {"L1DCMCounter", "CyclesCounter"};

static_assert(sizeof(papiEvents)/sizeof(*papiEvents) == sizeof(papiNames)/sizeof(*papiNames),
    "PAPI Events != PAPI Names");

static unsigned long galois_get_thread_id() {
  return Galois::Runtime::LL::getTID();
}

static void begin(bool mainThread) {
  if (mainThread) {
    if (isSampling)
      GALOIS_DIE("Sampling already begun");
    isSampling = true;
  } else if (!isSampling) {
    return;
  }

  int rv;

  // Init library
  if (!isInit) {
    rv = PAPI_library_init(PAPI_VER_CURRENT);
    if (rv != PAPI_VER_CURRENT && rv < 0) {
      GALOIS_DIE("PAPI library version mismatch!");
    }
    if (rv < 0) GALOIS_DIE(PAPI_strerror(rv));
    if ((rv = PAPI_thread_init(galois_get_thread_id)) != PAPI_OK)
      GALOIS_DIE(PAPI_strerror(rv));
    isInit = true;
  }
  // Register thread
  if ((rv = PAPI_register_thread()) != PAPI_OK) 
    GALOIS_DIE(PAPI_strerror(rv));
  // Create the Event Set
  if ((rv = PAPI_create_eventset(&papiEventSet)) != PAPI_OK)
    GALOIS_DIE(PAPI_strerror(rv));
  if ((rv = PAPI_add_events(papiEventSet, papiEvents, sizeof(papiEvents)/sizeof(*papiEvents))) != PAPI_OK)
    GALOIS_DIE(PAPI_strerror(rv));
  // Start counting events in the event set
  if ((rv = PAPI_start(papiEventSet)) != PAPI_OK)
    GALOIS_DIE(PAPI_strerror(rv));
}

static void end(bool mainThread) {
  if (mainThread) {
    if (!isSampling)
      GALOIS_DIE("Sampling not yet begun");
    isSampling = false;
  } else if (!isSampling) {
    return;
  }

  int rv;

  long_long papiResults[sizeof(papiNames)/sizeof(*papiNames)];

  memset(&papiResults, 0, sizeof(papiResults));

  // Get the values
  if ((rv = PAPI_stop(papiEventSet, papiResults)) != PAPI_OK) {
    GALOIS_DIE(PAPI_strerror(rv));
  }
  // Remove all events in the eventset
  if ((rv = PAPI_cleanup_eventset(papiEventSet)) != PAPI_OK) {
    GALOIS_DIE(PAPI_strerror(rv));
  }
  // Free all memory and data structures, EventSet must be empty.
  if ((rv = PAPI_destroy_eventset(&papiEventSet)) != PAPI_OK) {
    GALOIS_DIE(PAPI_strerror(rv));
  }
  // Unregister thread
  if ((rv = PAPI_unregister_thread()) != PAPI_OK) {
    GALOIS_DIE(PAPI_strerror(rv));
  }

  for (unsigned i = 0; i < sizeof(papiNames)/sizeof(*papiNames); ++i)
    Galois::Runtime::reportStat(NULL, papiNames[i], papiResults[i]);
}

}
#else
namespace papi {
static void begin(bool) {}
static void end(bool) {}
}
#endif

void Galois::Runtime::beginThreadSampling() {
  papi::begin(false);
}

void Galois::Runtime::endThreadSampling() {
  papi::end(false);
}

void Galois::Runtime::beginSampling() {
  beginPeriod();
  papi::begin(true);
  vtune::begin();
  hpctoolkit::begin();
}

void Galois::Runtime::endSampling() {
  hpctoolkit::end();
  vtune::end();
  papi::end(true);
  endPeriod();
}
