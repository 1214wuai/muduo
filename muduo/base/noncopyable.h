#ifndef MUDUO_BASE_NONCOPYABLE_H
#define MUDUO_BASE_NONCOPYABLE_H

namespace muduo
{
// 不可复制类
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

// 类中默认的六个成员函数
// 初始化&清理
// 1）构造函数
// 2）析构函数
// 拷贝&赋值
// 3）拷贝构造
// 4）赋值运算符重载
// 取地址重载
// 5）取地址符重载
// 6）const取地址符重载