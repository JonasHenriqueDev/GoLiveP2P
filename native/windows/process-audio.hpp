#pragma once
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>
#include <wrl/client.h>

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
inline std::vector<Process> snapshot() {
  std::vector<Process> out;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return out;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snap, &entry)) do {
      Process item{entry.th32ProcessID, entry.th32ParentProcessID, entry.szExeFile, L""};
      std::transform(item.name.begin(), item.name.end(), item.name.begin(), towlower);
      HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, item.pid);
      if (handle) {
        std::vector<wchar_t> path(32768);
        DWORD length = 32768;
        FILETIME created{}, exit{}, kernel{}, user{};
        if (QueryFullProcessImageNameW(handle, 0, path.data(), &length) &&
            GetProcessTimes(handle, &created, &exit, &kernel, &user)) {
          item.image.assign(path.data(), length);
          std::transform(item.image.begin(), item.image.end(), item.image.begin(), towlower);
          item.created = (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        }
        CloseHandle(handle);
      }
      out.push_back(std::move(item));
    } while (Process32NextW(snap, &entry));
  CloseHandle(snap);
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
  std::deque<int16_t> queue;
  std::atomic<bool> running{true};
  std::atomic<HRESULT> result{E_PENDING};
  ScopedHandle initialized{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
  void run() {
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
                                              0, 0, &format, nullptr);
    if (SUCCEEDED(result)) result = client->GetService(IID_PPV_ARGS(&capture));
    if (SUCCEEDED(result)) result = client->SetEventHandle(event.get());
    if (SUCCEEDED(result)) result = client->Start();
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
      auto list = snapshot();
      auto identity = find(pid, list);
      if (!process.get() || !identity || identity->created != created || !safe(pid, list) ||
          WaitForSingleObject(process.get(), 0) != WAIT_TIMEOUT) {
        result = E_ACCESSDENIED;
        break;
      }
      result =
          drainPackets(capture.Get(), [this](const int16_t* samples, UINT32 frames, bool silent) {
            std::lock_guard<std::mutex> lock(mutex);
            for (UINT32 i = 0; i < frames * 2; ++i) queue.push_back(silent ? 0 : samples[i]);
            while (queue.size() > 9600) queue.pop_front();
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
  void mix(std::vector<int32_t>& output) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!active()) {
      queue.clear();
      return;
    }
    for (size_t i = 0; i < output.size() && !queue.empty(); ++i) {
      output[i] += queue.front();
      queue.pop_front();
    }
  }
};
class Mixer {
  std::map<DWORD, std::unique_ptr<Capture>> captures;
  std::set<std::string> permitted;
  DWORD windowPid = 0;
  HWND windowHandle = nullptr;
  uint64_t windowCreated = 0;
  uint64_t lastScan = 0;
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
  void stop() {
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
    lastScan = 0;
    refresh();
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
    for (auto it = captures.begin(); it != captures.end();)
      if (!wanted.count(it->first))
        it = captures.erase(it);
      else if (!it->second->active()) {
        retry(it->first, it->second->identity(), it->second->status());
        it = captures.erase(it);
      } else
        ++it;
    for (auto it = failed.begin(); it != failed.end();)
      if (!wanted.count(it->first))
        it = failed.erase(it);
      else
        ++it;
    for (auto pid : wanted)
      if (!captures.count(pid)) {
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
        auto capture = std::make_unique<Capture>(pid, item->created);
        auto hr = capture->start();
        if (FAILED(hr))
          retry(pid, item->created, hr);
        else {
          failed.erase(pid);
          captures[pid] = std::move(capture);
        }
      }
  }
  std::vector<int16_t> packet() {
    auto now = GetTickCount64();
    if (now - lastScan >= 500) {
      lastScan = now;
      refresh();
    }
    std::vector<int32_t> mixed(960);
    for (auto& [pid, capture] : captures) capture->mix(mixed);
    std::vector<int16_t> out(960);
    for (size_t i = 0; i < out.size(); ++i) out[i] = (int16_t)std::clamp(mixed[i], -32768, 32767);
    return out;
  }
  size_t active() const {
    return std::count_if(captures.begin(), captures.end(),
                         [](const auto& item) { return item.second->active(); });
  }
};
}  // namespace process_audio
