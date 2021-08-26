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
  : loop_(NULL),                                                                   //loopδ����ΪNULL
    exiting_(false),
    thread_(std::bind(&EventLoopThread::threadFunc, this), name),                  //���߳����к���
    mutex_(),
    cond_(mutex_),
    callback_(cb)                                                                  //��ʼ���ص�����
{
}

EventLoopThread::~EventLoopThread()
{
  exiting_ = true;
  if (loop_ != NULL) // not 100% race-free, eg. threadFunc could be running callback_.
  {
    // still a tiny chance to call destructed object, if threadFunc exits just now.
    // but when EventLoopThread destructs, usually programming is exiting anyway.
    loop_->quit();                                                                 //�˳�I/O�̣߳���I/O�̵߳�loopѭ���˳����Ӷ��˳���I/O�߳�
    thread_.join();
  }
}

EventLoop* EventLoopThread::startLoop()                                           //����EventLoopThread�е�loopѭ�����ڲ�ʵ�ʵ���thread_.start
{
  assert(!thread_.started());
  thread_.start();                                                                //�߳�ִ�������threadFunc����������loopѭ������ʱ��loop_���Ѿ���Ϊ����

  EventLoop* loop = NULL;
  {
    MutexLockGuard lock(mutex_);
    while (loop_ == NULL)
    {
      cond_.wait();  // �ȴ�loop_��Ϊ�գ��ȴ��߳������¼�ѭ�����ȵ�������ɣ�
    }
    loop = loop_;  // ����ָ��һ���ֲ�������EventLoop
  }

  return loop;
}


//�ú�����EventLoopThread��ĺ��ĺ���������������loopѭ��
//�ú����������startLoop��������ִ�У�������Ҫ������condition
void EventLoopThread::threadFunc()
{
  EventLoop loop;

  if (callback_)                                                                   //�̳߳�ʼ���ص�
  {
    callback_(&loop);                                                              //���캯�����ݽ����ģ��߳�����ִ�лص�����
  }

  {
    MutexLockGuard lock(mutex_);
    loop_ = &loop;                                                                 //Ȼ��loop_ָ��ָ�������������ջ�ϵĶ���threadFunc�˳�֮�����ָ���ʧЧ�ˣ�EventLoopThread�Զ�����
    cond_.notify();  //֪ͨ�ȴ���ִ��������ʾ�����Ѿ���ɣ�loop_��Ϊ����

  }

  loop.loop();                                                                     //�����¼�ѭ��
                                       //�ú����˳�����ζ���߳̾��˳��ˣ�EventLoopThread����Ҳ��û�д��ڵļ�ֵ�ˡ�����muduo��EventLoopThread
                                       //ʵ��Ϊ�Զ����ٵġ�һ��loop�����˳�����������˳��ˣ����������ʲô������⣬
                                       //��Ϊmuduo����̳߳ؾ�������ʱ���䣬��û���ͷš������߳̽���һ����˵����������������ˡ�
  }
  //assert(exiting_);
  MutexLockGuard lock(mutex_);
  loop_ = NULL;  // �����¼�ѭ��������loop_Ϊ��
}

