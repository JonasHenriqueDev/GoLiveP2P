#pragma once
#include <windows.h>
// Anonymous shared mapping, inherited only by the supervised capture child.
// 0=empty, 1=ready, 2=reader, 3=writer. Interlocked APIs synchronize processes.
struct WindowFrame {
  volatile LONG state = 0;
  volatile LONG sequence = 0;
  volatile LONG error = 0;
  LONG width = 0, height = 0;
};
