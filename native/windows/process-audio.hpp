#pragma once
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>
#include <wrl/client.h>

#include "audio-fifo.hpp"
#include <avrt.h>
#include <condition_variable>
#include <cwctype>
#include <deque>
#include <functional>
namespace process_audio {
using Microsoft::WRL::ComPtr;
class ScopedHandle {
  HANDLE handle = nullptr;

 public:
  explicit ScopedHandle(HANDLE value) : handle(value) {}
  ~ScopedHandle() {
    if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
  }
  ScopedHandle(const ScopedHandle&) = delete;
  ScopedHandle& operator=(const ScopedHandle&) = delete;
  HANDLE get() const { return handle; }
};
// MMCSS is optional: scheduling still works if the system denies enrollment.
class AudioPriority {
  HMODULE module = LoadLibraryExW(L"avrt.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  HANDLE task = nullptr;
 public:
  AudioPriority() {
    if (!module) return;
    auto enroll = reinterpret_cast<decltype(&AvSetMmThreadCharacteristicsW)>(GetProcAddress(module, "AvSetMmThreadCharacteristicsW"));
    DWORD index = 0;
    if (enroll) task = enroll(L"Audio", &index);
  }
  ~AudioPriority() {
    if (task) {
      auto revert = reinterpret_cast<decltype(&AvRevertMmThreadCharacteristics)>(GetProcAddress(module, "AvRevertMmThreadCharacteristics"));
      if (revert) revert(task);
    }
    if (module) FreeLibrary(module);
  }
};
struct RetrySchedule {
  uint64_t identity = 0, due = 0;
  unsigned failures = 0;
  bool ready(uint64_t created, uint64_t now) const { return identity != created || now >= due; }
  void fail(uint64_t created, uint64_t now) {
    if (identity != created) {
      identity = created;
      failures = 0;
    }
    due = now + std::min<uint64_t>(1000ull << std::min(failures, 5u), 30000);
    failures = std::min(failures + 1, 5u);
  }
};
// Preserve every WASAPI error. A live thread is not evidence of a live audio source.
inline HRESULT drainPackets(IAudioCaptureClient* capture,
                            const std::function<void(const int16_t*, UINT32, bool)>& consume) {
  while (true) {
    UINT32 frames = 0;
    HRESULT result = capture->GetNextPacketSize(&frames);
    if (FAILED(result) || !frames) return result;
    BYTE* data = nullptr;
    DWORD flags = 0;
    result = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
    if (FAILED(result)) return result;
    if (result == AUDCLNT_S_BUFFER_EMPTY || !frames) return S_OK;
    const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
    if (!data && !silent) {
      capture->ReleaseBuffer(frames);
      return E_POINTER;
    }
    try {
      consume(reinterpret_cast<const int16_t*>(data), frames, silent);
    } catch (...) {
      capture->ReleaseBuffer(frames);
      throw;
    }
    result = capture->ReleaseBuffer(frames);
    if (FAILED(result)) return result;
  }
}
struct Process {
  DWORD pid, parent;
  std::wstring name, image;
  uint64_t created = 0;
};
inline std::vector<Process> snapshot(DWORD root = 0) {
  std::vector<Process> out;
  ScopedHandle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
  if (snap.get() == INVALID_HANDLE_VALUE) return out;
  PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
  if (Process32FirstW(snap.get(), &entry)) do {
    Process item{entry.th32ProcessID, entry.th32ParentProcessID, entry.szExeFile, L""};
    std::transform(item.name.begin(), item.name.end(), item.name.begin(), towlower);
    out.push_back(std::move(item));
  } while (Process32NextW(snap.get(), &entry));
  std::set<DWORD> required;
  auto lookup = [&](DWORD pid) -> const Process* {
    const auto it = std::find_if(out.begin(), out.end(), [pid](const auto& item) { return item.pid == pid; });
    return it == out.end() ? nullptr : &*it;
  };
  if (root) {
    // Enumerate EVERY process relationship. Query costly identities only for
    // the selected tree, its ancestors, and our engine's own ancestry.
    // Unknown identities in that relevant graph still fail safe().
    for (const auto& item : out) {
      auto pid = item.pid;
      for (size_t i = 0; i < out.size(); ++i) {
        if (pid == root) { required.insert(item.pid); break; }
        const auto parent = lookup(pid);
        if (!parent || !parent->parent || parent->parent == pid) break;
        pid = parent->parent;
      }
    }
    for (DWORD origin : {root, GetCurrentProcessId()}) {
      auto pid = origin;
      for (size_t i = 0; i < out.size(); ++i) {
        required.insert(pid);
        const auto item = lookup(pid);
        if (!item || !item->parent || item->parent == pid) break;
        pid = item->parent;
      }
    }
  }
  for (auto& item : out) {
    if (root && !required.count(item.pid)) continue;
    ScopedHandle handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, item.pid));
    if (handle.get()) {
      std::vector<wchar_t> path(32768); DWORD length = 32768;
      FILETIME created{}, exited{}, kernel{}, user{};
      if (QueryFullProcessImageNameW(handle.get(), 0, path.data(), &length) &&
          GetProcessTimes(handle.get(), &created, &exited, &kernel, &user)) {
        item.image.assign(path.data(), length);
        std::transform(item.image.begin(), item.image.end(), item.image.begin(), towlower);
        item.created = (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
      }
    }
  }
  return out;
}
inline const Process* find(DWORD pid, const std::vector<Process>& list) {
  auto it = std::find_if(list.begin(), list.end(), [&](auto& item) { return item.pid == pid; });
  return it == list.end() ? nullptr : &*it;
}
inline bool identityMatches(DWORD pid, uint64_t created, const std::vector<Process>& list) {
  auto process = find(pid, list);
  return created && process && process->created == created;
}
inline bool descendant(DWORD pid, DWORD root, const std::vector<Process>& list) {
  for (size_t i = 0; i < list.size(); ++i) {
    if (pid == root) return true;
    auto item = find(pid, list);
    if (!item || !item->parent || item->parent == pid) return false;
    auto parent = find(item->parent, list);
    if (!parent || !parent->created || parent->created > item->created) return false;
    pid = item->parent;
  }
  return false;
}
// A missing identity must not hide a possible child from the safety check.
inline bool potentialDescendant(DWORD pid, DWORD root, const std::vector<Process>& list) {
  for (size_t i = 0; i < list.size(); ++i) {
    if (pid == root) return true;
    const auto item = find(pid, list);
    if (!item || !item->parent || item->parent == pid) return false;
    pid = item->parent;
  }
  return false;
}
inline bool forbidden(const std::wstring& name) {
  static const std::set<std::wstring> names = {
      L"discord.exe",        L"discordptb.exe", L"discordcanary.exe", L"discorddevelopment.exe",
      L"golive p2p.exe",     L"electron.exe",   L"media-engine.exe",  L"svchost.exe",
      L"audiodg.exe",        L"system"};
  return names.count(name) != 0;
}
inline bool safe(DWORD root, const std::vector<Process>& list) {
  auto target = find(root, list);
  if (!target || target->image.empty() || !target->created) return false;
  // Exclude the engine, the UI and all their descendants/ancestors.
  if (descendant(root, GetCurrentProcessId(), list) ||
      descendant(GetCurrentProcessId(), root, list))
    return false;
  for (const auto& item : list)
    if (potentialDescendant(item.pid, root, list)) {
      if (!descendant(item.pid, root, list) || item.image.empty() || !item.created || forbidden(item.name)) return false;
    }
  auto current = target;
  for (size_t i = 0; current && i < list.size(); ++i) {
    if (forbidden(current->name)) return false;
    if (!current->parent || current->parent == current->pid) break;
    auto parent = find(current->parent, list);
    if (parent && parent->created > current->created) break;
    current = parent;
  }
  return true;
}
inline Json sessions() {
  Json out = Json::array();
  ComPtr<IMMDeviceEnumerator> enumerator;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(&enumerator))))
    return out;
  ComPtr<IMMDeviceCollection> endpoints;
  if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &endpoints))) return out;
  UINT count = 0;
  endpoints->GetCount(&count);
  auto list = snapshot();
  std::set<DWORD> seen;
  for (UINT i = 0; i < count; ++i) {
    ComPtr<IMMDevice> endpoint;
    if (FAILED(endpoints->Item(i, &endpoint))) continue;
    ComPtr<IAudioSessionManager2> manager;
    if (FAILED(endpoint->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &manager)))
      continue;
    ComPtr<IAudioSessionEnumerator> sessions;
    if (FAILED(manager->GetSessionEnumerator(&sessions))) continue;
    int n = 0;
    sessions->GetCount(&n);
    for (int j = 0; j < n; ++j) {
      ComPtr<IAudioSessionControl> control;
      ComPtr<IAudioSessionControl2> session;
      DWORD pid = 0;
      if (FAILED(sessions->GetSession(j, &control)) || FAILED(control.As(&session)) ||
          FAILED(session->GetProcessId(&pid)) || !pid || !seen.insert(pid).second)
        continue;
      auto item = find(pid, list);
      bool allowed = safe(pid, list);
      out.push_back({{"pid", pid},
                     {"name", item ? utf8(item->name) : "unknown"},
                     {"image", item ? utf8(item->image) : ""},
                     {"allowed", allowed},
                     {"reason", allowed ? "" : "Shared, excluded or unidentified process tree"}});
    }
  }
  return out;
}
class Activation final : public IActivateAudioInterfaceCompletionHandler {
  std::atomic<ULONG> references{1};

