#pragma once
#include "window-frame.hpp"
class WindowSource {
  HANDLE mapping = nullptr, ready = nullptr, job = nullptr, process = nullptr;
  WindowFrame* frame = nullptr;
  GstElement* appsrc = nullptr;
  std::thread worker;
  std::atomic<bool> running{false}, failed{false};

 public:
  ~WindowSource() { stop(); }
  bool unhealthy() const { return failed; }
  void stop() {
    running = false;
    if (ready) SetEvent(ready);
    if (job) {
      CloseHandle(job);
      job = nullptr;
    }
    if (worker.joinable()) worker.join();
    if (process) {
      WaitForSingleObject(process, 2000);
      CloseHandle(process);
      process = nullptr;
    }
    if (appsrc) {
      gst_object_unref(appsrc);
      appsrc = nullptr;
    }
    if (frame) {
      UnmapViewOfFile(frame);
      frame = nullptr;
    }
    if (mapping) {
      CloseHandle(mapping);
      mapping = nullptr;
    }
    if (ready) {
      CloseHandle(ready);
      ready = nullptr;
    }
  }
  void start(GstElement* source, HWND window, int width, int height, int fps) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    const size_t bytes = size_t(width) * height * 4;
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0,
                                 DWORD(sizeof(WindowFrame) + bytes), nullptr);
    ready = CreateEventW(&security, FALSE, FALSE, nullptr);
    job = CreateJobObjectW(nullptr, nullptr);
    if (!mapping || !ready || !job)
      throw std::runtime_error("Window capture resources unavailable");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
    limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit, sizeof(limit)))
      throw std::runtime_error("Window capture job unavailable");
    frame = static_cast<WindowFrame*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(WindowFrame) + bytes));
    if (!frame) throw std::runtime_error("Window capture mapping unavailable");
    memset(frame, 0, sizeof(WindowFrame));
    std::vector<wchar_t> path(32768);
    if (!GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()))) throw std::runtime_error("Capture executable path unavailable");
    std::wstring executable(path.data());
    executable = executable.substr(0, executable.find_last_of(L"\\/")) + L"\\window-capture.exe";
    std::wstring command = L"\"" + executable + L"\" " + std::to_wstring(uintptr_t(mapping)) +
                           L" " + std::to_wstring(uintptr_t(ready)) + L" " +
                           std::to_wstring(uintptr_t(window)) + L" " + std::to_wstring(width) +
                           L" " + std::to_wstring(height) + L" " + std::to_wstring(fps);
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<BYTE> storage(size);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size))
      throw std::runtime_error("Capture handle list unavailable");
    HANDLE inherited[] = {mapping, ready};
    const bool attributes =
        UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                  inherited, sizeof(inherited), nullptr, nullptr);
    PROCESS_INFORMATION child{};
    const bool created =
        attributes &&
        CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                       nullptr, &startup.StartupInfo, &child);
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (!created) throw std::runtime_error("Native window capture helper unavailable");
    process = child.hProcess;
    if (!AssignProcessToJobObject(job, process)) {
      TerminateProcess(process, 1);
      CloseHandle(child.hThread);
      throw std::runtime_error("Window capture supervision unavailable");
    }
    ResumeThread(child.hThread);
    CloseHandle(child.hThread);
    appsrc = GST_ELEMENT(gst_object_ref(source));
    running = true;
    worker = std::thread([this, bytes, fps] {
      ULONGLONG lastFrame = GetTickCount64();
      while (running) {
        WaitForSingleObject(ready, 100);
        if (!running) break;
        if (frame->error || WaitForSingleObject(process, 0) != WAIT_TIMEOUT ||
            GetTickCount64() - lastFrame > 3000) {
          failed = true;
          TerminateProcess(process, 1);
          break;
        }
        if (InterlockedCompareExchange(&frame->state, 2, 1) != 1) continue;
        auto buffer = gst_buffer_new_allocate(nullptr, bytes, nullptr);
        if (buffer) gst_buffer_fill(buffer, 0, frame + 1, bytes);
        InterlockedExchange(&frame->state, 0);
        if (!buffer) {
          failed = true;
          break;
        }
        GST_BUFFER_DURATION(buffer) = GST_SECOND / fps;
        if (gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer) != GST_FLOW_OK) {
          failed = true;
          break;
        }
        lastFrame = GetTickCount64();
      }
    });
  }
};
