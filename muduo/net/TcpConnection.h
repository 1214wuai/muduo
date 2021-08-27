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

//������󣬿ͻ��˺ͷ����������õ�
//����һ���ӿ��࣬���Բ��ñ�¶̫��ϸ��

class TcpConnection : noncopyable,
                      public std::enable_shared_from_this<TcpConnection>
{
 public:
  /// Constructs a TcpConnection with a connected sockfd
  ///
  /// User should not create this object.

  ///��һ���ѽ������ӳɹ���sockfd������������
  ///�û���Ӧ���Լ����졣����������accept()֮�����һ�����ӻص������������лṹ��һ���������

  ///������loop��I/O�̳߳����߳�ʱ�����Ǹ�loop��������һ���̵߳ģ����û��ʱ����������̵߳�loop
  TcpConnection(EventLoop* loop,
                const string& name,                                                           //��������
                int sockfd,                                                                   //�������ӳɹ����ص�fd
                const InetAddress& localAddr,                                                 //���˵�ַ
                const InetAddress& peerAddr);                                                 //�Զ˵�ַ
  ~TcpConnection();

  EventLoop* getLoop() const { return loop_; }                                                //���ش�����loop
  const string& name() const { return name_; }                                                //���ظ����Ӷ��������
  const InetAddress& localAddress() const { return localAddr_; }                              //���ر��˵�ַ
  const InetAddress& peerAddress() const { return peerAddr_; }                                //���ضԶ˵�ַ
  bool connected() const { return state_ == kConnected; }                                     //��������״̬
  bool disconnected() const { return state_ == kDisconnected; }                               //��������״̬
  // return true if success.
  bool getTcpInfo(struct tcp_info*) const;
  string getTcpInfoString() const;
                                                                                              //��������
  // void send(string&& message); // C++11
  void send(const void* message, int len);
  void send(const StringPiece& message);
  // void send(Buffer&& message); // C++11
  void send(Buffer* message);  // this one will swap data
  void shutdown(); // NOT thread safe, no simultaneous calling
  // void shutdownAndForceCloseAfter(double seconds); // NOT thread safe, no simultaneous calling
  void forceClose();                                                                         //ǿ�ƹر�����
  void forceCloseWithDelay(double seconds);                                                  //�Ӻ�ǿ�ƹر�����
  void setTcpNoDelay(bool on);                                                               //�Ƿ���Nagle�㷨
  // reading or not
  void startRead();                                                                          //����channel�������¼�
  void stopRead();                                                                           //����channelȡ���������¼�
  bool isReading() const { return reading_; }; // NOT thread safe, may race with start/stopReadInLoop

  void setContext(const boost::any& context)
  { context_ = context; }

  const boost::any& getContext() const
  { return context_; }

  boost::any* getMutableContext()
  { return &context_; }

  void setConnectionCallback(const ConnectionCallback& cb)                                    //���ӽ����ɹ���������ʱ��ִ�еĻص�����
  { connectionCallback_ = cb; }

  void setMessageCallback(const MessageCallback& cb)                                         //�������ж�ȡ���õ����ݺ�ִ�еĻص�����
  { messageCallback_ = cb; }

  void setWriteCompleteCallback(const WriteCompleteCallback& cb)                             //д�������ʱִ�еĻص�����
  { writeCompleteCallback_ = cb; }

  void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark)//���ø�ˮλ
  { highWaterMarkCallback_ = cb; highWaterMark_ = highWaterMark; }

  /// Advanced interface
  Buffer* inputBuffer()                                                                     //���ض�����
  { return &inputBuffer_; }

  Buffer* outputBuffer()                                                                    //����д����
  { return &outputBuffer_; }

  /// Internal use only.
  void setCloseCallback(const CloseCallback& cb)
  { closeCallback_ = cb; }

  // called when TcpServer accepts a new connection
  void connectEstablished();   // should be called only once                               //��TcpServer����������ʱ�����
  // called when TcpServer has removed me from its map
  void connectDestroyed();  // should be called only once                                  //��TcpServer���ö����connectmap���Ƴ�ʱ������øú������ص������ġ�

/*****************************************************************************������˽�г�Ա�ͺ���*************************************************************************/
 private:
  enum StateE { kDisconnected, kConnecting, kConnected, kDisconnecting };
  void handleRead(Timestamp receiveTime);                                                  //���ص�
  void handleWrite();                                                                      //д�ص�
  void handleClose();                                                                      //�رջص�
  void handleError();                                                                      //����ص�
  // void sendInLoop(string&& message);
  void sendInLoop(const StringPiece& message);                                             //������Ϣ
  void sendInLoop(const void* message, size_t len);
  void shutdownInLoop();                                                                   //�ر�д��
  // void shutdownAndForceCloseInLoop(double seconds);
  void forceCloseInLoop();                                                                 //ǿ�ƹر�����
  void setState(StateE s) { state_ = s; }                                                  //���õ�ǰ�����״̬
  const char* stateToString() const;
  void startReadInLoop();                                                                  //��ʼ�������¼�
  void stopReadInLoop();                                                                   //ֹͣ�������¼�

  EventLoop* loop_;                                                                        //������loop
  const string name_;                                                                      //��������
  StateE state_;  // FIXME: use atomic variable
  bool reading_;                                                                           //�Ƿ��ڼ������¼�
  // we don't expose those classes to client.
  ///��Կͻ�����˵�����ǲ��ܱ�¶��Щ��
  ///������˵��fd�ǣ�accept()�ɹ�ʱ���ص�fd�����ǽ������ӳɹ�ʱ���ص�fd
  std::unique_ptr<Socket> socket_;                                                         //�������������ɵ�fd��װ��Socket����
  std::unique_ptr<Channel> channel_;                                                       //ͨ��loop��fd����һ��Channel����
  const InetAddress localAddr_;                                                            //���˵�ַ
  const InetAddress peerAddr_;                                                             //�Զ˵�ַ
  ConnectionCallback connectionCallback_;                                                  //���ν������ӳɹ�ʱ��������ʱ�Ļ�ִ�еĻص�����
  MessageCallback messageCallback_;                                                        //ִ����������󣬽�������������
  WriteCompleteCallback writeCompleteCallback_;                                            //д�������֮����ִ�еĻص�
  HighWaterMarkCallback highWaterMarkCallback_;
  CloseCallback closeCallback_;
  size_t highWaterMark_;
  Buffer inputBuffer_;
  Buffer outputBuffer_; // FIXME: use list<Buffer> as output buffer.
  boost::any context_;
  // FIXME: creationTime_, lastReceiveTime_
  //        bytesReceived_, bytesSent_
};

typedef std::shared_ptr<TcpConnection> TcpConnectionPtr;

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_TCPCONNECTION_H
