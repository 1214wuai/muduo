// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/TcpConnection.h"

#include "muduo/base/Logging.h"
#include "muduo/base/WeakCallback.h"
#include "muduo/net/Channel.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/Socket.h"
#include "muduo/net/SocketsOps.h"

#include <errno.h>

using namespace muduo;
using namespace muduo::net;

void muduo::net::defaultConnectionCallback(const TcpConnectionPtr& conn)                       //����TcpServer TcpClient��ʼ��ʱ���������ӻص�����
{
  LOG_TRACE << conn->localAddress().toIpPort() << " -> "
            << conn->peerAddress().toIpPort() << " is "
            << (conn->connected() ? "UP" : "DOWN");
  // do not call conn->forceClose(), because some users want to register message callback only.
}

void muduo::net::defaultMessageCallback(const TcpConnectionPtr&,                              //����Tcp��ʼ��ʱ��������Ϣ����֮�� �����Ļص�����
                                        Buffer* buf,
                                        Timestamp)
{
  buf->retrieveAll();                                                                         //�������������ã���������д�����ص�Ԥ��λ
}

TcpConnection::TcpConnection(EventLoop* loop,
                             const string& nameArg,
                             int sockfd,
                             const InetAddress& localAddr,
                             const InetAddress& peerAddr)
  : loop_(CHECK_NOTNULL(loop)),                                                              //���loop��Ϊ��
    name_(nameArg),                                                                          //��������
    state_(kConnecting),                                                                     //���ӵ�״̬
    reading_(true),                                                                          //�������¼�
    socket_(new Socket(sockfd)),                                                             //���������ӳɹ����ص�sockfd���з�װ������socket_����
    channel_(new Channel(loop, sockfd)),                                                     //����һ��channel����
    localAddr_(localAddr),                                                                   //���˵�ַ
    peerAddr_(peerAddr),                                                                     //�Զ˵�ַ
    highWaterMark_(64*1024*1024)                                                             //��ˮλ���
{
                                                                                             //channel���ö��ص���д�ص����رջص�������ص�
  channel_->setReadCallback(
      std::bind(&TcpConnection::handleRead, this, _1));
  channel_->setWriteCallback(
      std::bind(&TcpConnection::handleWrite, this));
  channel_->setCloseCallback(
      std::bind(&TcpConnection::handleClose, this));
  channel_->setErrorCallback(
      std::bind(&TcpConnection::handleError, this));
  LOG_DEBUG << "TcpConnection::ctor[" <<  name_ << "] at " << this
            << " fd=" << sockfd;
  socket_->setKeepAlive(true);                                                               //�����������
}

TcpConnection::~TcpConnection()
{
  LOG_DEBUG << "TcpConnection::dtor[" <<  name_ << "] at " << this
            << " fd=" << channel_->fd()
            << " state=" << stateToString();
  assert(state_ == kDisconnected);                                                          //�������ӶϿ�״̬
}

bool TcpConnection::getTcpInfo(struct tcp_info* tcpi) const
{
  return socket_->getTcpInfo(tcpi);
}

string TcpConnection::getTcpInfoString() const
{
  char buf[1024];
  buf[0] = '\0';
  socket_->getTcpInfoString(buf, sizeof buf);
  return buf;
}

void TcpConnection::send(const void* data, int len)                                        //��������
{
  send(StringPiece(static_cast<const char*>(data), len));
}

void TcpConnection::send(const StringPiece& message)
{
  if (state_ == kConnected)
  {
    if (loop_->isInLoopThread())
    {
      sendInLoop(message);                                                                //������߳���I/O�̣߳��ڱ��߳�ֱ�ӷ�������
    }
    else
    {
      void (TcpConnection::*fp)(const StringPiece& message) = &TcpConnection::sendInLoop;
      loop_->runInLoop(
          std::bind(fp,
                    this,     // FIXME
                    message.as_string()));
                    //std::forward<string>(message)));
    }
  }
}

// FIXME efficiency!!!
void TcpConnection::send(Buffer* buf)
{
  if (state_ == kConnected)
  {
    if (loop_->isInLoopThread())
    {
      sendInLoop(buf->peek(), buf->readableBytes());                                       //readerIndex��writerIndex�м������
      buf->retrieveAll();                                                                  //��buf����������
    }
    else
    {
      void (TcpConnection::*fp)(const StringPiece& message) = &TcpConnection::sendInLoop;
      loop_->runInLoop(
          std::bind(fp,
                    this,     // FIXME
                    buf->retrieveAllAsString()));
                    //std::forward<string>(message)));
    }
  }
}

void TcpConnection::sendInLoop(const StringPiece& message)
{
  sendInLoop(message.data(), message.size());
}

