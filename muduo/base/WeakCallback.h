// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_WEAKCALLBACK_H
#define MUDUO_BASE_WEAKCALLBACK_H

#include <functional>
#include <memory>

namespace muduo
{

// A barely usable WeakCallback

template<typename CLASS, typename... ARGS>
class WeakCallback
{
 public:

  WeakCallback(const std::weak_ptr<CLASS>& object,
               const std::function<void (CLASS*, ARGS...)>& function)
    : object_(object), function_(function)
  {
  }

  // Default dtor, copy ctor and assignment are okay

  void operator()(ARGS&&... args) const
  {
    std::shared_ptr<CLASS> ptr(object_.lock());
    if (ptr)
    {
      function_(ptr.get(), std::forward<ARGS>(args)...);
    }
    // else
    // {
    //   LOG_TRACE << "expired";
    // }
  }

 private:

  std::weak_ptr<CLASS> object_;
  std::function<void (CLASS*, ARGS...)> function_;
};

template<typename CLASS, typename... ARGS>
WeakCallback<CLASS, ARGS...> makeWeakCallback(const std::shared_ptr<CLASS>& object,
                                              void (CLASS::*function)(ARGS...))
{
  return WeakCallback<CLASS, ARGS...>(object, function);
}

template<typename CLASS, typename... ARGS>
WeakCallback<CLASS, ARGS...> makeWeakCallback(const std::shared_ptr<CLASS>& object,
                                              void (CLASS::*function)(ARGS...) const)
{
  return WeakCallback<CLASS, ARGS...>(object, function);
}

}  // namespace muduo

#endif  // MUDUO_BASE_WEAKCALLBACK_H
/*
 *
智能指针原理
    1、RAII特性：利用对象生命周期来控制程序资源
         好处：不需要显示释放资源
         对象所需的资源在其生命周期内始终保持有效
    2、重载operator*和operator->，具有指针一样的行为

 c++98 auto_ptr，管理权限转移的思想，支持拷贝，会把原来的对象指针赋空，悬空指针

 C++11 unique_ptr，防拷贝，是一个专享对象
    C++98防拷贝的方式：拷贝构造和operator=，只声明不实现+声明成私有
    C++11防拷贝的方式：C++98的方式+ =delete

 C++11 shared_ptr，通过引用计数来实现多个shared_ptr对象之间共享资源。
    shared_ptr作为函数参数,分为传值和传引用：
        传值：在函数内引用计数+1，函数结束之后恢复到原来的数目
        传引用：引用计数不变，除非在函数内部有创建

    shared_ptr有默认的删除器，但是只负责删除new出来的内容，
    如果对象不是new出来的，就需要传递删除器，例如：
    void myClose(int *fd)
    {
        close(*fd);
    }
    int main()
    {
        int socketFd = 10;//just for example
        std::shared_ptr<int> up(&socketFd,myClose);
        return 0;
    }

    c++11中的share_ptr无法直接处理数组，c++17才支持，c++17前shared_ptr未提供opreator[]
        #include <memory>
        std::shared_ptr<int[]> sp1(new int[10]()); // 错误，c++17前不能传递数组类型作为shared_ptr的模板参数
        std::unique_ptr<int[]> up1(new int[10]()); // ok, unique_ptr对此做了特化

        std::shared_ptr<int> sp2(new int[10]()); // 错误，可以编译，但会产生未定义行为，请不要这么做

    shared_ptr初始化的两种方式：
        构造函数实现：
            int *aa = new int(10)
            shared_ptr<int>p1(aa);

            shared_ptr<int> aObj1(new int(10));

         辅助函数实现：
            std::shared_ptr<int> foo = std::make_shared<int> (10);

    reset函数：会先回收以前维护的指针，然后指向新给出的指针，没给的话，重置当前管理的指针，引用计数归零。

    避免使用独立两个独立的shared_ptr来存储同一个指针

    在C++20中被移除的 unique()作为std::shared_ptr的成员函数，它检查当前shared_ptr持有的对象，是不是该对象的唯一持有者。也就是说检查shard_ptr的引用计数是否为1。大概的实现如下
    bool unique() {
      return this->use_count() == 1;
    }

 C++11 weak_ptr用来解决shared_ptr的循环引用的问题
 只可以从一个 shared_ptr 或另一个 weak_ptr 对象构造, 它的构造和析构不会引起引用记数的增加或减少.
 同时weak_ptr 没有重载*和->但可以使用 lock 获得一个可用的 shared_ptr 对象

 expired函数，表示判断被管理的对象是否被删除，相当于use_count()==0，如果被删除则返回true，否则返回false。
 lock函数如果waak_ptr管理的对象不为空，则返回一个shared_ptr，否则返回一个空的 shared_ptr;此函数锁定所拥有的指针，防止其被释放。
 swap函数，表示交换weak_ptr管理对象和swap参数里边的内容。
 eg:
 void test()
{
    shared_ptr<int> sp1,sp2;
    weak_ptr<int> wp;
    sp1 = make_shared<int> (20);    // sp1
    wp = sp1;                       // sp1, wp

    cout<<"wp count = "<<wp.use_count()<<endl; //count = 1

    sp2 = wp.lock();                // lock返回的是一个shared_ptr，赋值给sp2时引用计数加1

    cout<<"sp2 count = "<<sp2.use_count()<<endl;  //count = 2
    cout<<"sp1 count = "<<sp1.use_count()<<endl;  //count = 2
    cout<<"wp count = "<<wp.use_count()<<endl; //count = 2.前面调用了一次lock
    sp1.reset();                    // 重置当前管理的指针，引用计数归零

    cout<<"sp1 count = "<<sp1.use_count()<<endl; //count = 0
    cout<<"wp count = "<<wp.use_count()<<endl; //count = 1，之前调用了lock防止被释放

    sp1 = wp.lock(); //wp.lock放回是shared_ptr，赋值之后引用计数加1

    cout<<"sp1 count = "<<sp1.use_count()<<endl; //count = 2
    cout<<"sp2 count = "<<sp2.use_count()<<endl; //count = 2
    cout<<"wp count = "<<wp.use_count()<<endl; //count = 2
    cout<<"wp expired = "<<wp.expired()<<endl;  //输出结果：0
    wp.reset();
    cout<<"wp count = "<<wp.use_count()<<endl; //count = 0
    cout<<"wp expired = "<<wp.expired()<<endl;  //输出结果：1
}

void test2()
{
    std::shared_ptr<int> sp1 (new int(10));
    std::shared_ptr<int> sp2 (new int(20));

    std::weak_ptr<int> wp1(sp1);
    std::weak_ptr<int> wp2(sp2);

    cout<<"wp1 = "<<*wp1.lock()<<endl;  // 10
    cout<<"wp2 = "<<*wp2.lock()<<endl;  // 20

    wp1.swap(wp2);

    cout<<"wp1 = "<<*wp1.lock()<<endl;  // 20
    cout<<"wp2 = "<<*wp2.lock()<<endl;  // 10
}
 */