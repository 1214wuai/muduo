// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/EventLoopThread.h"

#include "muduo/net/EventLoop.h"

using namespace muduo;
using namespace muduo::net;

EventLoopThread::EventLoopThread(const ThreadInitCallback& cb,
                                 const string& name)
  : loop_(NULL),                                                                   //loop未启动为NULL
    exiting_(false),
    thread_(std::bind(&EventLoopThread::threadFunc, this), name),                  //绑定线程运行函数
    mutex_(),
    cond_(mutex_),
    callback_(cb)                                                                  //初始化回调函数
{
}

EventLoopThread::~EventLoopThread()
{
  exiting_ = true;
  if (loop_ != NULL) // not 100% race-free, eg. threadFunc could be running callback_.
  {
    // still a tiny chance to call destructed object, if threadFunc exits just now.
    // but when EventLoopThread destructs, usually programming is exiting anyway.
    loop_->quit();                                                                 //退出I/O线程，让I/O线程的loop循环退出，从而退出了I/O线程
    thread_.join();
  }
}

EventLoop* EventLoopThread::startLoop()                                           //启动EventLoopThread中的loop循环，内部实际调用thread_.start
{
  assert(!thread_.started());
  thread_.start();                                                                //线程执行下面的threadFunc函数，启动loop循环，此时的loop_就已经不为空了

  EventLoop* loop = NULL;
  {
    MutexLockGuard lock(mutex_);
    while (loop_ == NULL)
    {
      cond_.wait();  // 等待loop_不为空，等待线程启动事件循环（等到条件达成）
    }
    loop = loop_;  // 这里指向一个局部变量的EventLoop
  }

  return loop;
}


//该函数是EventLoopThread类的核心函数，作用是启动loop循环
//该函数和上面的startLoop函数并发执行，所以需要上锁和condition
void EventLoopThread::threadFunc()
{
  EventLoop loop;

  if (callback_)                                                                   //线程初始化回调
  {
    callback_(&loop);                                                              //构造函数传递进来的，线程启动执行回调函数
  }

  {
    MutexLockGuard lock(mutex_);
    loop_ = &loop;                                                                 //然后loop_指针指向了这个创建的栈上的对象，threadFunc退出之后，这个指针就失效了，EventLoopThread自动析构
    cond_.notify();  //通知等待的执行流，表示条件已经达成，loop_不为空了

  }

  loop.loop();                                                                     //启动事件循环
                                       //该函数退出，意味着线程就退出了，EventLoopThread对象也就没有存在的价值了。但是muduo的EventLoopThread
                                       //实现为自动销毁的。一般loop函数退出整个程序就退出了，因而不会有什么大的问题，
                                       //因为muduo库的线程池就是启动时分配，并没有释放。所以线程结束一般来说就是整个程序结束了。
  }
  //assert(exiting_);
  MutexLockGuard lock(mutex_);
  loop_ = NULL;  // 结束事件循环后设置loop_为空
}

