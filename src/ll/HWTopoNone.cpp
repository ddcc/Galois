/** Machine Descriptions on BlueGeneQ -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2013, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS SOFTWARE
 * AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY, FITNESS FOR ANY
 * PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF PERFORMANCE, AND ANY
 * WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF DEALING OR USAGE OF TRADE.
 * NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH RESPECT TO THE USE OF THE
 * SOFTWARE OR DOCUMENTATION. Under no circumstances shall University be liable
 * for incidental, special, indirect, direct or consequential damages or loss of
 * profits, interruption of business, or related expenses which may arise from use
 * of Software or Documentation, including but not limited to those resulting from
 * defects in Software and/or Documentation, or loss or inaccuracy of data of any
 * kind.
 *
 * @section Description
 *
 * See HWTopoLinux.cpp.
 *
 * @author Donald Nguyen <ddn@cs.utexas.edu>
*/
#include "Galois/Runtime/ll/HWTopo.h"
#include "Galois/Runtime/ll/gio.h"

#include <vector>
#include <sched.h>

using namespace Galois::Runtime::LL;

namespace {

//! Flat machine with the correct number of threads and binding
struct Policy {
  std::vector<int> procmap; //Galois id -> cpu id

  unsigned numThreads, numCores, numPackages;

  Policy() {
    for (int i = 0; i < 16; ++i) {
      for (int j = 0; j < 4; ++j) {
        procmap.push_back(j*16 + i);
      }
    }
    numThreads = procmap.size();
    numCores = procmap.size();
    numPackages = 1;
  }
};

Policy& getPolicy() {
  static Policy A;
  return A;
}

} //namespace

bool Galois::Runtime::LL::bindThreadToProcessor(int id) {
  return true;
}

unsigned Galois::Runtime::LL::getProcessorForThread(int id) {
  return getPolicy().procmap[id];
}

unsigned Galois::Runtime::LL::getMaxThreads() {
  return getPolicy().numThreads;
}

unsigned Galois::Runtime::LL::getMaxCores() {
  return getPolicy().numCores;
}

unsigned Galois::Runtime::LL::getMaxPackages() {
  return getPolicy().numPackages;
}

unsigned Galois::Runtime::LL::getMaxPackageForThread(int id) {
  return getPolicy().numPackages - 1;
}

unsigned Galois::Runtime::LL::getPackageForThread(int id) {
  return 0;
}

bool Galois::Runtime::LL::isPackageLeader(int id) {
  return id == 0;
}

unsigned Galois::Runtime::LL::getLeaderForThread(int id) {
  return 0;
}

unsigned Galois::Runtime::LL::getLeaderForPackage(int id) {
  return 0;
}
