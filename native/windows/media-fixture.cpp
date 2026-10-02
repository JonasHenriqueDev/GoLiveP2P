#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <vector>
static HWND target = nullptr, cover = nullptr;
static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
  if (message == WM_PAINT) {
    PAINTSTRUCT paint;
    auto dc = BeginPaint(hwnd, &paint);
    RECT rect;
    GetClientRect(hwnd, &rect);
    auto brush = CreateSolidBrush(hwnd == target ? RGB(20, 180, 60) : RGB(210, 25, 25));
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    wchar_t text[128];
    swprintf_s(text, L"GoLive native test %llu ms / %ld x %ld", GetTickCount64(), rect.right,
               rect.bottom);
    TextOutW(dc, 30, 30, text, (int)wcslen(text));
    EndPaint(hwnd, &paint);
    return 0;
  }
  if (message == WM_DESTROY) {
    if (hwnd == target) PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, message, w, l);
}
int main(int argc, char** argv) {
  const unsigned duration = argc > 1 ? std::clamp(static_cast<unsigned>(std::strtoul(argv[1], nullptr, 10)), 40u, 300u) : 40u;
  WNDCLASSW klass{};
  klass.hInstance = GetModuleHandleW(nullptr);
  klass.lpszClassName = L"GoLiveMediaFixture";
  klass.lpfnWndProc = procedure;
  RegisterClassW(&klass);
  target =
      CreateWindowW(klass.lpszClassName, L"GoLive native validation target", WS_OVERLAPPEDWINDOW,
                    80, 80, 900, 640, nullptr, nullptr, klass.hInstance, nullptr);
  ShowWindow(target, SW_SHOWNOACTIVATE);
  printf("{\"window\":\"window:%llu\",\"pid\":%lu}\n", (unsigned long long)(uintptr_t)target,
         GetCurrentProcessId());
  fflush(stdout);
  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = 2;
  format.nSamplesPerSec = 48000;
  format.wBitsPerSample = 16;
  format.nBlockAlign = 4;
  format.nAvgBytesPerSec = 192000;
  HWAVEOUT output = nullptr;
  waveOutOpen(&output, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
  std::vector<short> samples(48000 * 2);
  for (size_t i = 0; i < samples.size() / 2; ++i) {
    auto sample = (short)(14000 * std::sin(2 * 3.141592653589793 * 440 * i / 48000));
    samples[i * 2] = samples[i * 2 + 1] = sample;
  }
  WAVEHDR header{};
  header.lpData = (LPSTR)samples.data();
  header.dwBufferLength = (DWORD)(samples.size() * 2);
  header.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP;
  header.dwLoops = duration;
  if (output) {
    waveOutPrepareHeader(output, &header, sizeof(header));
    waveOutWrite(output, &header, sizeof(header));
  }
  auto start = GetTickCount64();
  bool covered = false, resized = false;
  MSG message{};
  while (GetTickCount64() - start < duration * 1000ull) {
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    auto elapsed = GetTickCount64() - start;
    if (elapsed > 5000 && !covered) {
      covered = true;
      cover = CreateWindowW(klass.lpszClassName, L"GoLive native validation occluder",
                            WS_OVERLAPPEDWINDOW, 60, 60, 1200, 900, nullptr, nullptr,
                            klass.hInstance, nullptr);
      ShowWindow(cover, SW_SHOWNOACTIVATE);
      SetWindowPos(cover, HWND_TOPMOST, 60, 60, 1200, 900, SWP_NOACTIVATE);
      printf("covered\n");
      fflush(stdout);
    }
    if (elapsed > 14000 && !resized) {
      resized = true;
      SetWindowPos(target, nullptr, 80, 80, 650, 460, SWP_NOACTIVATE | SWP_NOZORDER);
      printf("resized\n");
      fflush(stdout);
    }
    InvalidateRect(target, nullptr, FALSE);
    Sleep(16);
  }
  if (output) {
    waveOutReset(output);
    waveOutUnprepareHeader(output, &header, sizeof(header));
    waveOutClose(output);
  }
  if (cover) DestroyWindow(cover);
  DestroyWindow(target);
  return 0;
}
