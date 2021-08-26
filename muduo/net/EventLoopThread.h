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

// ר�Ŵ���io�¼����̣߳��߳���Ϊ��ȷ������ ���̹߳�����һ��EventLoop
class EventLoopThread : noncopyable
{
 public:
  typedef std::function<void(EventLoop*)> ThreadInitCallback;

  EventLoopThread(const ThreadInitCallback& cb = ThreadInitCallback(),
                  const string& name = string());
  ~EventLoopThread();
  EventLoop* startLoop();                                                      //������Աthread_�̣߳����߳̾ͳ���I/O�̣߳��ڲ�����thread_.start()

 private:
  void threadFunc();                                                           //�߳����к���

  EventLoop* loop_ GUARDED_BY(mutex_);                                         //ָ��һ��EventLoop����һ��I/O�߳�����ֻ��һ��EventLoop����
  bool exiting_;
  Thread thread_;                                                              //���ڶ��󣬰�����һ��thread�����
  MutexLock mutex_;                                                            // ��֤�ٽ������̰߳�ȫ�ģ�loop_ �����ٽ���
  Condition cond_ GUARDED_BY(mutex_);                                          // �������������������߳̿�ʼ�����¼�ѭ����loop_��Ϊ�գ��� ͨ��cond_���Ŷӣ��ȴ���֪ͨ
  ThreadInitCallback callback_;                                                //�ص�������EventLoop::loop�¼�ѭ��֮ǰ������
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_EVENTLOOPTHREAD_H

