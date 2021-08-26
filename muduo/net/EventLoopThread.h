// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_EVENTLOOPTHREAD_H
#define MUDUO_NET_EVENTLOOPTHREAD_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"
#include "muduo/base/Thread.h"

namespace muduo
{
namespace net
{

class EventLoop;

// 专门处理io事件的线程（线程行为已确定）， 该线程管理了一个EventLoop
class EventLoopThread : noncopyable
{
 public:
  typedef std::function<void(EventLoop*)> ThreadInitCallback;

  EventLoopThread(const ThreadInitCallback& cb = ThreadInitCallback(),
                  const string& name = string());
  ~EventLoopThread();
  EventLoop* startLoop();                                                      //启动成员thread_线程，该线程就成了I/O线程，内部调用thread_.start()

 private:
  void threadFunc();                                                           //线程运行函数

  EventLoop* loop_ GUARDED_BY(mutex_);                                         //指向一个EventLoop对象，一个I/O线程有且只有一个EventLoop对象
  bool exiting_;
  Thread thread_;                                                              //基于对象，包含了一个thread类对象
  MutexLock mutex_;                                                            // 保证临界区是线程安全的，loop_ 就是临界区
  Condition cond_ GUARDED_BY(mutex_);                                          // 该条件变量的条件是线程开始启动事件循环（loop_不为空）， 通过cond_来排队，等待、通知
  ThreadInitCallback callback_;                                                //回调函数在EventLoop::loop事件循环之前被调用
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_EVENTLOOPTHREAD_H

