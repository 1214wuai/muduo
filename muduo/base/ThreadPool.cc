// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/ThreadPool.h"

#include "muduo/base/Exception.h"

#include <assert.h>
#include <stdio.h>

using namespace muduo;

ThreadPool::ThreadPool(const string& nameArg)
  : mutex_(),
    notEmpty_(mutex_),
    notFull_(mutex_),
    name_(nameArg),
    maxQueueSize_(0),
    running_(false)
{
}

ThreadPool::~ThreadPool()
{
  if (running_)  //如果线程池在运行，那就要进行内存处理，在stop()函数中执行
  {
    stop();
  }
}

void ThreadPool::start(int numThreads)
{
  assert(threads_.empty());  //确保未启动过
  running_ = true;  //启动标志
  threads_.reserve(numThreads);  //预留reserver个空间
  for (int i = 0; i < numThreads; ++i)  // 批量创建线程
  {
    char id[32];  //id存储线程id
    snprintf(id, sizeof id, "%d", i+1);
    threads_.emplace_back(new muduo::Thread(  //boost::bind在绑定类内部成员时，第二个参数必须是类的实例
          std::bind(&ThreadPool::runInThread, this), name_+id));//runInThread是每个线程的线程运行函数，线程为执行任务情况下会阻塞
    threads_[i]->start();//启动每个线程，但是由于线程运行函数是runInThread，所以会阻塞。
  }
  if (numThreads == 0 && threadInitCallback_)
  {
    threadInitCallback_();
  }
}

void ThreadPool::stop()
{
  {
  MutexLockGuard lock(mutex_);
  running_ = false;
  notEmpty_.notifyAll();
  notFull_.notifyAll();
  }
  for (auto& thr : threads_)
  {
    thr->join();
  }
}

size_t ThreadPool::queueSize() const
{
  MutexLockGuard lock(mutex_);
  return queue_.size();
}

// 向任务队列里面添加任务
void ThreadPool::run(Task task)
{
  if (threads_.empty())
  {
    task();
  }
  else
  {
    MutexLockGuard lock(mutex_);
    while (isFull() && running_)
    {
      notFull_.wait();
    }
    if (!running_) return;
    assert(!isFull());

    queue_.push_back(std::move(task));
    notEmpty_.notify();  // 唤醒一个消费者
  }
}

// 从任务队列中取出一个任务
ThreadPool::Task ThreadPool::take()
{
  MutexLockGuard lock(mutex_);  // 加锁
  // always use a while-loop, due to spurious wakeup
  while (queue_.empty() && running_)
  {
    notEmpty_.wait();  // 判断是否符合条件（不为空），不符合这里会释放锁并阻塞
  }
  Task task;
  if (!queue_.empty())
  {
    task = queue_.front();  // 从任务队列里取出任务
    queue_.pop_front();
    if (maxQueueSize_ > 0)
    {
      notFull_.notify();
    }
  }
  return task;  // 返回任务
}

bool ThreadPool::isFull() const
{
  mutex_.assertLocked();
  return maxQueueSize_ > 0 && queue_.size() >= maxQueueSize_;
}

void ThreadPool::runInThread()
{
  try
  {
    if (threadInitCallback_)
    {
      threadInitCallback_();
    }
    while (running_)
    {
      // 从任务队列中取出一个任务
      Task task(take());
      if (task)
      {
        // 执行任务
        task();
      }
    }
  }
  catch (const Exception& ex)
  {
    fprintf(stderr, "exception caught in ThreadPool %s\n", name_.c_str());
    fprintf(stderr, "reason: %s\n", ex.what());
    fprintf(stderr, "stack trace: %s\n", ex.stackTrace());
    abort();
  }
  catch (const std::exception& ex)
  {
    fprintf(stderr, "exception caught in ThreadPool %s\n", name_.c_str());
    fprintf(stderr, "reason: %s\n", ex.what());
    abort();
  }
  catch (...)
  {
    fprintf(stderr, "unknown exception caught in ThreadPool %s\n", name_.c_str());
    throw; // rethrow
  }
}

