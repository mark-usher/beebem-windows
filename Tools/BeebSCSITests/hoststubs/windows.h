// Fibers on ucontext, for testing Scsi.cpp on Linux
#pragma once
#include <ucontext.h>
#include <stdlib.h>
#include <sys/stat.h>
typedef void *LPVOID;
#define CALLBACK
#define MAX_PATH 260
#define INVALID_FILE_ATTRIBUTES ((unsigned long)-1)
struct TestFiber { ucontext_t ctx; void (*proc)(void*); void *param; char *stack; };
static TestFiber MainTestFiber;
static TestFiber *CurrentTestFiber = nullptr;
inline bool IsThreadAFiber() { return CurrentTestFiber != nullptr; }
inline LPVOID GetCurrentFiber() { return CurrentTestFiber; }
inline LPVOID ConvertThreadToFiber(LPVOID) { CurrentTestFiber = &MainTestFiber; return &MainTestFiber; }
static void TestFiberStart(unsigned lo, unsigned hi) { TestFiber *f = (TestFiber*)(((unsigned long)hi << 32) | lo); f->proc(f->param); }
inline LPVOID CreateFiber(size_t stack, void (*proc)(void*), LPVOID param)
{
   TestFiber *f = new TestFiber(); f->proc = proc; f->param = param; f->stack = (char*)malloc(stack);
   getcontext(&f->ctx); f->ctx.uc_stack.ss_sp = f->stack; f->ctx.uc_stack.ss_size = stack; f->ctx.uc_link = nullptr;
   unsigned long p = (unsigned long)f;
   makecontext(&f->ctx, (void(*)())TestFiberStart, 2, (unsigned)(p & 0xffffffff), (unsigned)(p >> 32));
   return f;
}
inline void SwitchToFiber(LPVOID to) { TestFiber *from = CurrentTestFiber; CurrentTestFiber = (TestFiber*)to; swapcontext(&from->ctx, &((TestFiber*)to)->ctx); }
inline void DeleteFiber(LPVOID f) { free(((TestFiber*)f)->stack); delete (TestFiber*)f; }
inline unsigned long GetFileAttributes(const char *n) { struct stat s; return stat(n, &s) == 0 ? 0 : INVALID_FILE_ATTRIBUTES; }
