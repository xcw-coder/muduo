// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_COUNTDOWNLATCH_H
#define MUDUO_BASE_COUNTDOWNLATCH_H

#include <muduo/base/Condition.h>
#include <muduo/base/Mutex.h>

namespace muduo
{
//CountDownLatch既可以用于所有子线程等待主线程发起“起跑”->发起起跑子线程才执行
//也可以用于主线程等待子线程初始化完毕才开始工作
class CountDownLatch : noncopyable
{
 public:

  explicit CountDownLatch(int count);

  void wait();

  void countDown();

  int getCount() const;

 private:
  //当调用const成员函数时，修改用mutable修饰的变量不会报错
  mutable MutexLock mutex_;
  Condition condition_ GUARDED_BY(mutex_);
  int count_ GUARDED_BY(mutex_);
};

}  // namespace muduo
#endif  // MUDUO_BASE_COUNTDOWNLATCH_H
