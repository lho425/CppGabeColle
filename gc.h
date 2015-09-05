#ifndef CPPGBCL_H
#define CPPGBCL_H

#include <iostream>
#include <cstdio>
#include <cstdint>
#include <cassert>
#include <utility>
#include <vector>
#include <list>
#include <memory>
#include <functional>

#ifdef CPPGBCL_DEBUG_MODE

#define GC_DEBUG(exp) (exp)

#else

#define GC_DEBUG(exp)

#endif


namespace cppgbcl {


struct cannot_copy_and_move {
cannot_copy_and_move() = default;
  cannot_copy_and_move(const cannot_copy_and_move&) = delete;
  cannot_copy_and_move(cannot_copy_and_move&&) = delete;
  operator=(const cannot_copy_and_move&) = delete;
  operator=(cannot_copy_and_move&&) = delete;
};

template<typename T>
using nondeletable_ptr = T*;

class GCObjectHolderBase;

class gc_ptr_base {

nondeletable_ptr<GCObjectHolderBase> holder_ptr;

public:

  gc_ptr_base(nondeletable_ptr<GCObjectHolderBase> p) :
    holder_ptr(p)
  {}
  
  auto get_holder_ptr() const {
    return holder_ptr;
  }

  explicit operator bool() {
    return bool(holder_ptr);
  }
  
  virtual ~gc_ptr_base() = default;
};


class GCObjectHolderBase {

  bool reachable = false;
  const std::size_t concrete_holder_size;
  std::vector<std::reference_wrapper<const gc_ptr_base>> child_gc_ptrs;

protected:
  GCObjectHolderBase(const std::size_t concrete_holder_size) :
    concrete_holder_size(concrete_holder_size)
  {}
public:
  GCObjectHolderBase() :
    concrete_holder_size(0)
  {}

  void register_child(const gc_ptr_base& gp) {
    child_gc_ptrs.push_back(gp);
  }

  void mark() {
    if (is_reachable()){
      return;
    }
    GC_DEBUG(std::clog << "GCObjectHolderBase: GCObjectHolderBase at " << this << " is reachable." << std::endl);
    set_reachable();

    for(const gc_ptr_base& gp : child_gc_ptrs) {
      auto p_holder = gp.get_holder_ptr();
      if (p_holder) {
        p_holder->mark();
      }
    }
  }

  std::size_t get_holder_size() {
    return concrete_holder_size;
  }

  void set_reachable() noexcept {
    reachable = true;
  }

  void set_not_reachable() noexcept {
    reachable = false;
  }

  bool is_reachable() const noexcept {
    return reachable;
  }
  
  virtual ~GCObjectHolderBase() = default;
};


template<typename T>
class GCObjectHolder : public GCObjectHolderBase {
  const std::size_t object_size;

  T object;

public:
  template <typename... Args>
  GCObjectHolder(Args&&...args) :
    GCObjectHolderBase(sizeof(GCObjectHolder<T>)),
    object_size(sizeof(T)),
    object(std::forward<Args>(args)...)
  {}

  T* get_object_ptr() noexcept {
    return &object;
  }
  
};



class RootGCPtrObserver : cannot_copy_and_move {

private:
  std::list<std::reference_wrapper<gc_ptr_base>> gc_ptr_ref_list;
  
public:
  void register_gc_ptr(gc_ptr_base& gcp) {
    gc_ptr_ref_list.push_back(gcp);
    GC_DEBUG(std::clog << "gc_ptr_base " << &gcp << " was registered." << std::endl);
  }

  void unregister_gc_ptr(gc_ptr_base& gcp) {
    GC_DEBUG(std::clog << "gc_ptr_base " << &gcp << " will be unregistered." << std::endl);

    auto rit = gc_ptr_ref_list.rbegin();
    
    if (&rit->get() == &gcp){
      GC_DEBUG(std::clog << "root gc pointer observer: last registered gc_ptr_base is matched.\n");
      gc_ptr_ref_list.erase(--rit.base());
      
      return;
    } 

    GC_DEBUG(std::clog << "root gc pointer observer: hmm... last registered gc_ptr_base is NOT matched.\n");
    ++rit;

    for(auto end = gc_ptr_ref_list.rend(); rit != end; ++rit){
      if (&rit->get() == &gcp){
        gc_ptr_ref_list.erase(--rit.base());
        return;
      }
    }


    std::clog << "never come here!" << std::endl;
    for(gc_ptr_base& gcp_ : gc_ptr_ref_list) {
      std::clog << &gcp_ << std::endl;
    }
    
    assert(false); //here is not reachable!
    
           
  }

