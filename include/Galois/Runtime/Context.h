/** simple galois context and contention manager -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2012, The University of Texas at Austin. All rights reserved.
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
 * @section Description
 *
 * @author Andrew Lenharth <andrewl@lenharth.org>
 */
#ifndef GALOIS_RUNTIME_CONTEXT_H
#define GALOIS_RUNTIME_CONTEXT_H

#include "Galois/config.h"
#include "Galois/MethodFlags.h"
#include "Galois/Runtime/Stm.h"
#include "Galois/Runtime/ll/PtrLock.h"
#include "Galois/Runtime/ll/gio.h"

#include <boost/utility.hpp>

#include <cassert>
#include <cstdlib>

#ifdef GALOIS_USE_LONGJMP
#include <setjmp.h>
#endif

namespace Galois {
namespace Runtime {

enum ConflictFlag {
  CONFLICT = -1,
  NO_CONFLICT = 0,
  REACHED_FAILSAFE = 1,
  BREAK = 2
};

enum PendingFlag {
  NON_DET,
  PENDING,
  COMMITTING
};

//! Used by deterministic and ordered executor
void setPending(PendingFlag value);
PendingFlag getPending ();

//! used to release lock over exception path
static inline void clearConflictLock() { }

#ifdef GALOIS_USE_LONGJMP
extern __thread jmp_buf hackjmp;

/**
 * Stack-allocated classes that require their deconstructors to be called,
 * after an abort for instance, should inherit from this class. 
 */
class Releasable {
  Releasable* next;
public:
  Releasable();
  virtual ~Releasable() { }
  virtual void release() = 0;
  void releaseAll();
};
void clearReleasable();
void applyReleasable();
#else
class Releasable {
public:
  virtual ~Releasable() { }
  virtual void release() = 0;
};
static inline void clearReleasable() { }
static inline void applyReleasable() { }
#endif

class LockManagerBase; 

#if defined(GALOIS_USE_SEQ_ONLY)
class Lockable { };

class LockManagerBase: private boost::noncopyable {
protected:
  enum AcquireStatus {
    FAIL, NEW_OWNER, ALREADY_OWNER
  };

  AcquireStatus tryAcquire(Lockable* lockable) { return FAIL; }
  bool stealByCAS(Lockable* lockable, LockManagerBase* other) { return false; }
  void ownByForce(Lockable* lockable) { }
  void release (Lockable* lockable) {}
  static bool tryLock(Lockable* lockable) { return false; }
  static LockManagerBase* getOwner(Lockable* lockable) { return 0; }
};

class SimpleRuntimeContext: public LockManagerBase {
  friend void doAcquire(Lockable*);
protected:
  void acquire(Lockable* lockable) { }
  void release (Lockable* lockable) {}
  virtual void subAcquire(Lockable* lockable);
  void addToNhood(Lockable* lockable) { }
  static SimpleRuntimeContext* getOwner(Lockable* lockable) { return 0; }

public:
  SimpleRuntimeContext(bool child = false): LockManagerBase () { }
  virtual ~SimpleRuntimeContext() { }
  void startIteration() { }
  
  unsigned cancelIteration() { return 0; }
  unsigned commitIteration() { return 0; }
};
#elif defined(GALOIS_USE_TINYSTM) || defined(GALOIS_USE_XTM)
class SimpleRuntimeContext;

class Lockable { 
  XTM_DECL_LOCKABLE(SimpleRuntimeContext*, owner);
  XTM_DECL_LOCKABLE(Lockable*, next);
  friend class SimpleRuntimeContext;
public:
  Lockable() {
    XTM_LOCKABLE_VALUE(owner) = 0;
    XTM_LOCKABLE_INIT(owner);
    XTM_LOCKABLE_VALUE(next) = 0;
    XTM_LOCKABLE_INIT(next);
  }
};

class SimpleRuntimeContext: private boost::noncopyable {
  XTM_DECL_LOCKABLE(Lockable*, locks);
protected:
  virtual void subAcquire(Lockable* lockable);
  int tryAcquire(Lockable* lockable) {abort(); return 0; }
  void insertLockable(Lockable* lockable) { 
    GALOIS_STM_WRITE_PTR(lockable->next, XTM_LOCKABLE_VALUE(locks));
    GALOIS_STM_WRITE_PTR(locks, lockable);
  }
  static bool tryLock(Lockable* lockable) { abort(); return false; }
  bool tryLockOwner(Lockable* lockable) { abort(); return false; }
  bool stealingCasOwner(Lockable* lockable, SimpleRuntimeContext* other) { abort(); return false; }
  void setOwner(Lockable* lockable) { abort(); }
  SimpleRuntimeContext* getOwner(Lockable* lockable) { abort(); return 0; }

public:
  SimpleRuntimeContext(bool child = false) { 
    if (child) abort(); 
    XTM_LOCKABLE_VALUE(locks) = 0;
    XTM_LOCKABLE_INIT(locks);
  }
  virtual ~SimpleRuntimeContext() { }
  void startIteration() { }
  
