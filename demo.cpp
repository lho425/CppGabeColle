#include "gc.h"

#include <iostream>
#include <cstdio>

// 循環参照する単方向リストの例

using namespace cppgbcl;

GCManager manager;

struct Node { 

  gc_ptr<Node> next;

  Node(gc_ptr<Node> gp = manager.gc_null) :
    next(gp)
  {}

  ~Node() {
    printf("~Node() at %p.\n", this);
  }
};

int main() {
  gc_ptr<Node> node = manager.gc_new<Node>();
  node->next = manager.gc_new<Node>();
  node->next->next = manager.gc_new<Node>();
  node->next->next->next = node->next;// ここで循環参照
  std::cout << "#### run_gc() ####" << std::endl;
  manager.run_gc();
  std::cout << "#### end run_gc() ####" << std::endl;

  node = manager.gc_null; // ここでNodeにはたどり着けなくなる
  std::cout << "#### run_gc() ####" << std::endl;
  manager.run_gc();
  std::cout << "#### end run_gc() ####" << std::endl;

}
