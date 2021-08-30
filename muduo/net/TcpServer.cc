// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/TcpServer.h"

#include "muduo/base/Logging.h"
#include "muduo/net/Acceptor.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/EventLoopThreadPool.h"
#include "muduo/net/SocketsOps.h"

#include <stdio.h>  // snprintf

using namespace muduo;
using namespace muduo::net;

TcpServer::TcpServer(EventLoop* loop,
                     const InetAddress& listenAddr,
                     const string& nameArg,
                     Option option)
  : loop_(CHECK_NOTNULL(loop)),                                                            //外部传入的一个EventLoop，检查不为空
    ipPort_(listenAddr.toIpPort()),                                                        //绑定的地址
    name_(nameArg),                                                                        //服务名称
    acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),                       //构造一个Acceptor对象
    threadPool_(new EventLoopThreadPool(loop, name_)),                                     //构造一个I/O线程池对象
    connectionCallback_(defaultConnectionCallback),
    messageCallback_(defaultMessageCallback),                                              //将buffer中的读索引和写索引重置
    nextConnId_(1)                                                                         //下一个建立的连接ID为1
{
  acceptor_->setNewConnectionCallback(                                                     //监听套接字获取到新链接后就会执行该回调函数
      std::bind(&TcpServer::newConnection, this, _1, _2));
}

TcpServer::~TcpServer()
{
  loop_->assertInLoopThread();
  LOG_TRACE << "TcpServer::~TcpServer [" << name_ << "] destructing";

  for (auto& item : connections_)
  {
    TcpConnectionPtr conn(item.second);
    item.second.reset();
    conn->getLoop()->runInLoop(
      std::bind(&TcpConnection::connectDestroyed, conn));
  }
}

void TcpServer::setThreadNum(int numThreads)                                              //设置I/O线程池中的线程数量
{
  assert(0 <= numThreads);
  threadPool_->setThreadNum(numThreads);
}

void TcpServer::start()                                                                  //开启服务器
{
  if (started_.getAndSet(1) == 0)                                                        //将started_当前数值返回，并设置成新传入的值
  {
    threadPool_->start(threadInitCallback_);                                             //I/O线程池开始工作

    assert(!acceptor_->listening());                                                     //判断是否是监听中
    loop_->runInLoop(
        std::bind(&Acceptor::listen, get_pointer(acceptor_)));                          //acceptor_启动监听任务，让listenfd处于监听状态，再将listenfd纳入acceptChannel的epoll管理
    /// 可将std::bind函数看作一个通用的函数适配器，它接受一个可调用对象，生成一个新的可调用对象来“适应”原对象的参数列表。
  }
}

// accepter 触发读事件后，会调用该函数，在TcpServer的构造函数中设置
void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr)
{
  // 校验当前执行流线程 是否是 EventLoop的创建线程
  loop_->assertInLoopThread();

                                                                                       //线程池中每个线程 都管理一个 EventLoop，获取其中一个EventLoop
  EventLoop* ioLoop = threadPool_->getNextLoop();                                      //如果线程池为空，则是返回的是本线程自己
  char buf[64];
  snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
  ++nextConnId_;                                                                       //统计当前连接数
  string connName = name_ + buf;

  LOG_INFO << "TcpServer::newConnection [" << name_
           << "] - new connection [" << connName
           << "] from " << peerAddr.toIpPort();
  InetAddress localAddr(sockets::getLocalAddr(sockfd));
  // FIXME poll with zero timeout to double confirm the new connection
  // FIXME use make_shared if necessary
  TcpConnectionPtr conn(new TcpConnection(ioLoop,
                                          connName,
                                          sockfd,
                                          localAddr,
                                          peerAddr));                                  //构建TcpConnectionPtr，share_ptr
  connections_[connName] = conn;                                                       //将新连接加入map中
  conn->setConnectionCallback(connectionCallback_);                                    //如果没有特别设置，将会会调用默认的，打印一些信息
  conn->setMessageCallback(messageCallback_);                                          //设置读事件处理回调函数
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  conn->setCloseCallback(
      std::bind(&TcpServer::removeConnection, this, _1)); // FIXME: unsafe             //在TcpConnection的handleClose函数中执行

  // 让ioLoop的channel监听这个连接
  ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));              //在I/O线程中调用某个函数，该函数可以跨线程调用,传入的是share_ptr
  Print();
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn)
{
  // FIXME: unsafe
  loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
  loop_->assertInLoopThread();
  LOG_INFO << "TcpServer::removeConnectionInLoop [" << name_
           << "] - connection " << conn->name();
  size_t n = connections_.erase(conn->name());
  (void)n;
  assert(n == 1);
  EventLoop* ioLoop = conn->getLoop();
  ioLoop->queueInLoop(
      std::bind(&TcpConnection::connectDestroyed, conn));
}

