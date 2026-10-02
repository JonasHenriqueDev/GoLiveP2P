// Unit tests exercise failing WASAPI packet calls without changing an audio device.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include "vendor/json.hpp"
using Json = nlohmann::json;
static void emit(const Json&) {}
static std::string utf8(const std::wstring&) { return {}; }
#include "process-audio.hpp"

class PacketClient final : public IAudioCaptureClient {
 public:
  HRESULT nextResult = S_OK, bufferResult = S_OK, releaseResult = S_OK;
  DWORD flags = 0;
  bool nullData = false;
  unsigned queried = 0, released = 0;
  int16_t samples[4]{120, -120, 240, -240};
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
  ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }
  HRESULT STDMETHODCALLTYPE GetNextPacketSize(UINT32* frames) override {
    *frames = queried++ ? 0 : 2;
    return nextResult;
  }
  HRESULT STDMETHODCALLTYPE GetBuffer(BYTE** data, UINT32* frames, DWORD* packetFlags, UINT64*,
                                      UINT64*) override {
    *data = nullData ? nullptr : reinterpret_cast<BYTE*>(samples);
    *frames = 2;
    *packetFlags = flags;
    return bufferResult;
  }
  HRESULT STDMETHODCALLTYPE ReleaseBuffer(UINT32) override {
    ++released;
    return releaseResult;
  }
};

int main() {
  unsigned passed = 0;
  auto expect = [&passed](bool condition) {
    if (!condition) throw std::runtime_error("Native WASAPI error handling regression");
    ++passed;
  };
  auto discard = [](const int16_t*, UINT32, bool) {};
  try {
    {
      PacketClient client;
      client.nextResult = AUDCLNT_E_DEVICE_INVALIDATED;
      expect(process_audio::drainPackets(&client, discard) == AUDCLNT_E_DEVICE_INVALIDATED &&
             client.released == 0);
    }
    {
      PacketClient client;
      client.bufferResult = AUDCLNT_E_SERVICE_NOT_RUNNING;
      expect(process_audio::drainPackets(&client, discard) == AUDCLNT_E_SERVICE_NOT_RUNNING &&
             client.released == 0);
    }
    {
      PacketClient client;
      client.bufferResult = AUDCLNT_S_BUFFER_EMPTY;
      client.nullData = true;
      expect(process_audio::drainPackets(&client, discard) == S_OK && client.released == 0);
    }
    {
      PacketClient client;
      client.releaseResult = AUDCLNT_E_DEVICE_INVALIDATED;
      expect(process_audio::drainPackets(&client, discard) == AUDCLNT_E_DEVICE_INVALIDATED &&
             client.released == 1 && client.queried == 1);
    }
    {
      PacketClient client;
      client.nullData = true;
      expect(process_audio::drainPackets(&client, discard) == E_POINTER && client.released == 1);
    }
    {
      PacketClient client;
      client.flags = AUDCLNT_BUFFERFLAGS_SILENT;
      client.nullData = true;
      bool silence = false;
      expect(process_audio::drainPackets(&client,
                                         [&](const int16_t* data, UINT32 frames, bool silent) {
                                           silence = !data && frames == 2 && silent;
                                         }) == S_OK &&
             silence && client.released == 1);
    }
    {
      PacketClient client;
      std::vector<int16_t> received;
      expect(process_audio::drainPackets(&client,
                                         [&](const int16_t* data, UINT32 frames, bool silent) {
                                           if (!silent) received.assign(data, data + frames * 2);
                                         }) == S_OK &&
             received == std::vector<int16_t>({120, -120, 240, -240}) && client.released == 1);
    }
    {
      PacketClient client;
      bool threw = false;
      try {
        process_audio::drainPackets(&client, [](const int16_t*, UINT32, bool) {
          throw std::runtime_error("Simulated allocation failure");
        });
      } catch (const std::runtime_error&) {
        threw = true;
      }
      expect(threw && client.released == 1);
    }
    {
      process_audio::RetrySchedule retry;
      expect(retry.ready(100, 0));
      retry.fail(100, 200);
      expect(!retry.ready(100, 1199) && retry.ready(100, 1200));
      retry.fail(100, 1200);
      expect(!retry.ready(100, 3199) && retry.ready(100, 3200));
      expect(retry.ready(200, 1201));  // A reused PID is a new identity, never the old capture.
      retry.fail(200, 1500);
      expect(retry.due == 2500 && retry.identity == 200);
      for (int i = 0; i < 100; ++i) retry.fail(200, 5000);
      expect(retry.due == 35000);  // Backoff stays bounded and never overflows.
    }
    {
      std::vector<process_audio::Process> processes = {
        {2000001, 0, L"player.exe", L"c:\\player.exe", 100},
        {2000002, 2000001, L"child.exe", L"", 0}};
      expect(process_audio::potentialDescendant(2000002, 2000001, processes));
      expect(!process_audio::safe(2000001, processes));
      processes[1].image = L"c:\\child.exe";
      processes[1].created = 200;
      expect(process_audio::safe(2000001, processes));
      processes[1].created = 50;
      expect(!process_audio::safe(2000001, processes));
    }
    std::cout << Json({{"passed", passed},
                       {"scope", "WASAPI packet failures and safe retry scheduling"}})
              << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
