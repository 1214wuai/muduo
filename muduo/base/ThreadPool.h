// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREADPOOL_H
#define MUDUO_BASE_THREADPOOL_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"
#include "muduo/base/Thread.h"
#include "muduo/base/Types.h"

#include <deque>
#include <vector>

namespace muduo
{

/**
 * 什么是线程池？
 * 组成：
 * 1）多个线程
 * 2）一个线程安全的任务队列
 * 线程池子实际上是一个生产者消费者模型
 * 1）线程是消费者，从任务队列获取任务
 * 2）往任务队列添加任务的就是生产者
 *
 * 生产者消费者模型简介：(生产者、消费者、场地)
 * 1）生产者与生产者互斥
 * 2）消费者与消费者互斥
 * 3）生产者与消费者互斥同步
 * 通过互斥锁达到互斥目的
 *
 * 4）资源不够时候，通过需要让消费者等待
 * 5）资源够了后，需要有办法通知消费者进行消费
 * 这个过程通过条件变量实现
 *
 */

//muduo线程数目属于启动时配置，当线程池启动时，线程数目就已经固定下来。
class ThreadPool : noncopyable
{
 public:
  typedef std::function<void ()> Task;

  explicit ThreadPool(const string& nameArg = string("ThreadPool"));
  ~ThreadPool();

  // Must be called before start().
  void setMaxQueueSize(int maxSize) { maxQueueSize_ = maxSize; }//设置任务队列最大数目
  void setThreadInitCallback(const Task& cb)//设置线程执行前的回调函数
  { threadInitCallback_ = cb; }

  void start(int numThreads);
  void stop();

  const string& name() const
  { return name_; }

  size_t queueSize() const;

  // Could block if maxQueueSize > 0
  // Call after stop() will return immediately.
  // There is no move-only version of std::function in C++ as of C++14.
  // So we don't need to overload a const& and an && versions
  // as we do in (Bounded)BlockingQueue.
  // https://stackoverflow.com/a/25408989
  void run(Task f);

 private:
  bool isFull() const REQUIRES(mutex_);//判满
  void runInThread();//线程池的线程运行函数
  Task take();//取任务函数

  mutable MutexLock mutex_;
  Condition notEmpty_ GUARDED_BY(mutex_);//不空condition
  Condition notFull_ GUARDED_BY(mutex_);//未满condition
  string name_;
  Task threadInitCallback_;//线程执行前的回调函数
  std::vector<std::unique_ptr<muduo::Thread>> threads_;//线程数组
  std::deque<Task> queue_ GUARDED_BY(mutex_);//任务队列
  size_t maxQueueSize_; //因为deque是通过push_back增加线程数目的，所以通过外界max_queuesize存储最多线程数目
  bool running_; //线程池运行标志
};

}  // namespace muduo

#endif  // MUDUO_BASE_THREADPOOL_H
