// Development-only libobs evaluation; not linked or shipped with the media engine.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "obs.h"
#include <atomic>
#include <iostream>
#include <fstream>
#include <string>
static std::atomic<unsigned> frames{0}, green{0}, nonblack{0};
static std::string snapshot;
static void frame(void*, video_data* video) {
  unsigned count = ++frames;
  auto pixel = video->data[0] + video->linesize[0] * 200 + 200 * 4;
  if (pixel[0] || pixel[1] || pixel[2]) ++nonblack;
  if (pixel[1] > pixel[0] + 20 && pixel[1] > pixel[2] + 20) ++green;
  if (count == 180 || count == 480 || count == 960) {
    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + 960 * 540 * 4;
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = 960;
    info.biHeight = -540;
    info.biPlanes = 1;
    info.biBitCount = 32;
    std::ofstream output(snapshot + "-" + std::to_string(count) + ".bmp", std::ios::binary);
    output.write(reinterpret_cast<char*>(&file), sizeof(file));
    output.write(reinterpret_cast<char*>(&info), sizeof(info));
    for (unsigned y = 0; y < 540; ++y)
      output.write(reinterpret_cast<char*>(video->data[0] + y * video->linesize[0]), 960 * 4);
  }
}
int main(int argc, char** argv) {
  if (argc < 3) return 2;
  std::string root = argv[1], mode = argv[2];
  char currentDirectory[32768]{};
  GetCurrentDirectoryA(sizeof(currentDirectory), currentDirectory);
  snapshot = std::string(currentDirectory) + "/work/obs-evaluation/" + mode +
             (argc > 3 ? "-chrome" : "-fixture");
  SetCurrentDirectoryA((root + "/bin/64bit").c_str());
  SetDllDirectoryA((root + "/bin/64bit").c_str());
  auto dll = LoadLibraryA((root + "/bin/64bit/obs.dll").c_str());
  if (!dll) {
    std::cerr << "LoadLibrary " << GetLastError();
    return 3;
  }
#define LOAD(name)                                                                \
  auto name##_fn = reinterpret_cast<decltype(&name)>(GetProcAddress(dll, #name)); \
  if (!name##_fn) return 4
  LOAD(obs_startup);
  LOAD(obs_shutdown);
  LOAD(obs_add_data_path);
  LOAD(obs_reset_video);
  LOAD(obs_open_module);
  LOAD(obs_init_module);
  LOAD(obs_data_create);
  LOAD(obs_data_set_string);
  LOAD(obs_data_set_int);
  LOAD(obs_data_set_bool);
  LOAD(obs_data_release);
  LOAD(obs_source_create_private);
  LOAD(obs_set_output_source);
  LOAD(obs_source_release);
  LOAD(obs_source_get_width);
  LOAD(obs_source_inc_showing);
  LOAD(obs_source_dec_showing);
  LOAD(obs_add_raw_video_callback);
  LOAD(obs_remove_raw_video_callback);
  std::string configPath = std::string(currentDirectory) + "/work/obs-evaluation/config";
  CreateDirectoryA(configPath.c_str(), nullptr);
  if (!obs_startup_fn("en-US", configPath.c_str(), nullptr)) return 5;
  obs_add_data_path_fn((root + "/data/libobs/").c_str());
  obs_video_info video{};
  std::string graphics = root + "/bin/64bit/libobs-d3d11.dll";
  video.graphics_module = graphics.c_str();
  video.fps_num = 60;
  video.fps_den = 1;
  video.base_width = video.output_width = 960;
  video.base_height = video.output_height = 540;
  video.output_format = VIDEO_FORMAT_BGRA;
  video.gpu_conversion = false;
  video.colorspace = VIDEO_CS_709;
  video.range = VIDEO_RANGE_FULL;
  video.scale_type = OBS_SCALE_BILINEAR;
  int reset = obs_reset_video_fn(&video);
  if (reset != 0) {
    std::cerr << "reset " << reset;
    return 6;
  }
  obs_module_t* module = nullptr;
  int opened = obs_open_module_fn(&module, (root + "/obs-plugins/64bit/win-capture.dll").c_str(),
                                  (root + "/data/obs-plugins/win-capture").c_str());
  if (opened != 0 || !obs_init_module_fn(module)) return 7;
  auto settings = obs_data_create_fn();
  std::string target = "GoLive native validation target:GoLiveMediaFixture:media-fixture.exe";
  if (argc > 3 && std::string(argv[3]) == "chrome") {
    EnumWindows(
        [](HWND window, LPARAM pointer) -> BOOL {
          char title[1024]{}, klass[256]{};
          GetWindowTextA(window, title, sizeof(title));
          GetClassNameA(window, klass, sizeof(klass));
          if (IsWindowVisible(window) &&
              std::string(title).find("Google Chrome") != std::string::npos) {
            *reinterpret_cast<std::string*>(pointer) =
                std::string(title) + ":" + klass + ":chrome.exe";
            return FALSE;
          }
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&target));
  }
  obs_data_set_string_fn(settings, "window", target.c_str());
  obs_data_set_string_fn(settings, "capture_mode", "window");
  obs_data_set_int_fn(settings, "method", mode == "wgc" ? 2 : 1);
  obs_data_set_int_fn(settings, "priority", 0);
  obs_data_set_bool_fn(settings, "capture_audio", false);
  obs_data_set_bool_fn(settings, "capture_cursor", false);
  obs_data_set_bool_fn(settings, "sli_compatibility", false);
  auto source = obs_source_create_private_fn(mode == "game" ? "game_capture" : "window_capture",
                                             "GoLive OBS integration probe", settings);
  obs_data_release_fn(settings);
  if (!source) return 8;
  obs_source_inc_showing_fn(source);
  obs_set_output_source_fn(0, source);
  obs_add_raw_video_callback_fn(nullptr, frame, nullptr);
  Sleep(17000);
  std::cout << "{\"mode\":\"" << mode << "\",\"frames\":" << frames
            << ",\"nonblackCenter\":" << nonblack << ",\"greenCenter\":" << green
            << ",\"sourceWidth\":" << obs_source_get_width_fn(source) << "}\n";
  obs_remove_raw_video_callback_fn(frame, nullptr);
  obs_set_output_source_fn(0, nullptr);
  obs_source_dec_showing_fn(source);
  obs_source_release_fn(source);
  obs_shutdown_fn();
  FreeLibrary(dll);
}
