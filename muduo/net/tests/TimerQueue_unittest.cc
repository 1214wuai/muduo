#include "muduo/net/EventLoop.h"
#include "muduo/net/EventLoopThread.h"
#include "muduo/base/Thread.h"

#include <stdio.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

int cnt = 0;
EventLoop* g_loop;

void printTid()
{
  printf("pid = %d, tid = %d\n", getpid(), CurrentThread::tid());
  printf("now %s\n", Timestamp::now().toString().c_str());
}

void print(const char* msg)
{
  printf("msg %s %s\n", Timestamp::now().toString().c_str(), msg);
  if (++cnt == 20)
  {
    g_loop->quit();
  }
}

void cancel(TimerId timer)
{
  g_loop->cancel(timer);
  printf("cancelled at %s\n", Timestamp::now().toString().c_str());
}

int main()
{
  printTid();
  sleep(1);
  {
    EventLoop loop;
    g_loop = &loop;

    print("main");
    loop.runAfter(1, std::bind(print, "once1"));
    loop.runAfter(1.5, std::bind(print, "once1.5"));
    loop.runAfter(2.5, std::bind(print, "once2.5"));
    loop.runAfter(3.5, std::bind(print, "once3.5"));
    TimerId t45 = loop.runAfter(4.5, std::bind(print, "once4.5"));           //4.5秒之后没有被触发，在4.2秒之后被取消，4.8秒之后再次被取消
    loop.runAfter(4.2, std::bind(cancel, t45));                              //4.2秒之后取消once4.5
    loop.runAfter(4.8, std::bind(cancel, t45));                              //4.8秒之后取消once4.5
    loop.runEvery(2, std::bind(print, "every2"));                            //每隔2秒触发一次，
    TimerId t3 = loop.runEvery(3, std::bind(print, "every3"));               //每隔3秒触发一次
    loop.runAfter(9.001, std::bind(cancel, t3));                             //every3在9s之后被取消

    loop.loop();
    print("main loop exits");                                                //main loop总是在监听19次之后就会结束。为啥？因为Print函数被调用20次之后会调用loop->quit()
  }
  sleep(1);
  {
    EventLoopThread loopThread;
    EventLoop* loop = loopThread.startLoop();
    loop->runAfter(2, printTid);
    sleep(3);
    print("thread loop exits");
  }
}

/*
输出：
pid = 19745, tid = 19745
now 1629798741.456381
msg 1629798742.456566 main
msg 1629798743.456642 once1
msg 1629798743.956635 once1.5
msg 1629798744.456639 every2
msg 1629798744.956640 once2.5
msg 1629798745.456638 every3
msg 1629798745.956639 once3.5
msg 1629798746.456662 every2
cancelled at 1629798746.656636
cancelled at 1629798747.256635
msg 1629798748.456662 every3
msg 1629798748.456670 every2
msg 1629798750.456688 every2
msg 1629798751.456685 every3
cancelled at 1629798751.457613
msg 1629798752.456716 every2
msg 1629798754.456741 every2
msg 1629798756.456763 every2
msg 1629798758.456791 every2
msg 1629798760.456828 every2
msg 1629798762.456856 every2
msg 1629798764.456878 every2
msg 1629798766.456902 every2
msg 1629798766.456914 main loop exits
pid = 19745, tid = 19837
now 1629798769.457240
msg 1629798770.457281 thread loop exits

*/
