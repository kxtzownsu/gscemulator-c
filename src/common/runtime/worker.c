#include <common/runtime/worker.h>

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <common/logger.h>
#include <common/runtime/control.h>
#include <common/runtime/thread.h>

static void worker_submit(Worker *worker, WorkerOp *op, bool wait) {
  pthread_mutex_lock(&worker->lock);

  if (worker->exited) {
    bool async = op->async;

    op->done = true;
    op->result = 0;
    pthread_mutex_unlock(&worker->lock);

    if (async) free(op);
    return;
  }

  op->done = false;
  op->next = NULL;

  if (worker->tail != NULL) {
    worker->tail->next = op;
  } else {
    worker->head = op;
  }
  worker->tail = op;

  pthread_cond_signal(&worker->wake);

  while (wait && !op->done) {
    pthread_cond_wait(&worker->done, &worker->lock);
  }

  pthread_mutex_unlock(&worker->lock);
}

static void *worker_main(void *arg) {
  Worker *worker = arg;

  thread_ctx_init(THREAD_ROLE_WORKER, worker->name);
  tctx.worker = worker;

  pthread_mutex_lock(&worker->lock);

  for (;;) {
    WorkerOp *op;

    while (!worker->stop && worker->head == NULL) {
      pthread_cond_wait(&worker->wake, &worker->lock);
    }

    if (worker->head == NULL && worker->stop) {
      worker->exited = true;
      pthread_cond_broadcast(&worker->done);
      break;
    }

    op = worker->head;
    worker->head = op->next;
    if (worker->head == NULL) worker->tail = NULL;

    bool async = op->async;

    pthread_mutex_unlock(&worker->lock);

    tctx.component = op->component;
    tctx.mmio_pc = op->pc;
    tctx.mmio_pc_valid = true;

    if (op->write) {
      op->component->write(op->component, op->offset, op->size, op->value);
    } else {
      op->result = op->component->read(op->component, op->offset, op->size);
    }

    tctx.mmio_pc_valid = false;
    tctx.component = NULL;

    pthread_mutex_lock(&worker->lock);
    op->done = true;
    pthread_cond_broadcast(&worker->done);
    pthread_mutex_unlock(&worker->lock);

    if (async) free(op);

    pthread_mutex_lock(&worker->lock);
  }

  pthread_mutex_unlock(&worker->lock);
  return NULL;
}

Worker *worker_start(const char *name) {
  Worker *worker = calloc(1, sizeof(*worker));

  if (worker == NULL) return NULL;

  worker->name = name;

  if (pthread_mutex_init(&worker->lock, NULL) != 0) {
    free(worker);
    return NULL;
  }

  if (pthread_cond_init(&worker->wake, NULL) != 0) {
    pthread_mutex_destroy(&worker->lock);
    free(worker);
    return NULL;
  }

  if (pthread_cond_init(&worker->done, NULL) != 0) {
    pthread_cond_destroy(&worker->wake);
    pthread_mutex_destroy(&worker->lock);
    free(worker);
    return NULL;
  }

  if (pthread_create(&worker->thread, NULL, worker_main, worker) != 0) {
    pthread_cond_destroy(&worker->done);
    pthread_cond_destroy(&worker->wake);
    pthread_mutex_destroy(&worker->lock);
    free(worker);
    return NULL;
  }

  worker->started = true;
  return worker;
}

void worker_stop(Worker *worker) {
  if (worker == NULL) return;

  if (worker->started) {
    pthread_mutex_lock(&worker->lock);
    worker->stop = true;
    pthread_cond_broadcast(&worker->wake);
    pthread_mutex_unlock(&worker->lock);

    pthread_join(worker->thread, NULL);
    worker->started = false;
  }

  pthread_cond_destroy(&worker->done);
  pthread_cond_destroy(&worker->wake);
  pthread_mutex_destroy(&worker->lock);
  free(worker);
}

uint64_t worker_read(Worker *worker, Component *component, uint64_t offset, unsigned size,
                     uint32_t pc) {
  WorkerOp op;

  memset(&op, 0, sizeof(op));
  op.component = component;
  op.offset = offset;
  op.size = size;
  op.pc = pc;

  worker_submit(worker, &op, true);
  return op.result;
}

void worker_write(Worker *worker, Component *component, uint64_t offset, unsigned size,
                  uint64_t value, uint32_t pc, bool async) {
  WorkerOp op;

  if (!async) {
    memset(&op, 0, sizeof(op));
    op.write = true;
    op.component = component;
    op.offset = offset;
    op.size = size;
    op.value = value;
    op.pc = pc;

    worker_submit(worker, &op, true);
    return;
  }

  WorkerOp *async_op = calloc(1, sizeof(*async_op));

  if (async_op == NULL) {
    LOG_ERR("Failed to allocate worker operation\n");
    runtime_fault(RC_CHIPEMU_FAILED_INIT, "worker operation allocation failed");
    return;
  }

  async_op->write = true;
  async_op->async = true;
  async_op->component = component;
  async_op->offset = offset;
  async_op->size = size;
  async_op->value = value;
  async_op->pc = pc;

  worker_submit(worker, async_op, false);
}
