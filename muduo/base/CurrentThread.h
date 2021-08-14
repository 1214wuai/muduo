// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_CURRENTTHREAD_H
#define MUDUO_BASE_CURRENTTHREAD_H

#include "muduo/base/Types.h"

namespace muduo
{
namespace CurrentThread
{
  // internal
  extern __thread int t_cachedTid;  // __thread修饰的变量就是线程局部变量
  extern __thread char t_tidString[32];
  extern __thread int t_tidStringLength;
  extern __thread const char* t_threadName;
  void cacheTid();

  inline int tid()
  {
      /*
       * // 两个感叹号的作用是将所有的非零值转化为1
       * #define LIKELY(x) __builtin_expect(!!(x), 1)  //x很可能为真
       * #define UNLIKELY(x) __builtin_expect(!!(x), 0)//x很可能为假
       *
       * __builtin_expect是为了生成高效的代码
       */
    if (__builtin_expect(t_cachedTid == 0, 0))//表达式t_cachedTid == 0  很可能为假。//若没缓存tid，则获取tid
    {
      // 缓存线程id到t_cachedTid和t_tidString中
      cacheTid();
    }
    return t_cachedTid;
  }

  inline const char* tidString() // for logging
  {
    return t_tidString;
  }

  inline int tidStringLength() // for logging
  {
    return t_tidStringLength;
  }

  inline const char* name()
  {
    return t_threadName;
  }

  bool isMainThread();

  void sleepUsec(int64_t usec);  // for testing

  string stackTrace(bool demangle);
}  // namespace CurrentThread
}  // namespace muduo

#endif  // MUDUO_BASE_CURRENTTHREAD_H
