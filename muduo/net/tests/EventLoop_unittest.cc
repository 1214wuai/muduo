#include "muduo/net/EventLoop.h"
#include "muduo/base/Thread.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

EventLoop* g_loop;

void callback()
{
  printf("callback(): pid = %d, tid = %d\n", getpid(), CurrentThread::tid());
  EventLoop anotherLoop;//此线程在threadFunc中已经启动了一个loop，此处再启动一个loop会报错。
}

void threadFunc()
{
  printf("threadFunc(): pid = %d, tid = %d\n", getpid(), CurrentThread::tid());

  assert(EventLoop::getEventLoopOfCurrentThread() == NULL);
  EventLoop loop;
  assert(EventLoop::getEventLoopOfCurrentThread() == &loop);
  loop.runAfter(1.0, callback);
  loop.loop();
}

int main()
{
  printf("main(): pid = %d, tid = %d\n", getpid(), CurrentThread::tid());

  assert(EventLoop::getEventLoopOfCurrentThread() == NULL);//一个线程最多只有一个loop，此时该线程还没有创建loop对象
  EventLoop loop;
  assert(EventLoop::getEventLoopOfCurrentThread() == &loop);//该线程已经创建好了loop对象

  Thread thread(threadFunc);
  thread.start();

  loop.loop();
}
