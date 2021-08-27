// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_TCPSERVER_H
#define MUDUO_NET_TCPSERVER_H

#include "muduo/base/Atomic.h"
#include "muduo/base/Types.h"
#include "muduo/net/TcpConnection.h"

#include <map>

namespace muduo
{
namespace net
{

class Acceptor;
class EventLoop;
class EventLoopThreadPool;

///
/// TCP server, supports single-threaded and thread-pool models.（tcp服务器，支持单线程和多线程模式）
///
/// This is an interface class, so don't expose too much details.
class TcpServer : noncopyable
{
 public:
  typedef std::function<void(EventLoop*)> ThreadInitCallback;                             //线程初始化函数，也可以不设置
  enum Option
  {
    kNoReusePort,
    kReusePort,
  };

  //TcpServer(EventLoop* loop, const InetAddress& listenAddr);
  TcpServer(EventLoop* loop,
            const InetAddress& listenAddr,
            const string& nameArg,
            Option option = kNoReusePort);
  ~TcpServer();  // force out-line dtor, for std::unique_ptr members.

  const string& ipPort() const { return ipPort_; }
  const string& name() const { return name_; }
  EventLoop* getLoop() const { return loop_; }

  /// Set the number of threads for handling input.
  ///
  /// Always accepts new connection in loop's thread.
  /// Must be called before @c start
  /// @param numThreads
  /// - 0 means all I/O in loop's thread, no thread will created.
  ///   this is the default value.
  /// - 1 means all I/O in another thread.
  /// - N means a thread pool with N threads, new connections
  ///   are assigned on a round-robin basis.
  void setThreadNum(int numThreads);                                                             //设置I/O线程池中线程的数量，在start函数前调用
  void setThreadInitCallback(const ThreadInitCallback& cb)
  { threadInitCallback_ = cb; }
  /// valid after calling start()
  std::shared_ptr<EventLoopThreadPool> threadPool()
  { return threadPool_; }

  /// Starts the server if it's not listening.
  ///
  /// It's harmless to call it multiple times.
  /// Thread safe.
  void start();                                                                                  //如果处于监听状态就启动服务器,该函数可以被调用多次,线程安全的函数

  /// Set connection callback.
  /// Not thread safe.
  void setConnectionCallback(const ConnectionCallback& cb)                                       //设置建立连接成功后的回调,不是线程安全的
  { connectionCallback_ = cb; }

  /// Set message callback.
  /// Not thread safe.
  void setMessageCallback(const MessageCallback& cb)                                             //设置消息回调,不是线程安全的
  { messageCallback_ = cb; }

  /// Set write complete callback.
  /// Not thread safe.
  void setWriteCompleteCallback(const WriteCompleteCallback& cb)                                 //设置写回调，不是线程安全的
  { writeCompleteCallback_ = cb; }

 private:
  /// Not thread safe, but in loop
  void newConnection(int sockfd, const InetAddress& peerAddr);                                   //acceptor设置的回调
  /// Thread safe.
  void removeConnection(const TcpConnectionPtr& conn);                                           //移除已建立的连接
  /// Not thread safe, but in loop
  void removeConnectionInLoop(const TcpConnectionPtr& conn);                                     //实际上的移除连接的操作

  typedef std::map<string, TcpConnectionPtr> ConnectionMap;                                      //使用map维护了一个连接列表，连接名字:TcpConnection的share_ptr

  EventLoop* loop_;  // the acceptor loop
  const string ipPort_;                                                                          //服务器端口
  const string name_;                                                                            //服务器名
  std::unique_ptr<Acceptor> acceptor_; // avoid revealing Acceptor                               //起到监听作用的对象accept()
  std::shared_ptr<EventLoopThreadPool> threadPool_;                                              //I/O线程池
  ConnectionCallback connectionCallback_;                                                        //有一个默认的函数，内部就是输出本地的地址，对端的地址，连接状态
  MessageCallback messageCallback_;                                                              //每个连接的消息处理回调
  WriteCompleteCallback writeCompleteCallback_;
  ThreadInitCallback threadInitCallback_;                                                        //线程初始化函数
  AtomicInt32 started_;                                                                          //启动标记实际上是bool量，只不过用原子操作在0和1之间切换
  // always in loop thread
  int nextConnId_;                                                                               //下一个连接id

  // 保存每个链接
  ConnectionMap connections_;                                                                    //连接列表
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_TCPSERVER_H
