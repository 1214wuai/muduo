// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_BUFFER_H
#define MUDUO_NET_BUFFER_H

#include "muduo/base/copyable.h"
#include "muduo/base/StringPiece.h"
#include "muduo/base/Types.h"

#include "muduo/net/Endian.h"

#include <algorithm>
#include <vector>

#include <assert.h>
#include <string.h>
//#include <unistd.h>  // ssize_t

namespace muduo
{
namespace net
{

/// A buffer class modeled after org.jboss.netty.buffer.ChannelBuffer
///
/// @code
/// +-------------------+------------------+------------------+
/// | prependable bytes |  readable bytes  |  writable bytes  |
/// |                   |     (CONTENT)    |                  |
/// +-------------------+------------------+------------------+
/// |                   |                  |                  |
/// 0      <=      readerIndex   <=   writerIndex    <=     size
/// @endcode
class Buffer : public muduo::copyable  // 缓存类
{
 public:
  static const size_t kCheapPrepend = 8;                                                      //默认预存8个字节
  static const size_t kInitialSize = 1024;                                                    //初始大小

  explicit Buffer(size_t initialSize = kInitialSize)
    : buffer_(kCheapPrepend + initialSize),                                                   //总大小为1032
      readerIndex_(kCheapPrepend),                                                            //初始指向8
      writerIndex_(kCheapPrepend)                                                             //初始指向8
  {
    assert(readableBytes() == 0);
    assert(writableBytes() == initialSize);
    assert(prependableBytes() == kCheapPrepend);
  }

  // implicit copy-ctor, move-ctor, dtor and assignment are fine
  // NOTE: implicit move-ctor is added in g++ 4.6

  void swap(Buffer& rhs)
  {
    buffer_.swap(rhs.buffer_);
    std::swap(readerIndex_, rhs.readerIndex_);
    std::swap(writerIndex_, rhs.writerIndex_);
  }

  size_t readableBytes() const                                                                // 缓存中可读字节数
  { return writerIndex_ - readerIndex_; }

  size_t writableBytes() const                                                                // 缓存中可写字节数
  { return buffer_.size() - writerIndex_; }

  size_t prependableBytes() const                                                             // 预留大小
  { return readerIndex_; }

  const char* peek() const  //  只读数据（看一眼数据），不会移动读指针                                              获取可读下标
  { return begin() + readerIndex_; }

  const char* findCRLF() const  // 搜索可读取数据中第一个 "\r\n"，并返回其地址
  {
    // FIXME: replace with memmem()?
    // 在字符串中搜索子字符串
    const char* crlf = std::search(peek(), beginWrite(), kCRLF, kCRLF+2);
    return crlf == beginWrite() ? NULL : crlf;
  }

  const char* findCRLF(const char* start) const  // 搜索可读数据自定义范围中第一个 "\r\n"，并返回其地址
  {
    assert(peek() <= start);
    assert(start <= beginWrite());
    // FIXME: replace with memmem()?  memmem与strstr类似
    const char* crlf = std::search(start, beginWrite(), kCRLF, kCRLF+2);
    return crlf == beginWrite() ? NULL : crlf;
  }

  const char* findEOL() const
  {
    const void* eol = memchr(peek(), '\n', readableBytes());
    return static_cast<const char*>(eol);
  }

  const char* findEOL(const char* start) const
  {
    assert(peek() <= start);
    assert(start <= beginWrite());
    const void* eol = memchr(start, '\n', beginWrite() - start);
    return static_cast<const char*>(eol);
  }

  // retrieve returns void, to prevent
  // string str(retrieve(readableBytes()), readableBytes());
  // the evaluation of two functions are unspecified
  void retrieve(size_t len)
  {
    assert(len <= readableBytes());
    if (len < readableBytes())  // len小于当可读数据数，说明buffer中还有需要被读取的数据
    {
      readerIndex_ += len;  // 增加读指针
    }
    else  // 表示缓存数据都已读完
    {
      retrieveAll();  // 恢复buffer到初始状态
    }
  }

