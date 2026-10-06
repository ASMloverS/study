// golden fixture 跑器（T002）：拉起 ms 子进程执行 .ms 用例，捕获 stdout/退出码与期望文件比对
// 契约见 docs/tasks/T002-test-infra.md §2：<case>.ms + 同名 .out（比对前 LF 归一化）；
// 可选 .code（期望退出码，缺省 0）、.err（期望 stderr 首行前缀）、.timeout（秒，缺省 10）
// 自测（tests/unit/test_runner_selftest.c）以 MS_TEST_RUNNER_NO_MAIN 屏蔽驱动段后包含本文件
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// stdout/stderr 捕获上限：防失控用例无限输出撑爆内存，超限截断后比对自然 FAIL
#define RUNNER_MAX_OUTPUT (1024 * 1024)

static char* slurpFile(const char* path, size_t* outLen) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    return NULL;
  }
  size_t cap = 4096;
  size_t len = 0;
  char* data = malloc(cap);
  if (data == NULL) {
    fclose(file);
    return NULL;
  }
  for (;;) {
    if (len == cap) {
      cap *= 2;
      char* grown = realloc(data, cap);
      if (grown == NULL) {
        free(data);
        fclose(file);
        return NULL;
      }
      data = grown;
    }
    size_t got = fread(data + len, 1, cap - len, file);
    len += got;
    if (got == 0) {
      break;
    }
  }
  fclose(file);
  char* shrunk = realloc(data, len + 1);
  if (shrunk == NULL) {
    free(data);
    return NULL;
  }
  shrunk[len] = '\0';
  *outLen = len;
  return shrunk;
}

// 就地 CRLF -> LF（孤立 \r 保留）；要求 NUL 结尾
// 局部名用 src/dst：独立构建时本 TU 会引入 <unistd.h>（Task 4），read/write 会与 POSIX 同名符号冲突
static void normalizeLf(char* text) {
  char* src = text;
  char* dst = text;
  while (*src != '\0') {
    if (src[0] == '\r' && src[1] == '\n') {
      src++;
    }
    *dst++ = *src++;
  }
  *dst = '\0';
}

// 首个差异行号（1 起，按 \n 切行：尾随 \n 计一空行）；相等返回 0
static int firstDiffLine(const char* expected, const char* actual) {
  const char* lhs = expected;
  const char* rhs = actual;
  int line = 1;
  for (;;) {
    const char* lhsEnd = strchr(lhs, '\n');
    const char* rhsEnd = strchr(rhs, '\n');
    size_t lhsLen = lhsEnd == NULL ? strlen(lhs) : (size_t)(lhsEnd - lhs);
    size_t rhsLen = rhsEnd == NULL ? strlen(rhs) : (size_t)(rhsEnd - rhs);
    if (lhsLen != rhsLen || memcmp(lhs, rhs, lhsLen) != 0) {
      return line;
    }
    if ((lhsEnd == NULL) != (rhsEnd == NULL)) {
      return line + 1;
    }
    if (lhsEnd == NULL) {
      return 0;
    }
    lhs = lhsEnd + 1;
    rhs = rhsEnd + 1;
    line++;
  }
}

static const char* lineAt(const char* text, int line) {
  if (*text == '\0') {
    return NULL;
  }
  int current = 1;
  for (const char* p = text; *p != '\0'; p++) {
    if (current == line) {
      return p;
    }
    if (*p == '\n') {
      current++;
    }
  }
  return current == line ? text + strlen(text) : NULL;
}

static void printDiffLine(FILE* stream, const char* label, const char* text, int line) {
  const char* p = lineAt(text, line);
  if (p == NULL) {
    fprintf(stream, "  %s: (no such line)\n", label);
    return;
  }
  const char* end = strchr(p, '\n');
  int len = end == NULL ? (int)strlen(p) : (int)(end - p);
  fprintf(stream, "  %s: %.*s\n", label, len, p);
}

// 输出首个差异行（行号 + 双方内容）
static void printDiff(FILE* stream, const char* expected, const char* actual) {
  int line = firstDiffLine(expected, actual);
  if (line == 0) {
    return;
  }
  fprintf(stream, "  first difference at line %d:\n", line);
  printDiffLine(stream, "expected", expected, line);
  printDiffLine(stream, "actual  ", actual, line);
}

// 读单整数文件（.code/.timeout）；文件缺失或无法解析返回 false，调用方保留缺省值
static bool readOptionalInt(const char* path, int* value) {
  size_t len = 0;
  char* text = slurpFile(path, &len);
  if (text == NULL) {
    return false;
  }
  char* end = NULL;
  long parsed = strtol(text, &end, 10);
  bool ok = end != text;
  free(text);
  if (ok) {
    *value = (int)parsed;
  }
  return ok;
}

#ifndef MS_TEST_RUNNER_NO_MAIN

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define RUNNER_MAX_PATH 4096

typedef struct Buffer {
  char* data;
  size_t len;
} Buffer;

