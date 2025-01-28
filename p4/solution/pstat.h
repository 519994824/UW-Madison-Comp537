#ifndef PSTAT_H
#define PSTAT_H

#define NPROC 64  // 与 proc.h 中的 NPROC 一致

struct pstat {
  int inuse[NPROC];      // 该进程槽是否被使用
  int tickets[NPROC];    // 每个进程的票数
  int pid[NPROC];        // 每个进程的 PID
  int pass[NPROC];       // 每个进程的 pass 值
  int remain[NPROC];     // 每个进程的 remain 值
  int stride[NPROC];     // 每个进程的 stride 值
  int rtime[NPROC];      // 每个进程的运行总时间
};
#endif // PSTAT_H