  void retrieveUntil(const char* end)
  {
    assert(peek() <= end);
    assert(end <= beginWrite());
    retrieve(end - peek());
  }

  void retrieveInt64()
  {
    retrieve(sizeof(int64_t));
  }

  void retrieveInt32()
  {
    retrieve(sizeof(int32_t));
  }

  void retrieveInt16()
  {
    retrieve(sizeof(int16_t));
  }

  void retrieveInt8()
  {
    retrieve(sizeof(int8_t));
  }

  void retrieveAll()
  {
    readerIndex_ = kCheapPrepend;
    writerIndex_ = kCheapPrepend;
  }

  string retrieveAllAsString()
  {
    return retrieveAsString(readableBytes());
  }

  string retrieveAsString(size_t len)
  {
    assert(len <= readableBytes());
    string result(peek(), len);
    retrieve(len);
    return result;
  }

  StringPiece toStringPiece() const
  {
    return StringPiece(peek(), static_cast<int>(readableBytes()));
  }

  void append(const StringPiece& str)
  {
    append(str.data(), str.size());
  }

  void append(const char* /*restrict*/ data, size_t len)
  {
    ensureWritableBytes(len);                                                                     //确保缓冲区可写空间大于等于len，如果不足，需要扩充
    std::copy(data, data+len, beginWrite());  // fist -> last copy to result                      追加数据
    hasWritten(len);                                                                              //内部仅仅是写入后调整writeindex
  }

  void append(const void* /*restrict*/ data, size_t len)
  {
    append(static_cast<const char*>(data), len);
  }

  void ensureWritableBytes(size_t len)
  {
    if (writableBytes() < len)                                                                     //如果可写数据小于len
    {
      makeSpace(len);                                                                              //增加空间
    }
    assert(writableBytes() >= len);
  }

  char* beginWrite()
  { return begin() + writerIndex_; }

  const char* beginWrite() const
  { return begin() + writerIndex_; }

  void hasWritten(size_t len)
  {
    assert(len <= writableBytes());
    writerIndex_ += len;
  }

  void unwrite(size_t len)
  {
    assert(len <= readableBytes());
    writerIndex_ -= len;
  }

  ///
  /// Append int64_t using network endian
  ///
  void appendInt64(int64_t x)
  {
    int64_t be64 = sockets::hostToNetwork64(x);
    append(&be64, sizeof be64);
  }

  ///
  /// Append int32_t using network endian
  ///
  void appendInt32(int32_t x)
  {
    int32_t be32 = sockets::hostToNetwork32(x);
    append(&be32, sizeof be32);
  }

  void appendInt16(int16_t x)
  {
    int16_t be16 = sockets::hostToNetwork16(x);
    append(&be16, sizeof be16);
  }

  void appendInt8(int8_t x)
  {
    append(&x, sizeof x);
  }

  ///
  /// Read int64_t from network endian
  ///
  /// Require: buf->readableBytes() >= sizeof(int32_t)
  int64_t readInt64()
  {
    int64_t result = peekInt64();
    retrieveInt64();
    return result;
  }

  ///
  /// Read int32_t from network endian
  ///
  /// Require: buf->readableBytes() >= sizeof(int32_t)
  int32_t readInt32()
  {
    int32_t result = peekInt32();
    retrieveInt32();
    return result;
  }

  int16_t readInt16()
  {
    int16_t result = peekInt16();
    retrieveInt16();
    return result;
  }

  int8_t readInt8()
  {
    int8_t result = peekInt8();
    retrieveInt8();
    return result;
  }

  ///
  /// Peek int64_t from network endian
  ///
  /// Require: buf->readableBytes() >= sizeof(int64_t)
  int64_t peekInt64() const
  {
    assert(readableBytes() >= sizeof(int64_t));
    int64_t be64 = 0;
    ::memcpy(&be64, peek(), sizeof be64);
    return sockets::networkToHost64(be64);
  }

