// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_EXCEPTION_H
#define MUDUO_BASE_EXCEPTION_H

#include "muduo/base/Types.h"
#include <exception>

/*
 * �ؼ���noexcept�����ں����Ĳ����б���棬���Ա�ʶ�ú��������׳��쳣��
ʵ���л��Զ����쳣��ϵ���̳�std::exception
final���λ�����麯�����ܱ���������д��void*func()final
override�����������麯��ǿ�������д�����û����д����뱨��voidfunc()override
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
