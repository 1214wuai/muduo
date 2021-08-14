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
static_assert(std::is_same<int, pid_t>::value, "pid_t should be int");//boost::is_same���ã���int��pid_t����ͬ���͵ģ��򷵻���true
/*
 * __threadֻ������POD����POD���ͣ�plainolddata������C���ݵ�ԭʼ���ݣ�
���磬�ṹ�����͵�C�����е�������POD���ͣ���ʼ��ֻ���Ǳ����ڳ�������
�������û�����Ĺ��캯�����麯���������Ƿ�POD���ͣ�Ҳϣ��ÿ���߳�ֻ��1�ݣ�����ô�죿
�������߳��ض�����tsd
 */

//ÿ�ε����ڲ�����ʱ��ת����ջ���ݣ������ڵ�������ӡ
string stackTrace(bool demangle)
{
    //
  string stack;
  const int max_frames = 200;
  void* frame[max_frames];// ��ջ���ٵ�ַ���ݵĴ洢����

  int nptrs = ::backtrace(frame, max_frames);// ��ȡ��ǰջ��ַ.
  //int backtrace (void **buffer, int size);
  // �ú���������ȡ��ǰ�̵߳ĵ��ö�ջ����ȡ����Ϣ���ᱻ�����buffer�У�
  // ����һ��ָ�����顣����size����ָ��buffer�п��Ա�����ٸ�void* Ԫ�ء�
  // ��������ֵ��ʵ�ʻ�ȡ��ָ�������

  char** strings = ::backtrace_symbols(frame, nptrs);    // ����ַ����Ϊ�������ļ���������+��ַ�������ַ�����
  //char **backtrace_symbols (void *const *buffer, int size);
  //�ú�������backtrace������ȡ����Ϣת��Ϊһ���ַ������顣
  // ����buffer�Ǵ�backtrace������ȡ������ָ�룬
  // size�Ǹ������е�Ԫ�ظ���(backtrace�ķ���ֵ)��
  // ��������ֵ��һ��ָ���ַ��������ָ��,���Ĵ�Сͬbuffer��ͬ��
  if (strings)
  {
    size_t len = 256;
    char* demangled = demangle ? static_cast<char*>(::malloc(len)) : nullptr;// ����һ���ַ��������ַ����������ú�������
    //�������صķ����С�������һ�������Ǹú����ĵ�ַ��
    for (int i = 1; i < nptrs; ++i)  // skipping the 0-th, which is this function
    {
      if (demangle)
      {
        // https://panthema.net/2008/0901-stacktrace-demangled/
        // bin/exception_test(_ZN3Bar4testEv+0x79) [0x401909]
        char* left_par = nullptr;
        char* plus = nullptr;
        //����Բ���ź�+��ַƫ����
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
          //��C++Դ�����ʶ��(original C++ source identifier)ת����C++ ABI��ʶ��(C++ ABI identifier)�Ĺ��̳�Ϊmangle���෴�Ĺ��̳�Ϊdemangle
          //ʶ��C++�����Ժ�ĺ������Ĺ��̣��ͽ�demangle
          //abi::__cxa_demangle��������demangling
          //ʹ��abi::__cxa_demangle���������ֻ�ԭ������
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
