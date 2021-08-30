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
����ָ��ԭ��
    1��RAII���ԣ����ö����������������Ƴ�����Դ
         �ô�������Ҫ��ʾ�ͷ���Դ
         �����������Դ��������������ʼ�ձ�����Ч
    2������operator*��operator->������ָ��һ������Ϊ

 c++98 auto_ptr������Ȩ��ת�Ƶ�˼�룬֧�ֿ��������ԭ���Ķ���ָ�븳�գ�����ָ��

 C++11 unique_ptr������������һ��ר�����
    C++98�������ķ�ʽ�����������operator=��ֻ������ʵ��+������˽��
    C++11�������ķ�ʽ��C++98�ķ�ʽ+ =delete

 C++11 shared_ptr��ͨ�����ü�����ʵ�ֶ��shared_ptr����֮�乲����Դ��
    shared_ptr��Ϊ��������,��Ϊ��ֵ�ʹ����ã�
        ��ֵ���ں��������ü���+1����������֮��ָ���ԭ������Ŀ
        �����ã����ü������䣬�����ں����ڲ��д���

    shared_ptr��Ĭ�ϵ�ɾ����������ֻ����ɾ��new���������ݣ�
    ���������new�����ģ�����Ҫ����ɾ���������磺
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

    c++11�е�share_ptr�޷�ֱ�Ӵ������飬c++17��֧�֣�c++17ǰshared_ptrδ�ṩopreator[]
        #include <memory>
        std::shared_ptr<int[]> sp1(new int[10]()); // ����c++17ǰ���ܴ�������������Ϊshared_ptr��ģ�����
        std::unique_ptr<int[]> up1(new int[10]()); // ok, unique_ptr�Դ������ػ�

        std::shared_ptr<int> sp2(new int[10]()); // ���󣬿��Ա��룬�������δ������Ϊ���벻Ҫ��ô��

    shared_ptr��ʼ�������ַ�ʽ��
        ���캯��ʵ�֣�
            int *aa = new int(10)
            shared_ptr<int>p1(aa);

            shared_ptr<int> aObj1(new int(10));

         ��������ʵ�֣�
            std::shared_ptr<int> foo = std::make_shared<int> (10);

    reset���������Ȼ�����ǰά����ָ�룬Ȼ��ָ���¸�����ָ�룬û���Ļ������õ�ǰ�����ָ�룬���ü������㡣

    ����ʹ�ö�������������shared_ptr���洢ͬһ��ָ��

    ��C++20�б��Ƴ��� unique()��Ϊstd::shared_ptr�ĳ�Ա����������鵱ǰshared_ptr���еĶ����ǲ��Ǹö����Ψһ�����ߡ�Ҳ����˵���shard_ptr�����ü����Ƿ�Ϊ1����ŵ�ʵ������
    bool unique() {
      return this->use_count() == 1;
    }

 C++11 weak_ptr�������shared_ptr��ѭ�����õ�����
 ֻ���Դ�һ�� shared_ptr ����һ�� weak_ptr ������, ���Ĺ�������������������ü��������ӻ����.
 ͬʱweak_ptr û������*��->������ʹ�� lock ���һ�����õ� shared_ptr ����

 expired��������ʾ�жϱ�����Ķ����Ƿ�ɾ�����൱��use_count()==0�������ɾ���򷵻�true�����򷵻�false��
 lock�������waak_ptr����Ķ���Ϊ�գ��򷵻�һ��shared_ptr�����򷵻�һ���յ� shared_ptr;�˺���������ӵ�е�ָ�룬��ֹ�䱻�ͷš�
 swap��������ʾ����weak_ptr��������swap������ߵ����ݡ�
 eg:
 void test()
{
    shared_ptr<int> sp1,sp2;
    weak_ptr<int> wp;
    sp1 = make_shared<int> (20);    // sp1
    wp = sp1;                       // sp1, wp

    cout<<"wp count = "<<wp.use_count()<<endl; //count = 1

    sp2 = wp.lock();                // lock���ص���һ��shared_ptr����ֵ��sp2ʱ���ü�����1

    cout<<"sp2 count = "<<sp2.use_count()<<endl;  //count = 2
    cout<<"sp1 count = "<<sp1.use_count()<<endl;  //count = 2
    cout<<"wp count = "<<wp.use_count()<<endl; //count = 2.ǰ�������һ��lock
    sp1.reset();                    // ���õ�ǰ�����ָ�룬���ü�������

    cout<<"sp1 count = "<<sp1.use_count()<<endl; //count = 0
    cout<<"wp count = "<<wp.use_count()<<endl; //count = 1��֮ǰ������lock��ֹ���ͷ�

    sp1 = wp.lock(); //wp.lock�Ż���shared_ptr����ֵ֮�����ü�����1

    cout<<"sp1 count = "<<sp1.use_count()<<endl; //count = 2
    cout<<"sp2 count = "<<sp2.use_count()<<endl; //count = 2
    cout<<"wp count = "<<wp.use_count()<<endl; //count = 2
    cout<<"wp expired = "<<wp.expired()<<endl;  //��������0
    wp.reset();
    cout<<"wp count = "<<wp.use_count()<<endl; //count = 0
    cout<<"wp expired = "<<wp.expired()<<endl;  //��������1
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