  auto begin() {
    return gc_ptr_ref_list.begin();
  }

  auto end() {
    return gc_ptr_ref_list.end();
  }
  
  ~RootGCPtrObserver() {
    try {
      GC_DEBUG(std::clog << "~RootGCPtrObserver() at " << this << std::endl);
    } catch(...) {}
  }
};

class GCManager;


template<typename T>
class gc_ptr;

class gc_nullptr_t : private cannot_copy_and_move {
  GCManager &manager;
public:
  gc_nullptr_t(GCManager &manager) :
    manager(manager)
  {}

  template<typename T>
  operator gc_ptr<T> () {
    GC_DEBUG(std::clog << "gc_nullptr_t: assign to gc_ptr<T>\n");
    return gc_ptr<T>(nondeletable_ptr<GCObjectHolder<T>>(nullptr), manager);
  }
};


// まずはシングルスレッドで
class GCManager : private cannot_copy_and_move {
private:
  GCObjectHolderBase ghb;
  std::list<std::unique_ptr<GCObjectHolderBase>> all_holders;
  std::vector<std::reference_wrapper<GCObjectHolderBase>> holders_in_gc_new{ghb};
  RootGCPtrObserver root_observer;

public:

  // return: is_root
  bool manage(gc_ptr_base &gp) {
    GC_DEBUG(std::clog << "GCManager: manage" << std::endl);
    GC_DEBUG(std::clog << "GCManager: gc_ptr at " << &gp << std::endl);
    assert(holders_in_gc_new.size() > 0);
    GCObjectHolderBase& holder_in_gc_new = *--holders_in_gc_new.cend();
    intptr_t addr_of_gc_ptr = reinterpret_cast<intptr_t>(&gp);
    intptr_t start_addr_of_holder = reinterpret_cast<intptr_t>(&holder_in_gc_new);
    intptr_t end_addr_of_holder = start_addr_of_holder + holder_in_gc_new.get_holder_size();
    if (start_addr_of_holder <= addr_of_gc_ptr &&
        addr_of_gc_ptr < end_addr_of_holder) {
      GC_DEBUG(std::clog << "GCManager: gc_ptr at " << &gp << " is child." << std::endl);

      holder_in_gc_new.register_child(gp);
      
      return false;
    } else {
      GC_DEBUG(std::clog << "GCManager: gc_ptr at " << &gp << " is root." << std::endl);
      manage_as_root(gp);
      return true;
    }
  }

  void manage_as_root(gc_ptr_base &gp) {
    root_observer.register_gc_ptr(gp);
  }

  void unmanage_as_root(gc_ptr_base &gp) {
    root_observer.unregister_gc_ptr(gp);
  }
  gc_nullptr_t gc_null{*this};
  

  template<typename T, typename... Args>
  gc_ptr<T> gc_new(Args&& ... args){
    auto p =   static_cast<GCObjectHolder<T>*>(::operator new (sizeof(GCObjectHolder<T>)));
    try {
      holders_in_gc_new.push_back(*static_cast<GCObjectHolderBase*>(p));
      try {
        new(p) GCObjectHolder<T>(std::forward<Args>(args)...);
      } catch(...) {
        holders_in_gc_new.pop_back();
        throw;
      }
    } catch(...) {
      ::operator delete (p);
      throw;
    }

        // run_gc(); // ここでgcが起きるとpのchildが解放されて死ぬ

    
    // デリータの指定がないけどいいのか？
    // make_uniqueのアロケーターって::new使ってた気がするけど
    // デフォルトデリータってどうなってんの？
    all_holders.push_back(std::unique_ptr<GCObjectHolderBase>(p));
    holders_in_gc_new.pop_back();

    // run_gc(); // ここでgcが起きるとpが解放されて死ぬ
    return gc_ptr<T>(nondeletable_ptr<GCObjectHolder<T>>(p), *this);
  }
  
  
  void run_gc() {

    GC_DEBUG(std::clog << "GCManager: " << "gc marking phase " << std::endl);
    
    for(gc_ptr_base& gcp : root_observer) {
      GC_DEBUG(std::clog << "GCManager: " << "gc_ptr_base from root at " << &gcp << std::endl);
      GC_DEBUG(std::clog << "GCManager: " << "GCObjectHolderBase from root at " << gcp.get_holder_ptr() << std::endl);

      if (!gcp) {
        continue;
      }
      
      gcp.get_holder_ptr()->mark();
    }

    GC_DEBUG(std::clog << "GCManager: " << "gc sweeping phase " << std::endl);

    for(auto it = all_holders.begin(), end = all_holders.end(); it != end;) {
      GCObjectHolderBase& holder = **it;

      if (! holder.is_reachable()){
        GC_DEBUG(std::clog << "GCManager: " << "GCObjectHolderBase at " << &holder << " is garbage." << std::endl);
        it = all_holders.erase(it);
        continue;
      }
      GC_DEBUG(std::clog << "GCManager: " << "GCObjectHolderBase at " << &holder << " is living." << std::endl);
      holder.set_not_reachable();
      ++it;
    }
  }