static void bufAppend(Buffer* buf, const char* bytes, size_t count) {
  if (buf->data == NULL) {
    buf->data = malloc(RUNNER_MAX_OUTPUT + 1);
    if (buf->data == NULL) {
      return;
    }
    buf->data[0] = '\0';
  }
  size_t room = (size_t)RUNNER_MAX_OUTPUT - buf->len;
  size_t take = count < room ? count : room;
  memcpy(buf->data + buf->len, bytes, take);
  buf->len += take;
  buf->data[buf->len] = '\0';
}

static const char* bufText(const Buffer* buf) { return buf->data != NULL ? buf->data : ""; }

static void bufFree(Buffer* buf) {
  free(buf->data);
  buf->data = NULL;
  buf->len = 0;
}

typedef struct SpawnResult {
  bool spawned;
  bool timedOut;
  int exitCode;
  Buffer out;
  Buffer err;
} SpawnResult;

#if defined(_WIN32)

static void drainPipe(HANDLE readEnd, Buffer* buf) {
  char chunk[4096];
  for (;;) {
    DWORD available = 0;
    if (!PeekNamedPipe(readEnd, NULL, 0, NULL, &available, NULL) || available == 0) {
      return;
    }
    DWORD toRead = available < sizeof(chunk) ? available : (DWORD)sizeof(chunk);
    DWORD got = 0;
    if (!ReadFile(readEnd, chunk, toRead, &got, NULL) || got == 0) {
      return;
    }
    bufAppend(buf, chunk, got);
  }
}

static SpawnResult spawnCapture(const char* exe, const char* script, int timeoutSec) {
  SpawnResult result = {false, false, -1, {NULL, 0}, {NULL, 0}};
  SECURITY_ATTRIBUTES inheritable = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
  HANDLE outRead = NULL;
  HANDLE outWrite = NULL;
  HANDLE errRead = NULL;
  HANDLE errWrite = NULL;
  if (!CreatePipe(&outRead, &outWrite, &inheritable, 0)) {
    return result;
  }
  if (!CreatePipe(&errRead, &errWrite, &inheritable, 0)) {
    CloseHandle(outRead);
    CloseHandle(outWrite);
    return result;
  }
  SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA startup;
  PROCESS_INFORMATION process;
  ZeroMemory(&startup, sizeof(startup));
  ZeroMemory(&process, sizeof(process));
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = outWrite;
  startup.hStdError = errWrite;
  char command[RUNNER_MAX_PATH * 2];
  snprintf(command, sizeof(command), "\"%s\" \"%s\"", exe, script);
  if (!CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process)) {
    CloseHandle(outRead);
    CloseHandle(outWrite);
    CloseHandle(errRead);
    CloseHandle(errWrite);
    return result;
  }
  result.spawned = true;
  // 父进程关闭写端，子进程退出后读端才能观察到 EOF
  CloseHandle(outWrite);
  CloseHandle(errWrite);

  ULONGLONG deadline = GetTickCount64() + (ULONGLONG)timeoutSec * 1000;
  for (;;) {
    drainPipe(outRead, &result.out);
    drainPipe(errRead, &result.err);
    if (WaitForSingleObject(process.hProcess, 50) == WAIT_OBJECT_0) {
      break;
    }
    if (GetTickCount64() >= deadline) {
      TerminateProcess(process.hProcess, 1);
      WaitForSingleObject(process.hProcess, 5000);
      result.timedOut = true;
      break;
    }
  }
  drainPipe(outRead, &result.out);
  drainPipe(errRead, &result.err);
  DWORD code = 0;
  GetExitCodeProcess(process.hProcess, &code);
  result.exitCode = (int)code;
  CloseHandle(process.hProcess);
  CloseHandle(process.hThread);
  CloseHandle(outRead);
  CloseHandle(errRead);
  return result;
}

#else

static void drainFd(int fd, Buffer* buf, bool* open) {
  char chunk[4096];
  for (;;) {
    ssize_t got = read(fd, chunk, sizeof(chunk));
    if (got > 0) {
      bufAppend(buf, chunk, (size_t)got);
      continue;
    }
    if (got == 0 || errno != EAGAIN) {
      *open = false;
    }
    return;
  }
}

