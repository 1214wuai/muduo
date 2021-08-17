// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREADLOCAL_H
#define MUDUO_BASE_THREADLOCAL_H

#include "muduo/base/Mutex.h"
#include "muduo/base/noncopyable.h"

#include <pthread.h>

//多线程环境下，全部变量被所有线程所共有。
//线程私有的全局变量，仅在某个线程中有效,线程特定数据(Thread-specific Data，或TSD)
//对于POD类型，可以用__thread来解决。
//POSIX线程库通过四个函数操作线程特定数据，分别是pthread_key_create，pthread_key_delete，pthread_getspecific，pthread_setspecific

//create创建一个key，一旦一个线程创建了一个key，那么进程维护的key表的某元素（假设为key1）的标志位变成已使用。
//因为key表是进程维护的，那么所有的线程也都知道key1是已使用的，在线程自己维护的pthread表中的对应的key1指针变量就可以指向线程自己的私有数据地址

//我们可以为特定的线程指定特定的数据，可以使用set指定，get获取。
//那么这些数据就是每个线程所私有的，这样不同的线程的key就指向了不同的数据。
//delete是删除这个key，不是删除数据，删除数据要在create的时候指定一个回调函数，
//由回调函数来销毁数据，这个数据是堆上的数据就可以销毁。

/*
https://blog.csdn.net/hustraiet/article/details/9857919
线程特定数据,可以把它理解为就是一个索引和指针。
key结构中存储的是索引，pthread结构中存储的是指针，指向线程中的私有数据，通常是malloc函数返回的指针。
key结构由进程维护，pthread结构由线程维护

POSIX要求实现POSIX的系统为每个进程维护一个称之为Key的结构数组
这个数组中的每个结构称之为一个线程特定数据元素。
POSIX规定系统实现的Key结构数组必须包含不少于128个线程特定元素，
而每个线程特定数据元素至少包含两项内容：使用标志和析构函数指针。
key结构中的标志指示这个数组元素是否使用，所有的标志初始化为“不在使用”。
*/
namespace muduo
{

template<typename T>
class ThreadLocal : noncopyable
{
 public:
  ThreadLocal()
  {
    MCHECK(pthread_key_create(&pkey_, &ThreadLocal::destructor));
    //构造函数中创建key，数据的销毁由destructor来销毁，成功返回0，失败返回错误号
    /*
    同一线程的pkey_从0开始分配
    假设线程1使用了pkey_ = 0，线程2同样可以使用，指向的perThreadValue地址不会混淆
    */
  }

  ~ThreadLocal()
  {
    MCHECK(pthread_key_delete(pkey_));
    //析构函数中销毁key
  }

  //获取线程特定数据
  T& value()
  {
    T* perThreadValue = static_cast<T*>(pthread_getspecific(pkey_));//第一次调用得到的都是空指针
    if (!perThreadValue)//如果是空的，说明线程特定数据还没有创建，那么就空构造一个
    {
      T* newObj = new T();
      MCHECK(pthread_setspecific(pkey_, newObj));
      //使用pthread_setspecific()将线程特定数据的指针指向刚才分配到内存区域
      perThreadValue = newObj;
    }
    return *perThreadValue;
  }

 private:

  static void destructor(void *x)
  {
    T* obj = static_cast<T*>(x);
    typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1];
    T_must_be_complete_type dummy; (void) dummy;
    delete obj;
  }

 private:
  pthread_key_t pkey_;
};

}  // namespace muduo

#endif  // MUDUO_BASE_THREADLOCAL_H
