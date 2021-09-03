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

void muduo::net::defaultConnectionCallback(const TcpConnectionPtr& conn)                       //会在TcpServer TcpClient初始化时就设置连接回调函数
{
  LOG_TRACE << conn->localAddress().toIpPort() << " -> "
            << conn->peerAddress().toIpPort() << " is "
            << (conn->connected() ? "UP" : "DOWN");
  // do not call conn->forceClose(), because some users want to register message callback only.
}

void muduo::net::defaultMessageCallback(const TcpConnectionPtr&,                              //会在Tcp初始化时就设置消息读完之后 操作的回调函数
                                        Buffer* buf,
                                        Timestamp)
{
  buf->retrieveAll();                                                                         //将读缓冲区重置，读索引和写索引回到预置位
}

TcpConnection::TcpConnection(EventLoop* loop,
                             const string& nameArg,
                             int sockfd,
                             const InetAddress& localAddr,
                             const InetAddress& peerAddr)
  : loop_(CHECK_NOTNULL(loop)),                                                              //检查loop不为空
    name_(nameArg),                                                                          //连接名字
    state_(kConnecting),                                                                     //连接的状态
    reading_(true),                                                                          //监听读事件
    socket_(new Socket(sockfd)),                                                             //将建立连接成功返回的sockfd进行封装，生成socket_对象
    channel_(new Channel(loop, sockfd)),                                                     //生成一个channel对象
    localAddr_(localAddr),                                                                   //本端地址
    peerAddr_(peerAddr),                                                                     //对端地址
    highWaterMark_(64*1024*1024)                                                             //高水位标记
{
                                                                                             //channel设置读回调，写回调，关闭回调，错误回调
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
  socket_->setKeepAlive(true);                                                               //开启保活机制
}

TcpConnection::~TcpConnection()
{
  LOG_DEBUG << "TcpConnection::dtor[" <<  name_ << "] at " << this
            << " fd=" << channel_->fd()
            << " state=" << stateToString();
  assert(state_ == kDisconnected);                                                          //断言连接断开状态
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

void TcpConnection::send(const void* data, int len)                                        //发送数据
{
  send(StringPiece(static_cast<const char*>(data), len));
}

void TcpConnection::send(const StringPiece& message)
{
  if (state_ == kConnected)
  {
    if (loop_->isInLoopThread())
    {
      sendInLoop(message);                                                                //如果本线程是I/O线程，在本线程直接发送数据
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
      sendInLoop(buf->peek(), buf->readableBytes());                                       //readerIndex到writerIndex中间的数据
      buf->retrieveAll();                                                                  //将buf缓冲区重置
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

void TcpConnection::sendInLoop(const void* data, size_t len)                              //发送数据最后实际调用的函数，如果没有一次写完，会先放入outbuffer中，导致writeIndex后移
{
  loop_->assertInLoopThread();
  ssize_t nwrote = 0;
  size_t remaining = len;
  bool faultError = false;
  if (state_ == kDisconnected)  // 判断当前链接状态，如果断连，放弃写入
  {
    LOG_WARN << "disconnected, give up writing";
    return;
  }
  // if no thing in output queue, try writing directly
  if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)                      //如果channel_没有监听写事件，并且发送缓冲区为0，尝试直接写入
  {
    nwrote = sockets::write(channel_->fd(), data, len);                                  //如果顺利write()会返回实际写入的字节数，此处只写一次，不会阻塞
    if (nwrote >= 0)
    {
      remaining = len - nwrote;                                                          //剩下未读的字节数量
      if (remaining == 0 && writeCompleteCallback_)                                      //此时表示数据已经写完了
      {
        loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));       //写完数据后执行的回调函数，放入EventLoop的用户回调队列中，等待执行
      }
    }
    else // nwrote < 0
    {
      nwrote = 0;
      if (errno != EWOULDBLOCK)                                                          //错误不是本应该被阻塞
      {
        LOG_SYSERR << "TcpConnection::sendInLoop";
        if (errno == EPIPE || errno == ECONNRESET) // FIXME: any others?
        {
          faultError = true;                                                             //默认错误标志置为true
        }
      }
    }
  }

  assert(remaining <= len);
  if (!faultError && remaining > 0)                                                     //如果channel_监听了写事件，或者发送缓冲区有数据，或者send调用一次write还没有写完。
  {
    size_t oldLen = outputBuffer_.readableBytes();
    if (oldLen + remaining >= highWaterMark_
        && oldLen < highWaterMark_
        && highWaterMarkCallback_)                                                     //如果发送缓冲区中的可读数据+剩余的没发的数据长度就可以>=高水位(64*1024*1024)。并且高水位回调函数已注册
    {
      loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));//将高水位回调函数放入EventLoop的用户回调队列中，等待执行
    }
    outputBuffer_.append(static_cast<const char*>(data)+nwrote, remaining);           //将剩余的没有写完的数据放入发送缓冲区
    if (!channel_->isWriting())
    {
      channel_->enableWriting();                                                      //关注可写事件
      //（会一直可写，一直触发，一直调用handleWrite），直到在handleWrite()中检测到发送缓冲区没有数据了，就会取消关注可写事件
    }
  }
}

