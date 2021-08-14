// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/CurrentThread.h"

#include <cxxabi.h>
#include <execinfo.h>
#include <stdlib.h>

namespace muduo
{
namespace CurrentThread
{
__thread int t_cachedTid = 0;
__thread char t_tidString[32];
__thread int t_tidStringLength = 6;
__thread const char* t_threadName = "unknown";
static_assert(std::is_same<int, pid_t>::value, "pid_t should be int");//boost::is_same作用：若int和pid_t是相同类型的，则返回是true
/*
 * __thread只能修饰POD类型POD类型（plainolddata），与C兼容的原始数据，
例如，结构和整型等C语言中的类型是POD类型（初始化只能是编译期常量），
但带有用户定义的构造函数或虚函数的类则不是非POD类型，也希望每个线程只有1份，该怎么办？
可以用线程特定数据tsd
 */

//每次调用内部函数时都转储堆栈回溯，类似于调试器打印
string stackTrace(bool demangle)
{
    //
  string stack;
  const int max_frames = 200;
  void* frame[max_frames];// 堆栈跟踪地址数据的存储数组

  int nptrs = ::backtrace(frame, max_frames);// 获取当前栈地址.
  //int backtrace (void **buffer, int size);
  // 该函数用来获取当前线程的调用堆栈，获取的信息将会被存放在buffer中，
  // 它是一个指针数组。参数size用来指定buffer中可以保存多少个void* 元素。
  // 函数返回值是实际获取的指针个数，

  char** strings = ::backtrace_symbols(frame, nptrs);    // 将地址解析为包含“文件名（函数+地址）”的字符串，
  //char **backtrace_symbols (void *const *buffer, int size);
  //该函数将从backtrace函数获取的信息转化为一个字符串数组。
  // 参数buffer是从backtrace函数获取的数组指针，
  // size是该数组中的元素个数(backtrace的返回值)，
  // 函数返回值是一个指向字符串数组的指针,它的大小同buffer相同。
  if (strings)
  {
    size_t len = 256;
    char* demangled = demangle ? static_cast<char*>(::malloc(len)) : nullptr;// 分配一个字符串，该字符串将填充调用函数名称
    //迭代返回的符号行。跳过第一个，它是该函数的地址。
    for (int i = 1; i < nptrs; ++i)  // skipping the 0-th, which is this function
    {
      if (demangle)
      {
        // https://panthema.net/2008/0901-stacktrace-demangled/
        // bin/exception_test(_ZN3Bar4testEv+0x79) [0x401909]
        char* left_par = nullptr;
        char* plus = nullptr;
        //查找圆括号和+地址偏移量
        for (char* p = strings[i]; *p; ++p)
        {
          if (*p == '(')
            left_par = p;
          else if (*p == '+')
            plus = p;
        }

        if (left_par && plus)
        {
          *plus = '\0';
          int status = 0;
          //将C++源程序标识符(original C++ source identifier)转换成C++ ABI标识符(C++ ABI identifier)的过程称为mangle；相反的过程称为demangle
          //识别C++编译以后的函数名的过程，就叫demangle
          //abi::__cxa_demangle就是用于demangling
          //使用abi::__cxa_demangle将函数名字还原出来。
          char* ret = abi::__cxa_demangle(left_par+1, demangled, &len, &status);
          *plus = '+';
          if (status == 0)
          {
            demangled = ret;  // ret could be realloc()
            stack.append(strings[i], left_par+1);
            stack.append(demangled);
            stack.append(plus);
            stack.push_back('\n');
            continue;
          }
        }
      }
      // Fallback to mangled names
      stack.append(strings[i]);
      stack.push_back('\n');
    }
    free(demangled);
    free(strings);
  }
  return stack;
}

}  // namespace CurrentThread
}  // namespace muduo
