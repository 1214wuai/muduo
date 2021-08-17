// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)
/*
muduo的单例模式采用模板类实现，它内部维护一个模板参数的指针，
可以生成任何一个模板参数的单例。凭借SFINAE技术muduo库可以检测模板参数如果是类的话，
并且该类注册了一个no_destroy()方法，那么muduo库不会去自动销毁它。
否则muduo库会在init时，利用pthread_once()函数为模板参数，
注册一个atexit时的destroy()垃圾回收方法，实现自动垃圾回收。
智能指针也能达到类似的效果，
我们平时写的单例模式在Singleton中写一个Garbage类也可以完成垃圾回收。


muduo库与我们平时使用mutex取get_instance不同，
我们平时通常在get_Instance中只产生对象，在此之前需要先手动调用init()方法。
但muduo库使用了pthread_once()函数，该函数只会执行一次，且是线程安全的函数，
所以只有在我们第一次get_instance()时，才会自动调用Init()方法。此后只会获取实例。


*/

#ifndef MUDUO_BASE_SINGLETON_H
#define MUDUO_BASE_SINGLETON_H

#include "muduo/base/noncopyable.h"

#include <assert.h>
#include <pthread.h>
#include <stdlib.h> // atexit

namespace muduo
{

namespace detail
{
// This doesn't detect inherited member functions!
// http://stackoverflow.com/questions/1966362/sfinae-to-check-for-inherited-member-functions
//检测给定的类是否具有某个成员函数，此处用来检测给定的类中有没有no_destroy函数
template<typename T>
struct has_no_destroy
{
  template <typename C> static char test(decltype(&C::no_destroy));//decltype被称作类型说明符，它的作用是选择并返回操作数的数据类型。
  template <typename C> static int32_t test(...);
  const static bool value = sizeof(test<T>(0)) == 1;
};
}  // namespace detail

//懒汉模式
template<typename T>
class Singleton : noncopyable
{
 public:
  Singleton() = delete;
  ~Singleton() = delete;

  static T& instance()
  {
    //int pthread_once(pthread_once_t *once_control, void (*init_routine) (void))；
    //本函数使用初值为PTHREAD_ONCE_INIT的once_control变量保证init_routine()函数在本进程执行序列中仅执行一次。
    //并且pthread_once()能保证线程安全，效率高于mutex
    //once_control必须是一个非本地变量（即全局变量或静态变量），并且必须初始化为PTHREAD_ONCE_INIT
    pthread_once(&ponce_, &Singleton::init);
    assert(value_ != NULL);
    return *value_;
  }

 private:
  static void init()
  {
    value_ = new T();
    if (!detail::has_no_destroy<T>::value)//当参数是类且没有"no_destroy"方法才会注册atexit的destroy
    {
      //登记atexit时调用的销毁函数，防止内存泄漏
      //atexit函数的调用顺序是和登记顺序相反的。
      ::atexit(destroy);
    }
  }

  static void destroy()
  {
    //用typedef定义了一个数组类型，数组的大小不能为-1，利用这个方法，如果是不完全类型，编译阶段就会发现错误
    typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1];
    T_must_be_complete_type dummy; (void) dummy;

    delete value_;
    value_ = NULL;
  }

 private:
  static pthread_once_t ponce_;
  static T*             value_;
};

template<typename T>
pthread_once_t Singleton<T>::ponce_ = PTHREAD_ONCE_INIT;

template<typename T>
T* Singleton<T>::value_ = NULL;

}  // namespace muduo

#endif  // MUDUO_BASE_SINGLETON_H