void TcpConnection::sendInLoop(const void* data, size_t len)                              //�����������ʵ�ʵ��õĺ���
{
  loop_->assertInLoopThread();
  ssize_t nwrote = 0;
  size_t remaining = len;
  bool faultError = false;
  if (state_ == kDisconnected)  // �жϵ�ǰ����״̬���������������д��
  {
    LOG_WARN << "disconnected, give up writing";
    return;
  }
  // if no thing in output queue, try writing directly
  if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)                      //���channel_û�м���д�¼������ҷ��ͻ�����Ϊ0������ֱ��д��
  {
    nwrote = sockets::write(channel_->fd(), data, len);                                  //���˳��write()�᷵��ʵ��д����ֽ������˴�ֻдһ�Σ���������
    if (nwrote >= 0)
    {
      remaining = len - nwrote;                                                          //ʣ��δ�����ֽ�����
      if (remaining == 0 && writeCompleteCallback_)                                      //��ʱ��ʾ�����Ѿ�д����
      {
        loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));       //д�����ݺ�ִ�еĻص�����������EventLoop���û��ص������У��ȴ�ִ��
      }
    }
    else // nwrote < 0
    {
      nwrote = 0;
      if (errno != EWOULDBLOCK)                                                          //�����Ǳ�Ӧ�ñ�����
      {
        LOG_SYSERR << "TcpConnection::sendInLoop";
        if (errno == EPIPE || errno == ECONNRESET) // FIXME: any others?
        {
          faultError = true;                                                             //Ĭ�ϴ����־��Ϊtrue
        }
      }
    }
  }

  assert(remaining <= len);
  if (!faultError && remaining > 0)
  {
    size_t oldLen = outputBuffer_.readableBytes();
    if (oldLen + remaining >= highWaterMark_
        && oldLen < highWaterMark_
        && highWaterMarkCallback_)                                                     //������ͻ������еĿɶ�����+ʣ���û�������ݳ��ȾͿ���>=��ˮλ�����Ҹ�ˮλ�ص�������ע��
    {
      loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));//����ˮλ�ص���������EventLoop���û��ص������У��ȴ�ִ��
    }
    outputBuffer_.append(static_cast<const char*>(data)+nwrote, remaining);           //��ʣ���û��д������ݷ��뷢�ͻ�����
    if (!channel_->isWriting())
    {
      channel_->enableWriting();                                                      //������д�¼�
    }
  }
}

void TcpConnection::shutdown()
{
  // FIXME: use compare and swap
  if (state_ == kConnected)  // ���ʹ��cas����������֤�̰߳�ȫ
  {
    setState(kDisconnecting);  // ��������ȡ������״̬���������д��ʱ����жϸı�־λ�����Խ�������������д��
    // FIXME: shared_from_this()?
    loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));               //�ر�д��
  }
}

void TcpConnection::shutdownInLoop()
{
  loop_->assertInLoopThread();
  if (!channel_->isWriting())  // �����ǰû��д�¼���������˵��outputBuffer_�е����ݶ��Ѿ�д��ϵͳд�����������Ϳ������ŵĹر�
  {
    // we are not writing
    socket_->shutdownWrite();  // ���ŵĹر�д���ӣ���������д�����ݵ�д��������ͬʱ�Ὣд�������е����ݶ����ͺ󣬹ر����ӣ���ʱֻ�ر���д��
  }
}

// void TcpConnection::shutdownAndForceCloseAfter(double seconds)
// {
//   // FIXME: use compare and swap
//   if (state_ == kConnected)
//   {
//     setState(kDisconnecting);
//     loop_->runInLoop(std::bind(&TcpConnection::shutdownAndForceCloseInLoop, this, seconds));
//   }
// }

// void TcpConnection::shutdownAndForceCloseInLoop(double seconds)
// {
//   loop_->assertInLoopThread();
//   if (!channel_->isWriting())
//   {
//     // we are not writing
//     socket_->shutdownWrite();
//   }
//   loop_->runAfter(
//       seconds,
//       makeWeakCallback(shared_from_this(),
//                        &TcpConnection::forceCloseInLoop));
// }

void TcpConnection::forceClose()
{
  // FIXME: use compare and swap
  if (state_ == kConnected || state_ == kDisconnecting)
  {
    setState(kDisconnecting);
    loop_->queueInLoop(std::bind(&TcpConnection::forceCloseInLoop, shared_from_this()));          //��forceCloseInLoop��������loop��pendingFunctors_���ȴ�I/O�߳�ִ��
  }
}

void TcpConnection::forceCloseWithDelay(double seconds)
{
  if (state_ == kConnected || state_ == kDisconnecting)
  {
    setState(kDisconnecting);
    loop_->runAfter(
        seconds,
        makeWeakCallback(shared_from_this(),
                         &TcpConnection::forceClose));  // not forceCloseInLoop to avoid race condition
  }
}

void TcpConnection::forceCloseInLoop()                                                            //I/O�߳�ִ�иú�����ǿ�ƹر�����
{
  loop_->assertInLoopThread();
  if (state_ == kConnected || state_ == kDisconnecting)
  {
    // as if we received 0 byte in handleRead();
    handleClose();
  }
}

