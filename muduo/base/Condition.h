// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_CONDITION_H
#define MUDUO_BASE_CONDITION_H

#include "muduo/base/Mutex.h"

#include <pthread.h>
//RAII手法封装了条件变量
namespace muduo
{

class Condition : noncopyable
{
 public:
  explicit Condition(MutexLock& mutex)
    : mutex_(mutex)
  {
    MCHECK(pthread_cond_init(&pcond_, NULL));
  }

  ~Condition()
  {
    MCHECK(pthread_cond_destroy(&pcond_));
  }

  void wait()
  {
    MutexLock::UnassignGuard ug(mutex_);
    //进入wait时，先解锁（构造UnassignGuard类），退出wait时（析构UnassignGuard类），上锁。
    //这里所说的上锁，是对MutexLock类形式上的上锁，通过改变holder_的值来实现形式上的上锁
    MCHECK(pthread_cond_wait(&pcond_, mutex_.getPthreadMutex()));
    //一定要与一个互斥量相互使用。进入pthread_cond_wait()函数后，先上锁，
    //再将此线程挂到等待的队列中，然后解锁。直到满足条件，此线程会被唤醒，唤醒后先上锁，再退出pthread_cond_wait()函数。
  }

  // returns true if time out, false otherwise.
  bool waitForSeconds(double seconds);

  void notify()
  {
    MCHECK(pthread_cond_signal(&pcond_));
  }

  void notifyAll()
  {
    MCHECK(pthread_cond_broadcast(&pcond_));
  }

 private:
  MutexLock& mutex_;
  pthread_cond_t pcond_;
};

}  // namespace muduo

#endif  // MUDUO_BASE_CONDITION_H