  unsigned cancelIteration() { return commitIteration(); }
  unsigned commitIteration() { 
    unsigned numLocks = 0;
    Lockable* L;
    // XXX(ddn): Hack to allow committing/aborting outside a tinySTM transaction 
    if (!XTM_LOCKABLE_VALUE(locks))
      return 0;
    
    while ((L = (Lockable*) GALOIS_STM_READ_PTR(locks))) {
      GALOIS_STM_WRITE_PTR(locks, XTM_LOCKABLE_VALUE(L->next));
      GALOIS_STM_WRITE_PTR(L->next, 0);
      //SimpleRuntimeContext* other = (SimpleRuntimeContext*) GALOIS_STM_READ_PTR(L->owner);
      //if (other != this)
      //  GALOIS_DIE("Releasing not me! ", other);
      GALOIS_STM_WRITE_PTR(L->owner, 0);
      ++numLocks;
    }

    return numLocks;
  }
  
  void acquire(Lockable* lockable) { 
    SimpleRuntimeContext* other = (SimpleRuntimeContext*) GALOIS_STM_READ_PTR(lockable->owner);
    if (other == 0) {
      GALOIS_STM_WRITE_PTR(lockable->owner, this);
      insertLockable(lockable);
    } else if (other == this) {
      return;
    } else {
      GALOIS_STM_WRITE_PTR(lockable->owner, this);
      insertLockable(lockable);
    }
  }
};

#else
/**
 * All objects that may be locked (nodes primarily) must inherit from
 * Lockable. 
 */
class Lockable {
  LL::PtrLock<LockManagerBase, true> owner;
  //! Use an intrusive list to track neighborhood of a context without allocation overhead.
  //! Works for cases where a Lockable needs to be only in one context's neighborhood list
  Lockable* next;
  friend class LockManagerBase;
  friend class SimpleRuntimeContext;
public:
  Lockable() :next(0) {}
};

class LockManagerBase: private boost::noncopyable {
protected:
  enum AcquireStatus {
    FAIL, NEW_OWNER, ALREADY_OWNER
  };

  AcquireStatus tryAcquire(Lockable* lockable);

  inline bool stealByCAS(Lockable* lockable, LockManagerBase* other) {
    assert(lockable != NULL);
    return lockable->owner.stealing_CAS(other, this);
  }

  inline void ownByForce(Lockable* lockable) {
    assert(lockable != NULL);
    assert(!lockable->owner.getValue());
    lockable->owner.setValue(this);
  }

  inline void release(Lockable* lockable) {
    assert(lockable != NULL);
    assert(getOwner(lockable) == this);
    lockable->owner.unlock_and_clear();
  }

  inline static bool tryLock(Lockable* lockable) {
    assert(lockable != NULL);
    return lockable->owner.try_lock();
  }

  inline static LockManagerBase* getOwner(Lockable* lockable) {
    assert(lockable != NULL);
    return lockable->owner.getValue();
  }
};

class SimpleRuntimeContext: public LockManagerBase {
  //! The locks we hold
  Lockable* locks;
  bool customAcquire;

protected:
  friend void doAcquire(Lockable*);

  static SimpleRuntimeContext* getOwner(Lockable* lockable) {
    LockManagerBase* owner = LockManagerBase::getOwner (lockable);
    return static_cast<SimpleRuntimeContext*>(owner);
  }

  virtual void subAcquire(Lockable* lockable);

  void addToNhood(Lockable* lockable) {
    assert(!lockable->next);
    lockable->next = locks;
    locks = lockable;
  }

  void acquire(Lockable* lockable);
  void release(Lockable* lockable);

public:
  SimpleRuntimeContext(bool child = false): locks(0), customAcquire(child) { }
  virtual ~SimpleRuntimeContext() { }

  void startIteration() {
    assert(!locks);
  }
  
  unsigned cancelIteration();
  unsigned commitIteration();
};
#endif

//! get the current conflict detection class, may be null if not in parallel region
SimpleRuntimeContext* getThreadContext();

//! used by the parallel code to set up conflict detection per thread
void setThreadContext(SimpleRuntimeContext* n);

//! Helper function to decide if the conflict detection lock should be taken
inline bool shouldLock(const Galois::MethodFlag g) {
#ifdef GALOIS_USE_SEQ_ONLY
  return false;
#else
  // Mask out additional "optional" flags
  switch (g & ALL) {
  case NONE:
  case SAVE_UNDO:
    return false;
  case ALL:
  case CHECK_CONFLICT:
    return true;
  default:
    // XXX(ddn): Adding error checking code here either upsets the inlining
    // heuristics or icache behavior. Avoid complex code if possible.
    //GALOIS_DIE("shouldn't get here");
    assert(false);
  }
  return false;
#endif
}

//! actual locking function.  Will always lock.
inline void doAcquire(Lockable* lockable) {
  SimpleRuntimeContext* ctx = getThreadContext();
  if (ctx)
    ctx->acquire(lockable);
}

//! Master function which handles conflict detection
//! used to acquire a lockable thing
inline void acquire(Lockable* lockable, Galois::MethodFlag m) {
  if (shouldLock(m)) {
    doAcquire(lockable);
  }
}

struct AlwaysLockObj {
  void operator()(Lockable* lockable) const {
    doAcquire(lockable);
  }
};

struct CheckedLockObj {
  Galois::MethodFlag m;
  CheckedLockObj(Galois::MethodFlag _m) :m(_m) {}
  void operator()(Lockable* lockable) const {
    acquire(lockable, m);
  }
};

void signalConflict(Lockable*);

void forceAbort();

}
} // end namespace Galois

#endif
