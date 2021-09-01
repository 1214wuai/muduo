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
  : loop_(CHECK_NOTNULL(loop)),                                                            //�ⲿ�����һ��EventLoop����鲻Ϊ��
    ipPort_(listenAddr.toIpPort()),                                                        //�󶨵ĵ�ַ
    name_(nameArg),                                                                        //��������
    acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),                       //����һ��Acceptor����
    threadPool_(new EventLoopThreadPool(loop, name_)),                                     //����һ��I/O�̳߳ض���
    connectionCallback_(defaultConnectionCallback),
    messageCallback_(defaultMessageCallback),                                              //��buffer�еĶ�������д��������
    nextConnId_(1)                                                                         //��һ������������IDΪ1
{
  acceptor_->setNewConnectionCallback(                                                     //�����׽��ֻ�ȡ�������Ӻ�ͻ�ִ�иûص�����
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

void TcpServer::setThreadNum(int numThreads)                                              //����I/O�̳߳��е��߳�����
{
  assert(0 <= numThreads);
  threadPool_->setThreadNum(numThreads);
}

void TcpServer::start()                                                                  //����������
{
  if (started_.getAndSet(1) == 0)                                                        //��started_��ǰ��ֵ���أ������ó��´����ֵ
  {
    threadPool_->start(threadInitCallback_);                                             //I/O�̳߳ؿ�ʼ����

    assert(!acceptor_->listening());                                                     //�ж��Ƿ��Ǽ�����
    loop_->runInLoop(
        std::bind(&Acceptor::listen, get_pointer(acceptor_)));                          //acceptor_��������������listenfd���ڼ���״̬���ٽ�listenfd����acceptChannel��epoll����
    /// �ɽ�std::bind��������һ��ͨ�õĺ�����������������һ���ɵ��ö�������һ���µĿɵ��ö���������Ӧ��ԭ����Ĳ����б�
  }
}

// accepter �������¼��󣬻���øú�������TcpServer�Ĺ��캯��������
void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr)
{
  // У�鵱ǰִ�����߳� �Ƿ��� EventLoop�Ĵ����߳�
  loop_->assertInLoopThread();

                                                                                       //�̳߳���ÿ���߳� ������һ�� EventLoop����ȡ����һ��EventLoop
  EventLoop* ioLoop = threadPool_->getNextLoop();                                      //����̳߳�Ϊ�գ����Ƿ��ص��Ǳ��߳��Լ�
  char buf[64];
  snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
  ++nextConnId_;                                                                       //ͳ�Ƶ�ǰ������
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
                                          peerAddr));                                  //����TcpConnectionPtr��share_ptr
  connections_[connName] = conn;                                                       //�������Ӽ���map��
  conn->setConnectionCallback(connectionCallback_);                                    //���û���ر����ã���������Ĭ�ϵģ���ӡһЩ��Ϣ
  conn->setMessageCallback(messageCallback_);                                          //���ö��¼�����ص�����
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  conn->setCloseCallback(
      std::bind(&TcpServer::removeConnection, this, _1)); // FIXME: unsafe             //��TcpConnection��handleClose������ִ��

  // ��ioLoop��channel�����������
  ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));              //��I/O�߳��е���ĳ���������ú������Կ��̵߳���,�������share_ptr
  Print();
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn)                         //ת��������ΪTcpServer
{
  // FIXME: unsafe
  LOG_INFO << "before removeConnectionInLoop:"<<conn.use_count();
  loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
  LOG_INFO << "before removeConnectionInLoop:"<<conn.use_count();
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
  loop_->assertInLoopThread();
  LOG_INFO << "TcpServer::removeConnectionInLoop [" << name_
           << "] - connection " << conn->name();
  size_t n = connections_.erase(conn->name());                                       //ִ����֮��conn�����ü����ѽ���1
  (void)n;
  assert(n == 1);
  EventLoop* ioLoop = conn->getLoop();
   LOG_INFO << "Before queueInLoop:"<<conn.use_count();
  ioLoop->queueInLoop(
      std::bind(&TcpConnection::connectDestroyed, conn));//����һ��Ҫʹ��queueInLoop,����ͻ���������ڹ��̵����
       // �˴�һ��Ҫ��EventLoop::queueInLoop()������Channel������ǰ����
       // ������boost::bind��TcpConnection�������ڳ�������connectDestroyed()��ʱ��
       // ʹ��boost::bind�õ�һ��boost::function����,���conn���ݽ�ȥ�����ü������1����Ϊ2
  LOG_INFO << "After queueInLoop:"<<conn.use_count();
}

