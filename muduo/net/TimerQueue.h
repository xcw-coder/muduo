// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_TIMERQUEUE_H
#define MUDUO_NET_TIMERQUEUE_H

#include <set>
#include <vector>

#include <muduo/base/Mutex.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Callbacks.h>
#include <muduo/net/Channel.h>

namespace muduo
{
namespace net
{

class EventLoop;
class Timer;
class TimerId;

///
/// A best efforts timer queue.
/// No guarantee that the callback will be on time.
///
class TimerQueue : noncopyable
{
 public:
  explicit TimerQueue(EventLoop* loop);
  ~TimerQueue();

  ///
  /// Schedules the callback to be run at given time,
  /// repeats if @c interval > 0.0.
  ///
  /// Must be thread safe. Usually be called from other threads.
  //一定是线程安全的，可以跨线程调用。通常情况下被其他线程调用
  TimerId addTimer(TimerCallback cb,
                   Timestamp when,
                   double interval);

  void cancel(TimerId timerId);

 private:

  // FIXME: use unique_ptr<Timer> instead of raw pointers.
  // This requires heterogeneous comparison lookup (N3465) from C++14
  // so that we can find an T* in a set<unique_ptr<T>>.

  //下面2个set保存的是相同的东西，只不过一个是按时间排序，另外一个是按地址排序。
  //定时器的数据结构选择的是pair而不是map，考虑的是map无法处理两个Timer到期时间相同的情况。
  //有两个解决方案：1.用multimap或multiset，而是设法区分key，采用pair，这样避免使用不常
  //的multimap class，即以pair<Timestamp, Timer*>为key，这样即便两个Timer到期时间相同
  //他们的地址也必不相同。pair先比较first,若相同，在比较second，若相同，则pair相同，否则不同
  
  typedef std::pair<Timestamp, Timer*> Entry;
  typedef std::set<Entry> TimerList;
  typedef std::pair<Timer*, int64_t> ActiveTimer;
  typedef std::set<ActiveTimer> ActiveTimerSet;

  //以下2个成员函数只可能在其所属的I/O线程中调用，因而不必加锁。
  //服务器性能杀手之一就是锁竞争，所以要尽可能少用锁
  void addTimerInLoop(Timer* timer);
  void cancelInLoop(TimerId timerId);
  // called when timerfd alarms
  void handleRead();
  // move out all expired timers
  //返回超时的定时器列表
  std::vector<Entry> getExpired(Timestamp now);
  //对这些超时的定时器重置，因为这些超时的定时器可能是'重复的定时器'，
  //如果是重复的定时器，我们需要重置
  void reset(const std::vector<Entry>& expired, Timestamp now);

  bool insert(Timer* timer);

  EventLoop* loop_;   //所属EventLoop
  const int timerfd_;
  Channel timerfdChannel_;
  // Timer list sorted by expiration
  //timers_是按到期时间排序
  TimerList timers_;

  // for cancel()
  //activeTimers_是按照对象地址排序的
  //timers_与activeTimers_是保存的相同数据
  ActiveTimerSet activeTimers_;
  bool callingExpiredTimers_; /* atomic */
  //保存的是被取消的定时器
  ActiveTimerSet cancelingTimers_;
};

}  // namespace net
}  // namespace muduo
#endif  // MUDUO_NET_TIMERQUEUE_H