  ///
  /// Peek int32_t from network endian
  ///
  /// Require: buf->readableBytes() >= sizeof(int32_t)
  int32_t peekInt32() const
  {
    assert(readableBytes() >= sizeof(int32_t));
    int32_t be32 = 0;
    ::memcpy(&be32, peek(), sizeof be32);
    return sockets::networkToHost32(be32);
  }

  int16_t peekInt16() const
  {
    assert(readableBytes() >= sizeof(int16_t));
    int16_t be16 = 0;
    ::memcpy(&be16, peek(), sizeof be16);
    return sockets::networkToHost16(be16);
  }

  int8_t peekInt8() const
  {
    assert(readableBytes() >= sizeof(int8_t));
    int8_t x = *peek();
    return x;
  }

  ///
  /// Prepend int64_t using network endian
  ///
  void prependInt64(int64_t x)
  {
    int64_t be64 = sockets::hostToNetwork64(x);
    prepend(&be64, sizeof be64);
  }

  ///
  /// Prepend int32_t using network endian
  ///
  void prependInt32(int32_t x)
  {
    int32_t be32 = sockets::hostToNetwork32(x);
    prepend(&be32, sizeof be32);
  }

  void prependInt16(int16_t x)
  {
    int16_t be16 = sockets::hostToNetwork16(x);
    prepend(&be16, sizeof be16);
  }

  void prependInt8(int8_t x)
  {
    prepend(&x, sizeof x);
  }

  void prepend(const void* /*restrict*/ data, size_t len)
  {
    assert(len <= prependableBytes());
    readerIndex_ -= len;
    const char* d = static_cast<const char*>(data);
    std::copy(d, d+len, begin()+readerIndex_);
  }

  //收缩空间，保留reserver个字节，可能多次读写后buffer太大了，可以收缩，readable数据不会受到影响
  void shrink(size_t reserve)
  {
    // FIXME: use vector::shrink_to_fit() in C++ 11 if possible.

    //为什么要使用Buffer类型的other来收缩空间呢?如果不用这种方式，我们可选的有使用resize()
    Buffer other;  //生成临时对像，保存readable内容，然后和自身交换，该临时对象再析构掉
    //ensureWritableBytes()函数有两个功能，一个是空间不够resize空间，一个是空间足够内部腾挪，这里明显用的是后者。
    other.ensureWritableBytes(readableBytes()+reserve);
    other.append(toStringPiece());
    swap(other);
  }

  size_t internalCapacity() const
  {
    return buffer_.capacity();
  }

  /// Read data directly into buffer.
  ///
  /// It may implement with readv(2)
  /// @return result of read(2), @c errno is saved
  ssize_t readFd(int fd, int* savedErrno);

 private:

  char* begin()
  { return &*buffer_.begin(); }

  const char* begin() const
  { return &*buffer_.begin(); }

  void makeSpace(size_t len)  // 容量不够则扩容
  {
    if (writableBytes() + prependableBytes() < len + kCheapPrepend)             //确保空间是真的不够，而不是挪动就可以腾出空间
    {
      // FIXME: move readable data
      buffer_.resize(writerIndex_+len);
    }
    else
    {
      //内部腾挪就足够append，那么就内部腾挪一下
      // move readable data to the front, make space inside buffer
      assert(kCheapPrepend < readerIndex_);
      size_t readable = readableBytes();
      std::copy(begin()+readerIndex_,                                           //原来的可读部分全部copy到Prepend位置，相当于向前挪动，为writeable留出空间
                begin()+writerIndex_,
                begin()+kCheapPrepend);
      readerIndex_ = kCheapPrepend;                                             //更新下标
      writerIndex_ = readerIndex_ + readable;
      assert(readable == readableBytes());
    }
  }

 private:
  std::vector<char> buffer_;                                                    // 储存缓存数据，可扩容
  size_t readerIndex_;                                                          // 记录读指针，从buffer读取n字节，读指针+n
  size_t writerIndex_;                                                          // 记录写指针，向buffer写入n字节，写指针+n

  static const char kCRLF[];  // "\r\n"                                            使用柔性数组
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_BUFFER_H
