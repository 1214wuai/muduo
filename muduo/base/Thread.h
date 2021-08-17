// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREAD_H
#define MUDUO_BASE_THREAD_H

#include "muduo/base/Atomic.h"
#include "muduo/base/CountDownLatch.h"
#include "muduo/base/Types.h"

#include <functional>
#include <memory>
#include <pthread.h>

namespace muduo
{
/*
非static成员函数在当作谓词函数被调用的时候，无法传递this指针
谓词是返回bool值的函数
仿函数，类对象调用重载过后的operator()运算符

【1】什么是值语义？
所谓值语义是指目标对象由源对象拷贝生成，且生成后与源对象完全无关，彼此独立存在，改变互不影响。
就像int类型变量相互拷贝一样。C++的内置类型（bool/int/double/char）都是值语义，
标准库里的complex<>、pair<>、vector<>、map<>、string等等类型也都是值语义。
拷贝之后就与源对象完全脱离关系。

【2】什么是对象语义？
对象语义也叫指针语义，引用语义等。
通常是指一个目标对象由源对象拷贝生成，但生成后与源对象之间依然共享底层资源，
对任何一个的改变都将随之改变另一个。
就像包含有指针成员变量的自定义类在默认拷贝构造函数下对其对象之间进行的拷贝。
拷贝后目标对象和源对象的指针成员变量仍指向同一块内存数据。
如果当其中一个被析构掉后，另一个对象的指针成员就会沦为名副其实的悬垂指针！
又比如，Thread是对象语义，拷贝Thread是无意义的，也是被禁止的：
因为Thread代表线程，拷贝一个Thread对象并不能让系统增加一个一模一样的线程。
 */
class Thread : noncopyable
{
 public:
  typedef std::function<void ()> ThreadFunc;

  // explicit修饰只有一个参数的类构造函数,
  // 或者有多个参数，但是除第一个参数外其他的参数都有默认值的构造函数。
  // 它的作用是表明该构造函数是显示的, 而非隐式的,
  // 它的作用是表明构造函数是显式方式显示的。（类构造函数默认为隐式）
  // explicit防止了类构造函数的隐式自动转换
  // 跟它相对应的另一个关键字是implicit, 意思是隐藏的,类构造函数默认情况下即声明为implicit(隐式).
  explicit Thread(ThreadFunc, const string& name = string());
  // FIXME: make it movable in C++11
  ~Thread();

  void start();
  int join(); // return pthread_join()

  bool started() const { return started_; }
  // pthread_t pthreadId() const { return pthreadId_; }
  pid_t tid() const { return tid_; }
  const string& name() const { return name_; }

  static int numCreated() { return numCreated_.get(); }

 private:
  void setDefaultName();

  bool       started_;
  bool       joined_;
  pthread_t  pthreadId_;
  pid_t      tid_;
  ThreadFunc func_;
  string     name_;
  CountDownLatch latch_;

  static AtomicInt32 numCreated_;//静态成员变量，记录当前进程除主线程之外，有多少个线程
};

}  // namespace muduo
#endif  // MUDUO_BASE_THREAD_H
