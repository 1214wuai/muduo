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
  if (running_)  //å¦‚æœçº¿ç¨‹æ± åœ¨è¿è¡Œï¼Œé‚£å°±è¦è¿›è¡Œå†…å­˜å¤„ç†ï¼Œåœ¨stop()å‡½æ•°ä¸­æ‰§è¡Œ
  {
    stop();
  }
}

void ThreadPool::start(int numThreads)
{
  assert(threads_.empty());  //ç¡®ä¿æœªå¯åŠ¨è¿‡
  running_ = true;  ////å¯åŠ¨æ ‡å¿—
  threads_.reserve(numThreads);  //é¢„ç•™reserverä¸ªç©ºé—´
  for (int i = 0; i < numThreads; ++i)  // ÅúÁ¿´´½¨Ïß³Ì
  {
    char id[32];  //idå­˜å‚¨çº¿ç¨‹id
    snprintf(id, sizeof id, "%d", i+1);
    threads_.emplace_back(new muduo::Thread(  //boost::bindåœ¨ç»‘å®šç±»å†…éƒ¨æˆå‘˜æ—¶ï¼Œç¬¬äºŒä¸ªå‚æ•°å¿…é¡»æ˜¯ç±»çš„å®ä¾‹
          std::bind(&ThreadPool::runInThread, this), name_+id));//runInThreadæ˜¯æ¯ä¸ªçº¿ç¨‹çš„çº¿ç¨‹è¿è¡Œå‡½æ•°ï¼Œçº¿ç¨‹ä¸ºæ‰§è¡Œä»»åŠ¡æƒ…å†µä¸‹ä¼šé˜»å¡
    threads_[i]->start();//å¯åŠ¨æ¯ä¸ªçº¿ç¨‹ï¼Œä½†æ˜¯ç”±äºçº¿ç¨‹è¿è¡Œå‡½æ•°æ˜¯runInThreadï¼Œæ‰€ä»¥ä¼šé˜»å¡ã€‚
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

// ÏòÈÎÎñ¶ÓÁĞÀïÃæÌí¼ÓÈÎÎñ
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
    notEmpty_.notify();  // »½ĞÑÒ»¸öÏû·ÑÕß
  }
}

// ´ÓÈÎÎñ¶ÓÁĞÖĞÈ¡³öÒ»¸öÈÎÎñ
ThreadPool::Task ThreadPool::take()
{
  MutexLockGuard lock(mutex_);  // ¼ÓËø
  // always use a while-loop, due to spurious wakeup
  while (queue_.empty() && running_)
  {
    notEmpty_.wait();  // ÅĞ¶ÏÊÇ·ñ·ûºÏÌõ¼ş£¨²»Îª¿Õ£©£¬²»·ûºÏÕâÀï»áÊÍ·ÅËø²¢×èÈû
  }
  Task task;
  if (!queue_.empty())
  {
    task = queue_.front();  // ´ÓÈÎÎñ¶ÓÁĞÀïÈ¡³öÈÎÎñ
    queue_.pop_front();
    if (maxQueueSize_ > 0)
    {
      notFull_.notify();
    }
  }
  return task;  // ·µ»ØÈÎÎñ
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
      // ´ÓÈÎÎñ¶ÓÁĞÖĞÈ¡³öÒ»¸öÈÎÎñ
      Task task(take());
      if (task)
      {
        // Ö´ĞĞÈÎÎñ
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

