#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "window-frame.hpp"
int wmain(int argc, wchar_t** argv) {
  if (argc != 7) return 2;
  HANDLE mapping = reinterpret_cast<HANDLE>(_wcstoui64(argv[1], nullptr, 10));
  HANDLE ready = reinterpret_cast<HANDLE>(_wcstoui64(argv[2], nullptr, 10));
  HWND window = reinterpret_cast<HWND>(_wcstoui64(argv[3], nullptr, 10));
  int width = _wtoi(argv[4]), height = _wtoi(argv[5]), fps = _wtoi(argv[6]);
  if (width < 320 || width > 2560 || height < 180 || height > 1440 || (fps != 30 && fps != 60))
    return 3;
  auto frame = static_cast<WindowFrame*>(MapViewOfFile(
      mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(WindowFrame) + size_t(width) * height * 4));
  if (!frame) return 4;
  DWORD owner = 0;
  GetWindowThreadProcessId(window, &owner);
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, owner);
  if (!process) return 5;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  HDC screen = GetDC(nullptr), source = CreateCompatibleDC(screen),
      scaled = CreateCompatibleDC(screen);
  HBITMAP original = nullptr, output = nullptr;
  HGDIOBJ originalPrevious = nullptr, outputPrevious = nullptr;
  BYTE *originalPixels = nullptr, *outputPixels = nullptr;
  int oldWidth = 0, oldHeight = 0;
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  output = CreateDIBSection(screen, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&outputPixels),
                            nullptr, 0);
  if (!screen || !source || !scaled || !output || !outputPixels) return 6;
  outputPrevious = SelectObject(scaled, output);
  frame->width = width;
  frame->height = height;
  for (;;) {
    DWORD currentOwner = 0;
    GetWindowThreadProcessId(window, &currentOwner);
    if (!IsWindow(window) || IsIconic(window) || currentOwner != owner ||
        WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
      frame->error = 1;
      break;
    }
    RECT rect{};
    if (!GetWindowRect(window, &rect)) {
      frame->error = 2;
      break;
    }
    int actualWidth = rect.right - rect.left, actualHeight = rect.bottom - rect.top;
    if (actualWidth < 1 || actualHeight < 1 || actualWidth > 8192 || actualHeight > 8192 ||
        size_t(actualWidth) * actualHeight > 16777216) {
      frame->error = 3;
      break;
    }
    if (actualWidth != oldWidth || actualHeight != oldHeight) {
      if (original) {
        SelectObject(source, originalPrevious);
        DeleteObject(original);
      }
      info.bmiHeader.biWidth = actualWidth;
      info.bmiHeader.biHeight = -actualHeight;
      original = CreateDIBSection(screen, &info, DIB_RGB_COLORS,
                                  reinterpret_cast<void**>(&originalPixels), nullptr, 0);
      if (!original || !originalPixels) {
        frame->error = 4;
        break;
      }
      originalPrevious = SelectObject(source, original);
      oldWidth = actualWidth;
      oldHeight = actualHeight;
    }
    const ULONGLONG begin = GetTickCount64();
    // This synchronous API may block inside the target. The parent owns our job
    // and deadline, so it can terminate this process without terminating media.
    if (!PrintWindow(window, source, PW_RENDERFULLCONTENT)) {
      frame->error = 5;
      break;
    }
    GdiFlush();
    PatBlt(scaled, 0, 0, width, height, BLACKNESS);
    const double ratio = std::min(double(width) / actualWidth, double(height) / actualHeight);
    int targetWidth = int(actualWidth * ratio), targetHeight = int(actualHeight * ratio);
    SetStretchBltMode(scaled, COLORONCOLOR);
    if (!StretchBlt(scaled, (width - targetWidth) / 2, (height - targetHeight) / 2, targetWidth,
                    targetHeight, source, 0, 0, actualWidth, actualHeight, SRCCOPY)) {
      frame->error = 6;
      break;
    }
    GdiFlush();
    LONG state = InterlockedCompareExchange(&frame->state, 0, 0);
    if ((state == 0 || state == 1) &&
        InterlockedCompareExchange(&frame->state, 3, state) == state) {
      memcpy(frame + 1, outputPixels, size_t(width) * height * 4);
      InterlockedIncrement(&frame->sequence);
      InterlockedExchange(&frame->state, 1);
      SetEvent(ready);
    }
    const auto elapsed = GetTickCount64() - begin;
    const DWORD period = 1000 / fps;
    if (elapsed < period) Sleep(period - DWORD(elapsed));
  }
  SetEvent(ready);
  if (original) {
    SelectObject(source, originalPrevious);
    DeleteObject(original);
  }
  SelectObject(scaled, outputPrevious);
  DeleteObject(output);
  DeleteDC(source);
  DeleteDC(scaled);
  ReleaseDC(nullptr, screen);
  CloseHandle(process);
  UnmapViewOfFile(frame);
  CloseHandle(mapping);
  CloseHandle(ready);
  return 0;
}
