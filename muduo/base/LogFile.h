// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_LOGFILE_H
#define MUDUO_BASE_LOGFILE_H

#include <muduo/base/Mutex.h>
#include <muduo/base/Types.h>

#include <memory>

namespace muduo
{

namespace FileUtil
{
class AppendFile;
}

//实现日志文件的滚动：
//比如，当文件大于1个G的时候，就新建一个文件继续写
//或者，比如每天的0点，无论文件是否满1G，都新建另外一个日志文件
class LogFile : noncopyable
{
 public:
  LogFile(const string& basename,
          off_t rollSize,
          bool threadSafe = true,
          int flushInterval = 3,
          int checkEveryN = 1024);
  ~LogFile();

  void append(const char* logline, int len);
  void flush();
  bool rollFile();

 private:
  void append_unlocked(const char* logline, int len);

  static string getLogFileName(const string& basename, time_t* now);

  //日志文件basename
  const string basename_;
  //日志文件写满rollSize就换一个新文件
  const off_t rollSize_;
  //日志写入间隔时间
  const int flushInterval_;
  const int checkEveryN_;

  int count_;

  std::unique_ptr<MutexLock> mutex_;
  //开始记入日志时间(调整至零点的时间)
  time_t startOfPeriod_;   
  //上一次滚动日志文件的时间
  time_t lastRoll_;
  //上一次日志写入文件的时间
  time_t lastFlush_;
  std::unique_ptr<FileUtil::AppendFile> file_;

  const static int kRollPerSeconds_ = 60*60*24;
};

}  // namespace muduo
#endif  // MUDUO_BASE_LOGFILE_H