 public:
  HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  HRESULT result = E_FAIL;
  ComPtr<IAudioClient> client;
  ~Activation() {
    if (done) CloseHandle(done);
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (id == __uuidof(IUnknown) || id == __uuidof(IAgileObject) ||
        id == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
      *object = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
  ULONG STDMETHODCALLTYPE Release() override {
    auto left = --references;
    if (!left) delete this;
    return left;
  }
  HRESULT STDMETHODCALLTYPE
  ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override {
    ComPtr<IUnknown> unknown;
    HRESULT activation = E_FAIL;
    result = operation->GetActivateResult(&activation, &unknown);
    if (SUCCEEDED(result)) result = activation;
    if (SUCCEEDED(result) && unknown) result = unknown.As(&client);
    SetEvent(done);
    return S_OK;
  }
};
class Capture {
  DWORD pid;
  uint64_t created;
  std::thread worker;
  std::mutex mutex;
  AudioFifo queue;
  uint64_t capturedFrames = 0, capturePackets = 0, startedAt = 0, maximumScanUs = 0;
  UINT32 endpointFrames = 0;
  std::atomic<bool> running{true};
  std::atomic<HRESULT> result{E_PENDING};
  ScopedHandle initialized{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
  void run() {
    AudioPriority priority;
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(apartment)) {
      result = apartment;
      running = false;
      SetEvent(initialized.get());
      return;
    }
    struct ApartmentCleanup {
      ~ApartmentCleanup() { CoUninitialize(); }
    } apartmentCleanup;
    ScopedHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
    ComPtr<Activation> activation;
    activation.Attach(new Activation());
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    ComPtr<IAudioClient> client;
    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = pid;
    params.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT variant{};
    variant.vt = VT_BLOB;
    variant.blob.cbSize = sizeof(params);
    variant.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
    result = process.get() && activation->done
                 ? ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                               __uuidof(IAudioClient), &variant, activation.Get(),
                                               &operation)
                 : E_ACCESSDENIED;
    if (SUCCEEDED(result)) {
      auto wait = WaitForSingleObject(activation->done, 10000);
      result = wait == WAIT_OBJECT_0 ? activation->result : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
      if (wait == WAIT_OBJECT_0 && SUCCEEDED(result)) client = activation->client;
    }
    if (SUCCEEDED(result) && !client) result = E_POINTER;
    ComPtr<IAudioCaptureClient> capture;
    ScopedHandle event(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!event.get()) result = HRESULT_FROM_WIN32(GetLastError());
    struct ClientCleanup {
      IAudioClient* client;
      ~ClientCleanup() {
        if (client) client->Stop();
      }
    } clientCleanup{client.Get()};
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = 192000;
    if (SUCCEEDED(result))
      result = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                              AUDCLNT_STREAMFLAGS_LOOPBACK |
                                                  AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                                  AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                              2000000, 0, &format, nullptr);
    if (SUCCEEDED(result)) {
      UINT32 frames = 0;
      // Process loopback implementations can report unusable endpoint sizes.
      if (SUCCEEDED(client->GetBufferSize(&frames)) && frames <= 48000) endpointFrames = frames;
    }
    if (SUCCEEDED(result)) result = client->GetService(IID_PPV_ARGS(&capture));
    if (SUCCEEDED(result)) result = client->SetEventHandle(event.get());
    if (SUCCEEDED(result)) result = client->Start();
    startedAt = GetTickCount64();
    SetEvent(initialized.get());
    while (running && SUCCEEDED(result)) {
      if (!event.get()) {
        result = E_HANDLE;
        break;
      }
      auto waited = WaitForSingleObject(event.get(), 20);
      if (waited == WAIT_FAILED) {
        result = HRESULT_FROM_WIN32(GetLastError());
        break;
      }
      const auto scanStart = std::chrono::steady_clock::now();
      auto list = snapshot(pid);
      const auto scanTime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - scanStart).count());
      { std::lock_guard<std::mutex> lock(mutex); maximumScanUs = std::max(maximumScanUs, scanTime); }
      auto identity = find(pid, list);
      if (!process.get() || !identity || identity->created != created || !safe(pid, list) ||
          WaitForSingleObject(process.get(), 0) != WAIT_TIMEOUT) {
        result = E_ACCESSDENIED;
        break;
      }
      result =
          drainPackets(capture.Get(), [this](const int16_t* samples, UINT32 frames, bool silent) {
            std::lock_guard<std::mutex> lock(mutex);
            capturedFrames += frames; ++capturePackets;
            queue.push(samples, frames, silent);
          });
    }
    running = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      queue.clear();
    }
  }

 public:
  Capture(DWORD id, uint64_t identity) : pid(id), created(identity) {
    if (!initialized.get()) throw std::runtime_error("Audio initialization event unavailable");
    worker = std::thread([this] {
      try {
        run();
      } catch (...) {
        result = E_FAIL;
        running = false;
        {
          std::lock_guard<std::mutex> lock(mutex);
          queue.clear();
        }
        SetEvent(initialized.get());
      }
    });
  }
  ~Capture() {
    running = false;
    if (worker.joinable()) worker.join();
  }
  HRESULT start() {
    if (WaitForSingleObject(initialized.get(), 12000) != WAIT_OBJECT_0)
      return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    return result;
  }
  bool active() const { return running && SUCCEEDED(result.load()); }
  HRESULT status() const { return result; }
  uint64_t identity() const { return created; }
  DWORD processId() const { return pid; }
  Json metrics() {
    std::lock_guard<std::mutex> lock(mutex);
    return {{"bufferFrames", queue.frames()}, {"underruns", queue.underruns}, {"trimmedFrames", queue.trimmedFrames}, {"capturedFrames", capturedFrames}, {"capturePackets", capturePackets}, {"elapsedMs", GetTickCount64() - startedAt}, {"maxPolicyScanUs", maximumScanUs}, {"endpointBufferFrames", endpointFrames}};
  }
  void mix(std::vector<int32_t>& output) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!active()) {
      queue.clear();
      return;
    }
    queue.mix(output);
  }
};
class Mixer {
  std::map<DWORD, std::shared_ptr<Capture>> captures;
  std::mutex captureMutex, waitMutex;
  std::condition_variable wake;
  std::thread manager;
  std::atomic<bool> managing{false};
  std::vector<std::shared_ptr<Capture>> current() {
    std::lock_guard<std::mutex> lock(captureMutex);
    std::vector<std::shared_ptr<Capture>> out;
    for (auto& item : captures) out.push_back(item.second);
    return out;
  }
  std::set<std::string> permitted;
  DWORD windowPid = 0;
  HWND windowHandle = nullptr;
  uint64_t windowCreated = 0;
  std::map<DWORD, RetrySchedule> failed;