static long nowMs(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static SpawnResult spawnCapture(const char* exe, const char* script, int timeoutSec) {
  SpawnResult result = {false, false, -1, {NULL, 0}, {NULL, 0}};
  int outPipe[2];
  int errPipe[2];
  if (pipe(outPipe) != 0) {
    return result;
  }
  if (pipe(errPipe) != 0) {
    close(outPipe[0]);
    close(outPipe[1]);
    return result;
  }
  pid_t pid = fork();
  if (pid == 0) {
    dup2(outPipe[1], STDOUT_FILENO);
    dup2(errPipe[1], STDERR_FILENO);
    close(outPipe[0]);
    close(outPipe[1]);
    close(errPipe[0]);
    close(errPipe[1]);
    execl(exe, exe, script, (char*)NULL);
    _exit(127);
  }
  if (pid < 0) {
    close(outPipe[0]);
    close(outPipe[1]);
    close(errPipe[0]);
    close(errPipe[1]);
    return result;
  }
  close(outPipe[1]);
  close(errPipe[1]);
  fcntl(outPipe[0], F_SETFL, O_NONBLOCK);
  fcntl(errPipe[0], F_SETFL, O_NONBLOCK);

  result.spawned = true;
  long deadline = nowMs() + (long)timeoutSec * 1000L;
  bool outOpen = true;
  bool errOpen = true;
  int status = 0;
  for (;;) {
    struct pollfd fds[2] = {{outPipe[0], POLLIN, 0}, {errPipe[0], POLLIN, 0}};
    if (poll(fds, 2, 50) > 0) {
      if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
        drainFd(fds[0].fd, &result.out, &outOpen);
      }
      if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
        drainFd(fds[1].fd, &result.err, &errOpen);
      }
    }
    if (waitpid(pid, &status, WNOHANG) == pid) {
      break;
    }
    if (nowMs() >= deadline) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      result.timedOut = true;
      break;
    }
  }
  if (outOpen) {
    drainFd(outPipe[0], &result.out, &outOpen);
  }
  if (errOpen) {
    drainFd(errPipe[0], &result.err, &errOpen);
  }
  if (WIFEXITED(status)) {
    result.exitCode = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exitCode = 128 + WTERMSIG(status);
  }
  close(outPipe[0]);
  close(errPipe[0]);
  return result;
}

#endif

// 拼接 base + suffix；超长返回 false
static bool joinPath(char* out, size_t outCap, const char* base, const char* suffix) {
  size_t baseLen = strlen(base);
  size_t suffixLen = strlen(suffix);
  if (baseLen + suffixLen + 1 > outCap) {
    return false;
  }
  memcpy(out, base, baseLen);
  memcpy(out + baseLen, suffix, suffixLen + 1);
  return true;
}

static int runCase(const char* msExe, const char* casePath) {
  size_t caseLen = strlen(casePath);
  if (caseLen < 4 || caseLen + 1 > RUNNER_MAX_PATH || strcmp(casePath + caseLen - 3, ".ms") != 0) {
    printf("FAIL %s: case path must end with .ms\n", casePath);
    return 1;
  }
  char base[RUNNER_MAX_PATH];
  memcpy(base, casePath, caseLen - 3);
  base[caseLen - 3] = '\0';

  char path[RUNNER_MAX_PATH + 16];
  if (!joinPath(path, sizeof(path), base, ".out")) {
    printf("FAIL %s: case path too long\n", casePath);
    return 1;
  }
  size_t expectedLen = 0;
  char* expectedOut = slurpFile(path, &expectedLen);
  if (expectedOut == NULL) {
    printf("FAIL %s: missing expected output file %s\n", casePath, path);
    return 1;
  }
  int expectedCode = 0;
  int timeoutSec = 10;
  char* expectedErr = NULL;
  if (joinPath(path, sizeof(path), base, ".code")) {
    readOptionalInt(path, &expectedCode);
  }
  if (joinPath(path, sizeof(path), base, ".timeout")) {
    readOptionalInt(path, &timeoutSec);
  }
  if (joinPath(path, sizeof(path), base, ".err")) {
    expectedErr = slurpFile(path, &expectedLen);
  }

  SpawnResult run = spawnCapture(msExe, casePath, timeoutSec);
  int failed = 0;
  if (!run.spawned) {
    printf("FAIL %s: failed to spawn %s\n", casePath, msExe);
    failed = 1;
  } else if (run.timedOut) {
    printf("FAIL %s: timed out after %ds\n", casePath, timeoutSec);
    failed = 1;
  } else {
    normalizeLf(expectedOut);
    if (run.out.data != NULL) {
      normalizeLf(run.out.data);
    }
    if (run.err.data != NULL) {
      normalizeLf(run.err.data);
    }
    const char* actualOut = bufText(&run.out);
    if (strcmp(actualOut, expectedOut) != 0) {
      printf("FAIL %s: stdout mismatch\n", casePath);
      printDiff(stdout, expectedOut, actualOut);
      failed = 1;
    }
    if (run.exitCode != expectedCode) {
      printf("FAIL %s: exit code expected %d got %d\n", casePath, expectedCode, run.exitCode);
      failed = 1;
    }
    if (expectedErr != NULL) {
      normalizeLf(expectedErr);
      const char* actualErr = bufText(&run.err);
      const char* prefixEnd = strchr(expectedErr, '\n');
      size_t prefixLen = prefixEnd == NULL ? strlen(expectedErr) : (size_t)(prefixEnd - expectedErr);
      if (strncmp(actualErr, expectedErr, prefixLen) != 0) {
        printf("FAIL %s: stderr first line mismatch\n", casePath);
        printDiffLine(stdout, "expected", expectedErr, 1);
        printDiffLine(stdout, "actual  ", actualErr, 1);
        failed = 1;
      }
    }
  }
  if (failed == 0) {
    printf("PASS %s\n", casePath);
  }
  free(expectedOut);
  free(expectedErr);
  bufFree(&run.out);
  bufFree(&run.err);
  return failed;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: test_runner <ms-exe> <case.ms>\n");
    return 2;
  }
  return runCase(argv[1], argv[2]);
}

#endif  // MS_TEST_RUNNER_NO_MAIN