  ~GCManager(){
    
    try {
      run_gc();
      if (all_holders.size() > 0) {
        fprintf(stderr,
                "~GCManager(): There are living  managed object. "
                "This is fatal programing error or bug of gc. "
                "force release.\n");
        // all_holders[n] can have reference of *this (GCManager&).
        // so, have to destruct all element before members of this destruct.
        all_holders.clear();
      }
    } catch(...) {
      // ?
    }
  }
};

template<typename T>
class gc_ptr : public gc_ptr_base {
private:
  T* _ptr;
  std::reference_wrapper<GCManager> manager_ref;
  const bool is_root;

  gc_ptr(T* ptr, nondeletable_ptr<GCObjectHolderBase> p_holder, GCManager& manager) :
    _ptr(ptr),
    gc_ptr_base(p_holder),
    manager_ref(manager),
    is_root(manager.manage(*this)) // 初期化中のオブジェクトを渡すので、危険が伴う。
  {
    GC_DEBUG(std::clog << "gc_ptr<T>: " << "gc_ptr<T> created at " << this << std::endl);
    GC_DEBUG(std::clog << "gc_ptr<T>: " << "GCObjectHolder<T> " << p_holder << " given" << std::endl);
    //is_root = (manager.manage(*this));
  }

protected:

public:

  // これpublicにしたくないんだけど
  gc_ptr(nondeletable_ptr<GCObjectHolder<T>> p_holder, GCManager& manager) :
    gc_ptr(p_holder->get_object_ptr(),
           nondeletable_ptr<GCObjectHolderBase>(p_holder),
           manager)
  {}

  gc_ptr(const gc_ptr& rhs) :
    gc_ptr(rhs._ptr, rhs.get_holder_ptr(), rhs.manager_ref.get())
  {}

  // パターン
  // root = 別のroot (1)
  // child = child
  // root = chlid
  // child = root
  // gc_null(root) = another root 
  // gc_null = child
  // gc_null = gc_null
  // (1) 以外rootは無視
  gc_ptr& operator=(const gc_ptr& rhs) {

    if (&manager_ref.get() != &rhs.manager_ref.get() && is_root) {
      GC_DEBUG(std::clog << "gc_ptr<T>: " << "gc_ptr<T> at " << this << " is root object." << std::endl);
      GC_DEBUG(std::clog << "gc_ptr<T>: " << "gc_ptr<T> rhs at "<< &rhs << " has another manager. register again." << std::endl);
      manager_ref.get().unmanage_as_root(*this);
      manager_ref = rhs.manager_ref;
      manager_ref.get().manage_as_root(*this);

    } else {
      manager_ref = rhs.manager_ref;
    }


    _ptr = rhs._ptr;

    this->gc_ptr_base::operator=(rhs);

    return *this;
      
    
  }

  ~gc_ptr() override {

    try {
      GC_DEBUG(std::clog << "gc_ptr<T>: " << "~gc_ptr<T>() at " << this << std::endl);
      if (is_root) {
        manager_ref.get().unmanage_as_root(*this);
      }
    } catch (...) {
    }

  }

  T& operator*() const noexcept {
    return *_ptr;
  }

  T* operator->() const noexcept {
    return _ptr;
  }

  T* get() const noexcept {
    return _ptr;
  }

};

} //namespace cppgbcl

#endif
