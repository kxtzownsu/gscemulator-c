#ifndef COMMON_RUNTIME_WORKER_H
#define COMMON_RUNTIME_WORKER_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include <common/peripherals/component.h>

typedef struct WorkerOp {
  bool write;
  bool async;
  bool done;
  Component *component;
  uint64_t offset;
  unsigned size;
  uint64_t value;
  uint64_t result;
  uint32_t pc;
  struct WorkerOp *next;
} WorkerOp;

struct Worker {
  const char *name;
  pthread_t thread;
  pthread_mutex_t lock;
  pthread_cond_t wake;
  pthread_cond_t done;
  WorkerOp *head;
  WorkerOp *tail;
  bool stop;
  bool started;
};

Worker *worker_start(const char *name);
void worker_stop(Worker *worker);
uint64_t worker_read(Worker *worker, Component *component, uint64_t offset, unsigned size,
                     uint32_t pc);
void worker_write(Worker *worker, Component *component, uint64_t offset, unsigned size,
                  uint64_t value, uint32_t pc, bool async);

#endif /* COMMON_RUNTIME_WORKER_H */
