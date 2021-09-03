// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/EventLoopThreadPool.h"

#include "muduo/net/EventLoop.h"
#include "muduo/net/EventLoopThread.h"

#include <stdio.h>

using namespace muduo;
using namespace muduo::net;

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop, const string& nameArg)
  : baseLoop_(baseLoop),
    name_(nameArg),
    started_(false),
    numThreads_(0),
    next_(0)
{
}

EventLoopThreadPool::~EventLoopThreadPool()
{
  // Don't delete loop, it's stack variable
}

void EventLoopThreadPool::start(const ThreadInitCallback& cb)
{
  assert(!started_);
  baseLoop_->assertInLoopThread();

  started_ = true;

  for (int i = 0; i < numThreads_; ++i)                                                   //批量创建n个事件循环线程
  {
    char buf[name_.size() + 32];
    snprintf(buf, sizeof buf, "%s%d", name_.c_str(), i);
    EventLoopThread* t = new EventLoopThread(cb, buf);
    threads_.push_back(std::unique_ptr<EventLoopThread>(t));                              //存储I/O线程
    loops_.push_back(t->startLoop());                                                     //存储每个I/O线程的EventLoop。此时，线程池的每个I/O线程都会启动，去执行EventLoop::loop()监听文件描述符
  }
  if (numThreads_ == 0 && cb)                                                             //线程池的个数为0，则主线程开始监听套接字
  {
    cb(baseLoop_);
  }
}

EventLoop* EventLoopThreadPool::getNextLoop()
{
  baseLoop_->assertInLoopThread();
  assert(started_);
  EventLoop* loop = baseLoop_;
  /************************************************************************
    轮询调度算法的原理是每一次把来自用户的请求轮流分配给内部中的服务器，从1
    开始，直到N(内部服务器个数)，然后重新开始循环。轮询调度算法假设所有服务器
    的处理性能都相同，不关心每台服务器的当前连接数和响应速度。当请求服务间隔
    时间变化比较大时，轮询调度算法容易导致服务器间的负载不平衡。
    *************************************************************************/

  if (!loops_.empty())
  {
    // round-robin
    loop = loops_[next_];                                                                  //获取某个线程的EventLoop，并返回
    ++next_;
    if (implicit_cast<size_t>(next_) >= loops_.size())
    {
      next_ = 0;
    }
  }
  return loop;
}

EventLoop* EventLoopThreadPool::getLoopForHash(size_t hashCode)                          //通过哈希法从EventLoop线程池中选择一个EventLoop
{
  baseLoop_->assertInLoopThread();
  EventLoop* loop = baseLoop_;

  if (!loops_.empty())
  {
    loop = loops_[hashCode % loops_.size()];
  }
  return loop;
}

std::vector<EventLoop*> EventLoopThreadPool::getAllLoops()                               //获得所有的EventLoop
{
  baseLoop_->assertInLoopThread();
  assert(started_);
  if (loops_.empty())
  {
    return std::vector<EventLoop*>(1, baseLoop_);
  }
  else
  {
    return loops_;
  }
}