void TcpConnection::shutdown()
{
  // FIXME: use compare and swap
  if (state_ == kConnected)  // 最好使用cas操作，来保证线程安全
  {
    setState(kDisconnecting);  // 设置正在取消连接状态。设置完后，写入时候会判断改标志位，所以将不会再有数据写入
    // FIXME: shared_from_this()?
    loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));               //关闭写端
  }
}

void TcpConnection::shutdownInLoop()
{
  loop_->assertInLoopThread();
  if (!channel_->isWriting())  // 如果当前没有写事件被监听（说明outputBuffer_中的数据都已经写入系统写缓冲区），就可以优雅的关闭
  {
    // we are not writing
    socket_->shutdownWrite();  // 优雅的关闭写连接，不允许再写入数据到写缓冲区，同时会将写缓冲区中的数据都发送后，关闭连接，此时只关闭了写端
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
    loop_->queueInLoop(std::bind(&TcpConnection::forceCloseInLoop, shared_from_this()));          //将forceCloseInLoop函数放入loop的pendingFunctors_，等待I/O线程执行
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

void TcpConnection::forceCloseInLoop()                                                            //I/O线程执行该函数，强制关闭连接
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
    channel_->enableReading();                                            //关注读事件
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
    channel_->disableReading();                                          //取消关注读事件
    reading_ = false;
  }
}

void TcpConnection::connectEstablished()                                 //会执行一下connectionCallback回调
{
  loop_->assertInLoopThread();
  assert(state_ == kConnecting);                                         //初始化为这个状态
  setState(kConnected);
  channel_->tie(shared_from_this());                                     //当类A被share_ptr管理，且在类A的成员函数里需要把当前类对象作为参数传给其他函数时，就需要传递一个指向自身的share_ptr
                                                                         //返回该对象的一个share_ptr
  channel_->enableReading();                                             //让EventLoop监听channel_的读事件

  connectionCallback_(shared_from_this());
}

void TcpConnection::connectDestroyed()                                   //TcpServer的析构函数会执行这个额函数 Todo：为什么能够删除TcpConnection
{
  loop_->assertInLoopThread();
  if (state_ == kConnected)
  {
    setState(kDisconnected);
    channel_->disableAll();                                             //取消该channel对所有事件的监听，与handleClose中一样，因为TcpServer的析构函数可以直接调用connectDestroyed函数

    connectionCallback_(shared_from_this());
  }
  channel_->remove();                                                   //最终调用EpollPoller的removeChannel，从通道和Poller中移除
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
    LOG_INFO<<"To handleClose";
    handleClose();
  }
  else
  {
    errno = savedErrno;
    LOG_SYSERR << "TcpConnection::handleRead";
    handleError();
  }
}

void TcpConnection::handleWrite()                           //channel执行的写回调.在调用send函数发送数据之后，还有剩余的数据在发送缓冲区内，才会关注可写事件来触发
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
        channel_->disableWriting();                       //使其不再监听写事件，epoll水平触发模式，如果一直监听可写事件，就会一直触发，所以可写事件是用完取关
        if (writeCompleteCallback_)
        {
          loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
        }
        if (state_ == kDisconnecting)
        {
          shutdownInLoop();//关闭写端
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

void TcpConnection::handleClose()                                                                //调用forceClose()和readFd的返回值是0，就会调用该函数
{
  loop_->assertInLoopThread();
  LOG_TRACE << "fd = " << channel_->fd() << " state = " << stateToString();
  assert(state_ == kConnected || state_ == kDisconnecting);
  // we don't close fd, leave it to dtor, so we can find leaks easily.
  setState(kDisconnected);
  channel_->disableAll();                                                                      //取消对所有事件的监听

  TcpConnectionPtr guardThis(shared_from_this());                                              //C++11中的shared_from_this()来源于boost中的enable_shared_form_this类和shared_from_this()函数，功能为返回一个当前类的std::share_ptr
  connectionCallback_(guardThis);
  LOG_INFO << "Befor handleClose guardThis use_count:"<<guardThis.use_count();//引用计数为3，因为上面使用了shared_from_this
  // must be the last line
  closeCallback_(guardThis);                                                                   //这个函数会在TcpServer的newConnection被设置，最终执行connectDestroyed()函数
  LOG_INFO << "After handleClose guardThis use_count:"<<guardThis.use_count();//3
}

void TcpConnection::handleError()                                                              //并没有进一步的行动，知识在LOG中输出错误消息，这不影响连接的正常关闭
{
  int err = sockets::getSocketError(channel_->fd());
  LOG_ERROR << "TcpConnection::handleError [" << name_
            << "] - SO_ERROR = " << err << " " << strerror_tl(err);
}

