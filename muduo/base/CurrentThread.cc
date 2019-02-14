// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/base/CurrentThread.h>

#include <cxxabi.h>
#include <execinfo.h>
#include <stdlib.h>

namespace muduo
{
namespace CurrentThread
{
  //__thread修饰的变量是线程局部存储的
  //如果不用__thread的话，下面的变量就是全局的了
  //那么多个线程就能共享这些全局变量了，用了__thread后
  //就成了每个线程范围内的全局变量，每个线程都一有份，但不是共享的
  //__thread只能修饰POD类型(plain old data)， 与c兼容的原始数据
  //非POD类型要实现线程范围内的全局,要用线程特定数据类型tsd
__thread int t_cachedTid = 0;  //线程真实pid(tid)的缓存, 是为了减少::syscall(SYS_gettid)系统调用的次数
                               //提高获取tid的效率
__thread char t_tidString[32]; //tid的字符串表示形式
__thread int t_tidStringLength = 6;
__thread const char* t_threadName = "unknown"; //线程的名称
static_assert(std::is_same<int, pid_t>::value, "pid_t should be int");

string stackTrace(bool demangle)
{
  string stack;
  const int max_frames = 200;
  void* frame[max_frames];
  //把当前活动函数的栈帧放入frame数组，返回的是实际保存栈帧的个数
  int nptrs = ::backtrace(frame, max_frames);
  //将保存到frame数组里的栈帧转换成可执行的函数地址。
  char** strings = ::backtrace_symbols(frame, nptrs);
  if (strings)
  {
    size_t len = 256;
    char* demangled = demangle ? static_cast<char*>(::malloc(len)) : nullptr;
    for (int i = 1; i < nptrs; ++i)  // skipping the 0-th, which is this function
    {
      //把函数转换成人类可以看懂的函数,因为没转换的是编译器所能理解的函数名字
      if (demangle)
      {
        // https://panthema.net/2008/0901-stacktrace-demangled/
        // bin/exception_test(_ZN3Bar4testEv+0x79) [0x401909]
        char* left_par = nullptr;
        char* plus = nullptr;
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
