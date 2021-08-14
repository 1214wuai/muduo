// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_COUNTDOWNLATCH_H
#define MUDUO_BASE_COUNTDOWNLATCH_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"
/*
 * CountDownLatch可以使一个获多个线程等待其他线程各自执行完毕后再执行。
 * CountDownLatch 定义了一个计数器，和一个阻塞队列， 当计数器的值递减为0之前，阻塞队列里面的线程处于挂起状态，
 * 当计数器递减到0时会唤醒阻塞队列所有线程，这里的计数器是一个标志，可以表示一个任务一个线程，也可以表示一个倒计时器，
 * CountDownLatch可以解决那些一个或者多个线程在执行之前必须依赖于某些必要的前提业务先执行的场景。
 */
namespace muduo
{

class CountDownLatch : noncopyable
{
 public:

  explicit CountDownLatch(int count);//构造方法，创建一个值为count 的计数器。

  void wait();//阻塞当前线程，将当前线程加入阻塞队列。

  void countDown();//对计数器进行递减1操作，当计数器递减至0时，当前线程会去唤醒阻塞队列里的所有线程。

  int getCount() const;

 private:
  mutable MutexLock mutex_;//被mutable修饰的成员变量可以在const成员函数中被修改
  Condition condition_ GUARDED_BY(mutex_);//条件变量
  int count_ GUARDED_BY(mutex_);//计数器
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