const char* TcpConnection::stateToString() const
{
  switch (state_)
  {
    case kDisconnected:
      return "kDisconnected";
    case kConnecting:
      return "kConnecting";
    case kConnected:
      return "kConnected";
    case kDisconnecting:
      return "kDisconnecting";
    default:
      return "unknown state";
  }
}

void TcpConnection::setTcpNoDelay(bool on)
{
  socket_->setTcpNoDelay(on);
}

void TcpConnection::startRead()
{
  loop_->runInLoop(std::bind(&TcpConnection::startReadInLoop, this));
}

void TcpConnection::startReadInLoop()
{
  loop_->assertInLoopThread();
  if (!reading_ || !channel_->isReading())
  {
    channel_->enableReading();                                            //��ע���¼�
    reading_ = true;
  }
}

void TcpConnection::stopRead()
{
  loop_->runInLoop(std::bind(&TcpConnection::stopReadInLoop, this));
}

void TcpConnection::stopReadInLoop()
{
  loop_->assertInLoopThread();
  if (reading_ || channel_->isReading())
  {
    channel_->disableReading();                                         //ȡ����ע���¼�
    reading_ = false;
  }
}

void TcpConnection::connectEstablished()
{
  loop_->assertInLoopThread();
  assert(state_ == kConnecting);                                         //��ʼ��Ϊ���״̬
  setState(kConnected);
  channel_->tie(shared_from_this());
  channel_->enableReading();                                             //��EventLoop����channel_�Ķ��¼�

  connectionCallback_(shared_from_this());
}

void TcpConnection::connectDestroyed()                            //TcpServer������������ִ��������
{
  loop_->assertInLoopThread();
  if (state_ == kConnected)
  {
    setState(kDisconnected);
    channel_->disableAll();                                                  //ȡ����channel�������¼��ļ���

    connectionCallback_(shared_from_this());
  }
  channel_->remove();                                                        //���յ���EpollPoller��removeChannel
}

void TcpConnection::handleRead(Timestamp receiveTime)
{
  loop_->assertInLoopThread();
  int savedErrno = 0;
  ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
  if (n > 0)
  {
    messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
  }
  else if (n == 0)
  {
    handleClose();
  }
  else
  {
    errno = savedErrno;
    LOG_SYSERR << "TcpConnection::handleRead";
    handleError();
  }
}

void TcpConnection::handleWrite()                           //channelִ�е�д�ص�.�ڵ���send������������֮�󣬻���ʣ��������ڷ��ͻ������ڣ��Ż��ע��д�¼�������
{
  loop_->assertInLoopThread();
  if (channel_->isWriting())
  {
    ssize_t n = sockets::write(channel_->fd(),
                               outputBuffer_.peek(),
                               outputBuffer_.readableBytes());
    if (n > 0)
    {
      outputBuffer_.retrieve(n);
      if (outputBuffer_.readableBytes() == 0)
      {
        channel_->disableWriting();                       //ʹ�䲻�ټ���д�¼���epollˮƽ����ģʽ�����һֱ������д�¼����ͻ�һֱ���������Կ�д�¼�������ȡ��
        if (writeCompleteCallback_)
        {
          loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
        }
        if (state_ == kDisconnecting)
        {
          shutdownInLoop();//�ر�д��
        }
      }
    }
    else
    {
      LOG_SYSERR << "TcpConnection::handleWrite";
      // if (state_ == kDisconnecting)
      // {
      //   shutdownInLoop();
      // }
    }
  }
  else
  {
    LOG_TRACE << "Connection fd = " << channel_->fd()
              << " is down, no more writing";
  }
}

void TcpConnection::handleClose()                                                                //����forceClose()��readFd�ķ���ֵ��0���ͻ���øú���
{
  loop_->assertInLoopThread();
  LOG_TRACE << "fd = " << channel_->fd() << " state = " << stateToString();
  assert(state_ == kConnected || state_ == kDisconnecting);
  // we don't close fd, leave it to dtor, so we can find leaks easily.
  setState(kDisconnected);
  channel_->disableAll();                                                                      //ȡ���������¼��ļ���

  TcpConnectionPtr guardThis(shared_from_this());                                              //C++11�е�shared_from_this()��Դ��boost�е�enable_shared_form_this���shared_from_this()����������Ϊ����һ����ǰ���std::share_ptr
  connectionCallback_(guardThis);
  // must be the last line
  closeCallback_(guardThis);                                                                   //�����������TcpServer��newConnection�����ã�����ִ��connectDestroyed()����
}

void TcpConnection::handleError()
{
  int err = sockets::getSocketError(channel_->fd());
  LOG_ERROR << "TcpConnection::handleError [" << name_
            << "] - SO_ERROR = " << err << " " << strerror_tl(err);
}

