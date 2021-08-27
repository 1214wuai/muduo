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
/// TCP server, supports single-threaded and thread-pool models.��tcp��������֧�ֵ��̺߳Ͷ��߳�ģʽ��
///
/// This is an interface class, so don't expose too much details.
class TcpServer : noncopyable
{
 public:
  typedef std::function<void(EventLoop*)> ThreadInitCallback;                             //�̳߳�ʼ��������Ҳ���Բ�����
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
  void setThreadNum(int numThreads);                                                             //����I/O�̳߳����̵߳���������start����ǰ����
  void setThreadInitCallback(const ThreadInitCallback& cb)
  { threadInitCallback_ = cb; }
  /// valid after calling start()
  std::shared_ptr<EventLoopThreadPool> threadPool()
  { return threadPool_; }

  /// Starts the server if it's not listening.
  ///
  /// It's harmless to call it multiple times.
  /// Thread safe.
  void start();                                                                                  //������ڼ���״̬������������,�ú������Ա����ö��,�̰߳�ȫ�ĺ���

  /// Set connection callback.
  /// Not thread safe.
  void setConnectionCallback(const ConnectionCallback& cb)                                       //���ý������ӳɹ���Ļص�,�����̰߳�ȫ��
  { connectionCallback_ = cb; }

  /// Set message callback.
  /// Not thread safe.
  void setMessageCallback(const MessageCallback& cb)                                             //������Ϣ�ص�,�����̰߳�ȫ��
  { messageCallback_ = cb; }

  /// Set write complete callback.
  /// Not thread safe.
  void setWriteCompleteCallback(const WriteCompleteCallback& cb)                                 //����д�ص��������̰߳�ȫ��
  { writeCompleteCallback_ = cb; }

 private:
  /// Not thread safe, but in loop
  void newConnection(int sockfd, const InetAddress& peerAddr);                                   //acceptor���õĻص�
  /// Thread safe.
  void removeConnection(const TcpConnectionPtr& conn);                                           //�Ƴ��ѽ���������
  /// Not thread safe, but in loop
  void removeConnectionInLoop(const TcpConnectionPtr& conn);                                     //ʵ���ϵ��Ƴ����ӵĲ���

  typedef std::map<string, TcpConnectionPtr> ConnectionMap;                                      //ʹ��mapά����һ�������б���������:TcpConnection��share_ptr

  EventLoop* loop_;  // the acceptor loop
  const string ipPort_;                                                                          //�������˿�
  const string name_;                                                                            //��������
  std::unique_ptr<Acceptor> acceptor_; // avoid revealing Acceptor                               //�𵽼������õĶ���accept()
  std::shared_ptr<EventLoopThreadPool> threadPool_;                                              //I/O�̳߳�
  ConnectionCallback connectionCallback_;                                                        //��һ��Ĭ�ϵĺ������ڲ�����������صĵ�ַ���Զ˵ĵ�ַ������״̬
  MessageCallback messageCallback_;                                                              //ÿ�����ӵ���Ϣ����ص�
  WriteCompleteCallback writeCompleteCallback_;
  ThreadInitCallback threadInitCallback_;                                                        //�̳߳�ʼ������
  AtomicInt32 started_;                                                                          //�������ʵ������bool����ֻ������ԭ�Ӳ�����0��1֮���л�
  // always in loop thread
  int nextConnId_;                                                                               //��һ������id

  // ����ÿ������
  ConnectionMap connections_;                                                                    //�����б�
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_TCPSERVER_H
