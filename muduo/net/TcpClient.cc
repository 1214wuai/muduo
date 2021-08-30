// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include "muduo/net/TcpClient.h"

#include "muduo/base/Logging.h"
#include "muduo/net/Connector.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/SocketsOps.h"

#include <stdio.h>  // snprintf

using namespace muduo;
using namespace muduo::net;

// TcpClient::TcpClient(EventLoop* loop)
//   : loop_(loop)
// {
// }

// TcpClient::TcpClient(EventLoop* loop, const string& host, uint16_t port)
//   : loop_(CHECK_NOTNULL(loop)),
//     serverAddr_(host, port)
// {
// }

namespace muduo
{
namespace net
{
namespace detail
{

void removeConnection(EventLoop* loop, const TcpConnectionPtr& conn)
{
  loop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
}

void removeConnector(const ConnectorPtr& connector)
{
  //connector->
}

}  // namespace detail
}  // namespace net
}  // namespace muduo

TcpClient::TcpClient(EventLoop* loop,
                     const InetAddress& serverAddr,
                     const string& nameArg)
  : loop_(CHECK_NOTNULL(loop)),
    connector_(new Connector(loop, serverAddr)),
    name_(nameArg),
    connectionCallback_(defaultConnectionCallback),
    messageCallback_(defaultMessageCallback),
    retry_(false),
    connect_(true),
    nextConnId_(1)
{
  //将Connector的连接回调函数设置为TcpClient::newConnection
  //该函数在Connector::handleWrite中被调用，可写就表示连接已经建立成功了（但是还需判断是否出错）

  connector_->setNewConnectionCallback(
      std::bind(&TcpClient::newConnection, this, _1));
  // FIXME setConnectFailedCallback
  LOG_INFO << "TcpClient::TcpClient[" << name_
           << "] - connector " << get_pointer(connector_);
}

TcpClient::~TcpClient()
{
  LOG_INFO << "TcpClient::~TcpClient[" << name_
           << "] - connector " << get_pointer(connector_);
  TcpConnectionPtr conn;
  bool unique = false;
  {
    MutexLockGuard lock(mutex_);
    unique = connection_.unique();                                                            //查看share_ptr的引用计数是否为1，如果为1则返回true。
    conn = connection_;
  }
  if (conn)
  {
    assert(loop_ == conn->getLoop());
    // FIXME: not 100% safe, if we are in different thread
    CloseCallback cb = std::bind(&detail::removeConnection, loop_, _1);                      //调用TcpConnection::connectDestroyed
    loop_->runInLoop(
        std::bind(&TcpConnection::setCloseCallback, conn, cb));
    if (unique)
    {
      conn->forceClose();
    }
  }
  else                                                                                      //此时的conn还为空，说明连接还未建立起来
  {
    connector_->stop();                                                                     //马上去停止连接的建立
    // FIXME: HACK
    loop_->runAfter(1, std::bind(&detail::removeConnector, connector_));                    //1s后再执行renoveCinnector(该函数目前啥也没做)
  }
}

void TcpClient::connect()                                                                       //请求连接
{
  // FIXME: check state
  LOG_INFO << "TcpClient::connect[" << name_ << "] - connecting to "
           << connector_->serverAddress().toIpPort();
  connect_ = true;
  connector_->start();                                                                          //-->Connector::startInLoop()-->Connector::connect()
}

void TcpClient::disconnect()
{
  connect_ = false;

  {
    MutexLockGuard lock(mutex_);
    if (connection_)
    {
      connection_->shutdown();                                                                 //TcpConnection::shutdown()，关闭写端
    }
  }
}

void TcpClient::stop()
{
  connect_ = false;
  connector_->stop();                                                                         //最终执行channel_.reset();
}

//新连接的回调函数,将新连接封装为TcpConnection交给TcpClient来管理
void TcpClient::newConnection(int sockfd)
{
  loop_->assertInLoopThread();
  InetAddress peerAddr(sockets::getPeerAddr(sockfd));
  char buf[32];
  snprintf(buf, sizeof buf, ":%s#%d", peerAddr.toIpPort().c_str(), nextConnId_);
  ++nextConnId_;
  string connName = name_ + buf;

  InetAddress localAddr(sockets::getLocalAddr(sockfd));
  // FIXME poll with zero timeout to double confirm the new connection
  // FIXME use make_shared if necessary
  TcpConnectionPtr conn(new TcpConnection(loop_,
                                          connName,
                                          sockfd,
                                          localAddr,
                                          peerAddr));

  conn->setConnectionCallback(connectionCallback_);                                         //如果没有显示设置，则使用构造时的默认函数
  conn->setMessageCallback(messageCallback_);                                               //有默认函数，也可显示设置
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  conn->setCloseCallback(
      std::bind(&TcpClient::removeConnection, this, _1)); // FIXME: unsafe
  {
    MutexLockGuard lock(mutex_);
    connection_ = conn;
  }
  conn->connectEstablished();                                                               //关注可读，会执行一下connectionCallback_回调函数
}

void TcpClient::removeConnection(const TcpConnectionPtr& conn)                        //connection的close回调函数，调用TcpConnection::connectDestroyed
{
  loop_->assertInLoopThread();
  assert(loop_ == conn->getLoop());

  {
    MutexLockGuard lock(mutex_);
    assert(connection_ == conn);
    connection_.reset();
  }

  loop_->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
  if (retry_ && connect_)
  {
    LOG_INFO << "TcpClient::connect[" << name_ << "] - Reconnecting to "
             << connector_->serverAddress().toIpPort();
    connector_->restart();
  }
}

