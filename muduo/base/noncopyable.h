#ifndef MUDUO_BASE_NONCOPYABLE_H
#define MUDUO_BASE_NONCOPYABLE_H

namespace muduo
{
// 不可复制类
//noncopyable把复制构造函数和复制赋值函数做
//成了protected，这就意味着除非子类定义自己的copy构造和赋值函
//数，否则在子类没有定义的情况下，外面的调用者是不能够通
//过赋值和copy构造等手段来产生一个新的子类对象的
class noncopyable
{
 public:
  noncopyable(const noncopyable&) = delete;  // 禁用拷贝构造
  void operator=(const noncopyable&) = delete;  // 禁用赋值操作符重载

 protected:
  noncopyable() = default;  // 使用默认的构造函数
  ~noncopyable() = default;  // 使用默认的析构函数
};

}  // namespace muduo

#endif  // MUDUO_BASE_NONCOPYABLE_H

/// 类中默认的六个成员函数
/// 初始化&清理
/// 1）构造函数
/// 2）析构函数
/// 拷贝&赋值
/// 3）拷贝构造
/// 4）赋值运算符重载
/// 取地址重载
/// 5）取地址符重载
/// 6）const取地址符重载