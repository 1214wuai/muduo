// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_EXCEPTION_H
#define MUDUO_BASE_EXCEPTION_H

#include "muduo/base/Types.h"
#include <exception>

/*
 * 关键字noexcept紧跟在函数的参数列表后面，用以标识该函数不会抛出异常。
实际中会自定义异常体系，继承std::exception
final修饰基类的虚函数不能被派生类重写。void*func()final
override修饰派生类虚函数强制完成重写，如果没有重写会编译报错。voidfunc()override
 */
namespace muduo
{

class Exception : public std::exception
{
 public:
  Exception(string what);
  ~Exception() noexcept override = default;

  // default copy-ctor and operator= are okay.

  const char* what() const noexcept override
  {
    return message_.c_str();
  }

  const char* stackTrace() const noexcept
  {
    return stack_.c_str();
  }

 private:
  string message_;
  string stack_;
};

}  // namespace muduo

#endif  // MUDUO_BASE_EXCEPTION_H