  void retry(DWORD pid, uint64_t created, HRESULT result) {
    failed[pid].fail(created, GetTickCount64());
    char code[32];
    sprintf_s(code, "0x%08lx", (unsigned long)result);
    emit({{"event", "warning"},
          {"peer", "local"},
          {"message",
           std::string("Process audio unavailable; retrying after safety checks: ") + code}});
  }

 public:
  ~Mixer() { stop(); }
  void stop() {
    managing = false;
    wake.notify_all();
    if (manager.joinable()) manager.join();
    captures.clear();
    permitted.clear();
    windowPid = 0;
    windowHandle = nullptr;
    windowCreated = 0;
    failed.clear();
  }
  void start(const Json& settings, const Process* selectedOwner = nullptr) {
    stop();
    if (!settings.value("audio", false)) return;
    if (settings.at("source").get<std::string>().rfind("window:", 0) == 0) {
      windowHandle =
          (HWND)(uintptr_t)std::stoull(settings.at("source").get<std::string>().substr(7));
      if (selectedOwner) {
        windowPid = selectedOwner->pid;
        windowCreated = selectedOwner->created;
      } else {
        GetWindowThreadProcessId(windowHandle, &windowPid);
        auto list = snapshot();
        auto owner = find(windowPid, list);
        windowCreated = owner ? owner->created : 0;
      }
    } else
      for (auto& image : settings.value("allowedAudioApps", Json::array()))
        permitted.insert(image.get<std::string>());
    managing = true;
    manager = std::thread([this] {
      const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
      if (FAILED(apartment)) { managing = false; return; }
      while (managing) {
        try { refresh(); }
        catch (...) {
          std::map<DWORD, std::shared_ptr<Capture>> old;
          { std::lock_guard<std::mutex> lock(captureMutex); old.swap(captures); }
          emit({{"event", "warning"}, {"peer", "local"}, {"message", "Audio source scan failed; audio sources blocked"}});
        }
        std::unique_lock<std::mutex> lock(waitMutex);
        wake.wait_for(lock, std::chrono::milliseconds(500), [this] { return !managing; });
      }
      CoUninitialize();
    });
  }
  void refresh() {
    auto list = snapshot();
    std::set<DWORD> wanted;
    if (windowHandle) {
      DWORD currentPid = 0;
      GetWindowThreadProcessId(windowHandle, &currentPid);
      if (currentPid == windowPid && identityMatches(windowPid, windowCreated, list))
        wanted.insert(windowPid);
    } else
      for (auto& item : sessions())
        if (item["allowed"] == true && permitted.count(item["image"].get<std::string>()))
          wanted.insert(item["pid"].get<DWORD>());
    // One capture per independent tree avoids mixing descendants twice.
    auto candidates = wanted;
    for (auto pid : candidates)
      for (auto root : candidates)
        if (pid != root && descendant(pid, root, list)) wanted.erase(pid);
    std::vector<std::shared_ptr<Capture>> retired;
    {
      std::lock_guard<std::mutex> lock(captureMutex);
      for (auto it = captures.begin(); it != captures.end();) {
        if (!wanted.count(it->first) || !it->second->active()) {
          retired.push_back(it->second);
          it = captures.erase(it);
        } else ++it;
      }
    }
    for (auto& capture : retired)
      if (!capture->active()) retry(capture->processId(), capture->identity(), capture->status());
    retired.clear(); // WASAPI shutdown does not hold the packet mixer lock.
    for (auto it = failed.begin(); it != failed.end();)
      if (!wanted.count(it->first))
        it = failed.erase(it);
      else
        ++it;
    for (auto pid : wanted) {
      bool present = false;
      { std::lock_guard<std::mutex> lock(captureMutex); present = captures.count(pid) != 0; }
      if (!present && managing) {
        auto item = find(pid, list);
        const auto created = item ? item->created : 0;
        if (!failed[pid].ready(created, GetTickCount64())) continue;
        if (!item || !safe(pid, list)) {
          failed[pid].fail(created, GetTickCount64());
          emit({{"event", "warning"},
                {"peer", "local"},
                {"message", "Audio blocked: process origin is uncertain or excluded"}});
          continue;
        }
        auto capture = std::make_shared<Capture>(pid, item->created);
        auto hr = capture->start();
        if (FAILED(hr))
          retry(pid, item->created, hr);
        else {
          failed.erase(pid);
          std::lock_guard<std::mutex> lock(captureMutex);
          captures[pid] = std::move(capture);
        }
      }
    }
  }
  Json metrics() {
    Json out = Json::array();
    for (auto& capture : current()) out.push_back(capture->metrics());
    return out;
  }
  std::vector<int16_t> packet() {
    if (windowHandle) {
      DWORD owner = 0;
      GetWindowThreadProcessId(windowHandle, &owner);
      if (owner != windowPid) return std::vector<int16_t>(960);
    }
    std::vector<int32_t> mixed(960);
    for (auto& capture : current()) capture->mix(mixed);
    std::vector<int16_t> out(960);
    for (size_t i = 0; i < out.size(); ++i) out[i] = (int16_t)std::clamp(mixed[i], -32768, 32767);
    return out;
  }
  size_t active() {
    auto list = current();
    return std::count_if(list.begin(), list.end(), [](const auto& capture) { return capture->active(); });
  }
};
}  // namespace process_audio
