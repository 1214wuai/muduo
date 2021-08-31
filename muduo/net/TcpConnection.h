// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_TCPCONNECTION_H
#define MUDUO_NET_TCPCONNECTION_H

#include "muduo/base/noncopyable.h"
#include "muduo/base/StringPiece.h"
#include "muduo/base/Types.h"
#include "muduo/net/Callbacks.h"
#include "muduo/net/Buffer.h"
#include "muduo/net/InetAddress.h"

#include <memory>

#include <boost/any.hpp>

// struct tcp_info is in <netinet/tcp.h>
struct tcp_info;

namespace muduo
{
namespace net
{

class Channel;
class EventLoop;
class Socket;

///
/// TCP connection, for both client and server usage.
///
/// This is an interface class, so don't expose too much details.

//该类对象，客户端和服务器都会用到
//这是一个接口类，所以不用暴露太多细节



//该类中使用shared_from_this()，需要继承enable_shared_from_this

class TcpConnection : noncopyable,
                      public std::enable_shared_from_this<TcpConnection>
{
 public:
  /// Constructs a TcpConnection with a connected sockfd
  ///
  /// User should not create this object.

  ///用一个已建立连接成功的sockfd来构造该类对象
  ///用户不应该自己构造。该类对象会在accept()之后调用一个连接回调函数，在其中会构造一个该类对象

  ///从属的loop，I/O线程池有线程时，则是该loop属于其中一个线程的，如果没有时，则就是主线程的loop
  TcpConnection(EventLoop* loop,
                const string& name,                                                           //连接名称
                int sockfd,                                                                   //建立连接成功返回的fd
                const InetAddress& localAddr,                                                 //本端地址
                const InetAddress& peerAddr);                                                 //对端地址
  ~TcpConnection();

  EventLoop* getLoop() const { return loop_; }                                                //返回从属的loop
  const string& name() const { return name_; }                                                //返回该连接对象的名字
  const InetAddress& localAddress() const { return localAddr_; }                              //返回本端地址
  const InetAddress& peerAddress() const { return peerAddr_; }                                //返回对端地址
  bool connected() const { return state_ == kConnected; }                                     //返回连接状态
  bool disconnected() const { return state_ == kDisconnected; }                               //返回连接状态
  // return true if success.
  bool getTcpInfo(struct tcp_info*) const;
  string getTcpInfoString() const;
                                                                                              //发送数据
  // void send(string&& message); // C++11
  void send(const void* message, int len);
  void send(const StringPiece& message);
  // void send(Buffer&& message); // C++11
  void send(Buffer* message);  // this one will swap data
  void shutdown(); // NOT thread safe, no simultaneous calling
  // void shutdownAndForceCloseAfter(double seconds); // NOT thread safe, no simultaneous calling
  void forceClose();                                                                         //强制关闭连接
  void forceCloseWithDelay(double seconds);                                                  //延后强制关闭连接
  void setTcpNoDelay(bool on);                                                               //是否开启Nagle算法
  // reading or not
  void startRead();                                                                          //更新channel监听读事件
  void stopRead();                                                                           //更新channel取消监听读事件
  bool isReading() const { return reading_; }; // NOT thread safe, may race with start/stopReadInLoop

  void setContext(const boost::any& context)
  { context_ = context; }

  const boost::any& getContext() const
  { return context_; }

  boost::any* getMutableContext()
  { return &context_; }

  void setConnectionCallback(const ConnectionCallback& cb)                                    //连接建立成功或者销毁时会执行的回调函数
  { connectionCallback_ = cb; }

  void setMessageCallback(const MessageCallback& cb)                                         //从连接中读取可用的数据后执行的回调函数
  { messageCallback_ = cb; }

  void setWriteCompleteCallback(const WriteCompleteCallback& cb)                             //写操作完成时执行的回调函数
  { writeCompleteCallback_ = cb; }

  void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark)//设置高水位
  { highWaterMarkCallback_ = cb; highWaterMark_ = highWaterMark; }

  /// Advanced interface
  Buffer* inputBuffer()                                                                     //返回读缓存
  { return &inputBuffer_; }

  Buffer* outputBuffer()                                                                    //返回写缓存
  { return &outputBuffer_; }

  /// Internal use only.
  void setCloseCallback(const CloseCallback& cb)
  { closeCallback_ = cb; }

  // called when TcpServer accepts a new connection
  void connectEstablished();   // should be called only once                               //当TcpServer接收新连接时会调用
  // called when TcpServer has removed me from its map
  void connectDestroyed();  // should be called only once                                  //当TcpServer将该对象从connectmap中移除时，会调用该函数，回调过来的。

/*****************************************************************************下面是私有成员和函数*************************************************************************/
 private:
  enum StateE { kDisconnected, kConnecting, kConnected, kDisconnecting };
  void handleRead(Timestamp receiveTime);                                                  //读回调
  void handleWrite();                                                                      //写回调
  void handleClose();                                                                      //关闭回调
  void handleError();                                                                      //错误回调
  // void sendInLoop(string&& message);
  void sendInLoop(const StringPiece& message);                                             //发送信息
  void sendInLoop(const void* message, size_t len);
  void shutdownInLoop();                                                                   //关闭写端
  // void shutdownAndForceCloseInLoop(double seconds);
  void forceCloseInLoop();                                                                 //强制关闭连接
  void setState(StateE s) { state_ = s; }                                                  //设置当前对象的状态
  const char* stateToString() const;
  void startReadInLoop();                                                                  //开始监听读事件
  void stopReadInLoop();                                                                   //停止监听读事件

  EventLoop* loop_;                                                                        //从属的loop
  const string name_;                                                                      //连接名称
  StateE state_;  // FIXME: use atomic variable
  bool reading_;                                                                           //是否处于监听读事件
  // we don't expose those classes to client.
  ///针对客户端来说，我们不能暴露这些类
  ///下面我说我fd是，accept()成功时返回的fd，就是建立连接成功时返回的fd
  std::unique_ptr<Socket> socket_;                                                         //将建立连接生成的fd封装成Socket对象
  std::unique_ptr<Channel> channel_;                                                       //通过loop和fd构造一个Channel对象
  const InetAddress localAddr_;                                                            //本端地址
  const InetAddress peerAddr_;                                                             //对端地址
  ConnectionCallback connectionCallback_;                                                  //初次建立连接成功时或者销毁时的会执行的回调函数
  MessageCallback messageCallback_;                                                        //执行完读操作后，将读缓冲区重置
  WriteCompleteCallback writeCompleteCallback_;                                            //写操作完成之后所执行的回调
  HighWaterMarkCallback highWaterMarkCallback_;
  CloseCallback closeCallback_;
  size_t highWaterMark_;
  Buffer inputBuffer_;
  Buffer outputBuffer_; // FIXME: use list<Buffer> as output buffer.
  boost::any context_;                                                                     //用来保存TcpConnection的用户上下文
  // FIXME: creationTime_, lastReceiveTime_
  //        bytesReceived_, bytesSent_
};

typedef std::shared_ptr<TcpConnection> TcpConnectionPtr;//此处用shared_ptr来管理TcpConnection对象，是因为销毁连接的时候，TcpConnection的生命周期要长于HandleEvent函数

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_TCPCONNECTION_H
