#ifndef MUDUO_BASE_NONCOPYABLE_H
#define MUDUO_BASE_NONCOPYABLE_H

namespace muduo
{
// ���ɸ�����
//noncopyable�Ѹ��ƹ��캯���͸��Ƹ�ֵ������
//����protected�������ζ�ų������ඨ���Լ���copy����͸�ֵ��
//��������������û�ж��������£�����ĵ������ǲ��ܹ�ͨ
//����ֵ��copy������ֶ�������һ���µ���������
class noncopyable
{
 public:
  noncopyable(const noncopyable&) = delete;  // ���ÿ�������
  void operator=(const noncopyable&) = delete;  // ���ø�ֵ����������

 protected:
  noncopyable() = default;  // ʹ��Ĭ�ϵĹ��캯��
  ~noncopyable() = default;  // ʹ��Ĭ�ϵ���������
};

}  // namespace muduo

#endif  // MUDUO_BASE_NONCOPYABLE_H

/// ����Ĭ�ϵ�������Ա����
/// ��ʼ��&����
/// 1�����캯��
/// 2����������
/// ����&��ֵ
/// 3����������
/// 4����ֵ���������
/// ȡ��ַ����
/// 5��ȡ��ַ������
/// 6��constȡ��ַ������