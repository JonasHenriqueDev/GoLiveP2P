// Process-loopback PCM capture for Windows build 20348+. stdout: 48 kHz stereo s16le.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>
#include <propidl.h>
#include <atomic>
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>
#include <cstdio>

struct ProcessInfo { DWORD pid; DWORD parent; std::wstring name; };
static std::vector<ProcessInfo> processes() {
  std::vector<ProcessInfo> found;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return found;
  PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) do {
    std::wstring name(entry.szExeFile);
    std::transform(name.begin(), name.end(), name.begin(), towlower);
    found.push_back({entry.th32ProcessID, entry.th32ParentProcessID, name});
  } while (Process32NextW(snapshot, &entry));
  CloseHandle(snapshot);
  return found;
}
static bool isDiscord(const std::wstring& name) {
  return name == L"discord.exe" || name == L"discordptb.exe" ||
         name == L"discordcanary.exe" || name == L"discorddevelopment.exe";
}
static bool belongsToDiscord(DWORD pid, const std::vector<ProcessInfo>& list) {
  for (size_t i = 0; i <= list.size(); ++i) {
    const auto process = std::find_if(list.begin(), list.end(), [&](const ProcessInfo& item) { return item.pid == pid; });
    if (process == list.end()) return false;
    if (isDiscord(process->name)) return true;
    if (!process->parent || process->parent == pid) return false;
    pid = process->parent;
  }
  return false;
}
static bool belongsToTree(DWORD pid, DWORD root, const std::vector<ProcessInfo>& list) {
  for (size_t i = 0; i <= list.size(); ++i) {
    if (pid == root) return true;
    const auto process = std::find_if(list.begin(), list.end(), [&](const ProcessInfo& item) { return item.pid == pid; });
    if (process == list.end() || !process->parent || process->parent == pid) return false;
    pid = process->parent;
  }
  return false;
}
class Activation final : public IActivateAudioInterfaceCompletionHandler {
 public:
  HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  HRESULT result = E_FAIL;
  IAudioClient* client = nullptr;
  ~Activation() { if (client) client->Release(); if (done) CloseHandle(done); }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
    if (!object) return E_POINTER;
    if (id == __uuidof(IUnknown) || id == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
      *object = static_cast<IActivateAudioInterfaceCompletionHandler*>(this); AddRef(); return S_OK;
    }
    *object = nullptr; return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
  ULONG STDMETHODCALLTYPE Release() override { const ULONG left = --references; if (!left) delete this; return left; }
  HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override {
    IUnknown* unknown = nullptr;
    HRESULT activation = E_FAIL;
    result = operation->GetActivateResult(&activation, &unknown);
    if (SUCCEEDED(result)) result = activation;
    if (SUCCEEDED(result) && unknown) result = unknown->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void**>(&client));
    if (unknown) unknown->Release();
    SetEvent(done);
    return S_OK;
  }
 private:
  std::atomic<ULONG> references{1};
};
static int fail(const wchar_t* code, HRESULT hr = E_FAIL) {
  fwprintf(stderr, L"%ls 0x%08lx\n", code, static_cast<unsigned long>(hr));
  fflush(stderr);
  return 1;
}
int wmain(int argc, wchar_t** argv) {
  if (argc != 3) return fail(L"USAGE");
  if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return fail(L"COM_INIT");
  const auto list = processes();
  DWORD target = 0;
  if (wcscmp(argv[1], L"window") == 0) {
    wchar_t* end = nullptr;
    const unsigned long long id = wcstoull(argv[2], &end, 0);
    if (!id || !end || *end) return fail(L"INVALID_WINDOW");
    const HWND window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(id));
    if (!IsWindow(window)) return fail(L"INVALID_WINDOW");
    GetWindowThreadProcessId(window, &target);
    const auto helper = std::find_if(list.begin(), list.end(), [&](const ProcessInfo& item) { return item.pid == GetCurrentProcessId(); });
    const DWORD appPid = helper == list.end() ? 0 : helper->parent;
    if (!target || !appPid || belongsToDiscord(target, list) || belongsToTree(target, appPid, list)) return fail(L"WINDOW_AUDIO_BLOCKED");
  } else return fail(L"USAGE");

  AUDIOCLIENT_ACTIVATION_PARAMS params{};
  params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
  params.ProcessLoopbackParams.TargetProcessId = target;
  params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
  PROPVARIANT activation{};
  activation.vt = VT_BLOB;
  activation.blob.cbSize = sizeof(params);
  activation.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
  Activation* handler = new Activation();
  IActivateAudioInterfaceAsyncOperation* operation = nullptr;
  HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &activation, handler, &operation);
  if (FAILED(hr)) { handler->Release(); return fail(L"AUDIO_ACTIVATION", hr); }
  if (WaitForSingleObject(handler->done, 10000) != WAIT_OBJECT_0) {
    if (operation) operation->Release(); handler->Release(); return fail(L"AUDIO_TIMEOUT");
  }
  if (operation) operation->Release();
  hr = handler->result;
  if (FAILED(hr) || !handler->client) { handler->Release(); return fail(L"AUDIO_ACTIVATION", hr); }
  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = 2; format.nSamplesPerSec = 48000;
  format.wBitsPerSample = 16; format.nBlockAlign = 4; format.nAvgBytesPerSec = 192000;
  hr = handler->client->Initialize(AUDCLNT_SHAREMODE_SHARED,
    AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
    0, 0, &format, nullptr);
  if (FAILED(hr)) { handler->Release(); return fail(L"AUDIO_FORMAT", hr); }
  IAudioCaptureClient* capture = nullptr;
  hr = handler->client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture));
  if (FAILED(hr)) { handler->Release(); return fail(L"AUDIO_CAPTURE", hr); }
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  hr = handler->client->SetEventHandle(event);
  if (SUCCEEDED(hr)) hr = handler->client->Start();
  if (FAILED(hr)) { capture->Release(); CloseHandle(event); handler->Release(); return fail(L"AUDIO_START", hr); }
  fwprintf(stderr, L"READY\n"); fflush(stderr);
  const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
  int exitCode = 0;
  for (;;) {
    const DWORD wait = WaitForSingleObject(event, 1000);
    if (wait != WAIT_OBJECT_0) { if (wait == WAIT_FAILED) { exitCode = fail(L"AUDIO_WAIT"); break; } continue; }
    // Fail closed if the target becomes part of a Discord process tree.
    if (belongsToDiscord(target, processes())) { exitCode = fail(L"DISCORD_WINDOW_BLOCKED"); break; }
    UINT32 frames = 0;
    while (SUCCEEDED(capture->GetNextPacketSize(&frames)) && frames) {
      BYTE* data = nullptr; DWORD flags = 0; UINT64 position = 0, qpc = 0;
      hr = capture->GetBuffer(&data, &frames, &flags, &position, &qpc);
      if (FAILED(hr)) { exitCode = fail(L"AUDIO_BUFFER", hr); break; }
      const DWORD bytes = frames * format.nBlockAlign;
      DWORD written = 0;
      std::vector<BYTE> silence;
      if (flags & AUDCLNT_BUFFERFLAGS_SILENT) { silence.resize(bytes); data = silence.data(); }
      if (!WriteFile(output, data, bytes, &written, nullptr) || written != bytes) exitCode = fail(L"AUDIO_PIPE");
      capture->ReleaseBuffer(frames);
      if (exitCode) break;
    }
    if (exitCode) break;
  }
  handler->client->Stop(); capture->Release(); CloseHandle(event); handler->Release(); CoUninitialize();
  return exitCode;
}
