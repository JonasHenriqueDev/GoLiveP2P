#pragma once
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>
#include <wrl/client.h>

#include <cwctype>
#include <deque>
namespace process_audio {
using Microsoft::WRL::ComPtr;
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
        wchar_t path[32768];
        DWORD length = 32768;
        FILETIME created{}, exit{}, kernel{}, user{};
        if (QueryFullProcessImageNameW(handle, 0, path, &length) &&
            GetProcessTimes(handle, &created, &exit, &kernel, &user)) {
          item.image.assign(path, length);
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
inline bool forbidden(const std::wstring& name) {
  static const std::set<std::wstring> names = {
      L"discord.exe",        L"discordptb.exe", L"discordcanary.exe", L"discorddevelopment.exe",
      L"golive p2p.exe",     L"electron.exe",   L"media-engine.exe",  L"chrome.exe",
      L"msedge.exe",         L"firefox.exe",    L"brave.exe",         L"opera.exe",
      L"msedgewebview2.exe", L"whatsapp.exe",   L"teams.exe",         L"ms-teams.exe"};
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
    if (descendant(item.pid, root, list)) {
      if (item.image.empty() || !item.created || forbidden(item.name)) return false;
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
  ~Activation() { CloseHandle(done); }
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
  HRESULT result = E_PENDING;
  HANDLE initialized = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  void run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    Activation* activation = new Activation();
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = pid;
    params.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT variant{};
    variant.vt = VT_BLOB;
    variant.blob.cbSize = sizeof(params);
    variant.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
    result = process ? ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                                   __uuidof(IAudioClient), &variant, activation,
                                                   &operation)
                     : E_ACCESSDENIED;
    if (SUCCEEDED(result)) {
      auto wait = WaitForSingleObject(activation->done, 10000);
      result = wait == WAIT_OBJECT_0 ? activation->result : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    ComPtr<IAudioCaptureClient> capture;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = 192000;
    if (SUCCEEDED(result))
      result = activation->client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                              AUDCLNT_STREAMFLAGS_LOOPBACK |
                                                  AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                                  AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                              0, 0, &format, nullptr);
    if (SUCCEEDED(result)) result = activation->client->GetService(IID_PPV_ARGS(&capture));
    if (SUCCEEDED(result)) result = activation->client->SetEventHandle(event);
    if (SUCCEEDED(result)) result = activation->client->Start();
    SetEvent(initialized);
    while (running && SUCCEEDED(result)) {
      WaitForSingleObject(event, 20);
      auto list = snapshot();
      auto identity = find(pid, list);
      if (!identity || identity->created != created || !safe(pid, list) ||
          WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
        result = E_ACCESSDENIED;
        break;
      }
      UINT32 frames = 0;
      while (SUCCEEDED(capture->GetNextPacketSize(&frames)) && frames) {
        BYTE* data = nullptr;
        DWORD flags = 0;
        result = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
        if (FAILED(result)) break;
        {
          std::lock_guard<std::mutex> lock(mutex);
          const auto samples = reinterpret_cast<int16_t*>(data);
          for (UINT32 i = 0; i < frames * 2; ++i)
            queue.push_back(flags & AUDCLNT_BUFFERFLAGS_SILENT ? 0 : samples[i]);
          while (queue.size() > 9600) queue.pop_front();
        }
        capture->ReleaseBuffer(frames);
      }
    }
    running = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      queue.clear();
    }
    if (activation->client) activation->client->Stop();
    activation->Release();
    if (process) CloseHandle(process);
    CloseHandle(event);
    CoUninitialize();
  }

 public:
  Capture(DWORD id, uint64_t identity) : pid(id), created(identity) {
    worker = std::thread([this] { run(); });
  }
  ~Capture() {
    running = false;
    if (worker.joinable()) worker.join();
    CloseHandle(initialized);
  }
  HRESULT start() {
    if (WaitForSingleObject(initialized, 12000) != WAIT_OBJECT_0)
      return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    return result;
  }
  bool active() const { return running; }
  void mix(std::vector<int32_t>& output) {
    std::lock_guard<std::mutex> lock(mutex);
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
  uint64_t lastScan = 0;
  std::set<DWORD> failed;

 public:
  void stop() {
    captures.clear();
    permitted.clear();
    windowPid = 0;
    failed.clear();
  }
  void start(const Json& settings) {
    stop();
    if (!settings.value("audio", false)) return;
    if (settings.at("source").get<std::string>().rfind("window:", 0) == 0) {
      auto hwnd = (HWND)(uintptr_t)std::stoull(settings.at("source").get<std::string>().substr(7));
      GetWindowThreadProcessId(hwnd, &windowPid);
    } else
      for (auto& image : settings.value("allowedAudioApps", Json::array()))
        permitted.insert(image.get<std::string>());
    lastScan = 0;
    refresh();
  }
  void refresh() {
    auto list = snapshot();
    std::set<DWORD> wanted;
    if (windowPid)
      wanted.insert(windowPid);
    else
      for (auto& item : sessions())
        if (item["allowed"] == true && permitted.count(item["image"].get<std::string>()))
          wanted.insert(item["pid"].get<DWORD>());
    // One capture per independent tree avoids mixing descendants twice.
    auto candidates = wanted;
    for (auto pid : candidates)
      for (auto root : candidates)
        if (pid != root && descendant(pid, root, list)) wanted.erase(pid);
    for (auto it = captures.begin(); it != captures.end();)
      if (!wanted.count(it->first) || !it->second->active())
        it = captures.erase(it);
      else
        ++it;
    for (auto pid : wanted)
      if (!captures.count(pid) && !failed.count(pid)) {
        auto item = find(pid, list);
        if (!item || !safe(pid, list)) {
          failed.insert(pid);
          emit({{"event", "warning"},
                {"peer", "local"},
                {"message", "Audio blocked: process origin is uncertain or excluded"}});
          continue;
        }
        auto capture = std::make_unique<Capture>(pid, item->created);
        auto hr = capture->start();
        if (FAILED(hr)) {
          failed.insert(pid);
          char code[32];
          sprintf_s(code, "0x%08lx", (unsigned long)hr);
          emit({{"event", "warning"},
                {"peer", "local"},
                {"message", std::string("Process audio unavailable: ") + code}});
        } else
          captures[pid] = std::move(capture);
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
