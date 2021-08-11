// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_COUNTDOWNLATCH_H
#define MUDUO_BASE_COUNTDOWNLATCH_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"

namespace muduo
{

class CountDownLatch : noncopyable
{
 public:

  explicit CountDownLatch(int count);

  void wait();

  void countDown();

  int getCount() const;

 private:
  mutable MutexLock mutex_;
  Condition condition_ GUARDED_BY(mutex_);
  int count_ GUARDED_BY(mutex_);
};
/*
GUARDED_BY(mutex_)
THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(mutex_))
__attribute__((guarded_by(mutex_))
guarded_by属性是为了保证线程安全，使用该属性后，线程要使用相应变量，必须先锁定mutex_

线程安全注解
GUARDED_BY//声明数据成员受给定功能保护。对数据的读取操作需要共享访问，而写入操作需要独占访问。
*/
}  // namespace muduo
#endif  // MUDUO_BASE_COUNTDOWNLATCH_H
