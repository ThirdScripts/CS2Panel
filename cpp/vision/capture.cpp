#include "vision/capture.h"

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>
#include <cstring>

#include "core/win32.h"

namespace cs2n {
namespace {

// Own the GDI surface even if Image/vector allocation throws.
struct GdiSurface {
  HDC screen = nullptr, compatible = nullptr;
  HBITMAP bitmap = nullptr;
  HGDIOBJ previous = nullptr;
  ~GdiSurface() {
    if (compatible && previous && previous != HGDI_ERROR) SelectObject(compatible, previous);
    if (bitmap) DeleteObject(bitmap);
    if (compatible) DeleteDC(compatible);
    if (screen) ReleaseDC(nullptr, screen);
  }
};

struct ClientWindow {
  HWND handle = nullptr;
  int width = 0;
  int height = 0;
};

ClientWindow findCs2Window() {
  ClientWindow result;
  HWND window = GetForegroundWindow();
  if (window == nullptr) return result;
  const std::uint32_t pid = win32::processIdOf(window);
  if (pid == 0) return result;
  const char* path=win32::processName(pid);
  const char* slash=std::strrchr(path,'\\');
  if(std::strcmp(slash?slash+1:path,"cs2.exe")!=0)return result;
  RECT rect{};
  if (!GetClientRect(window, &rect)) return result;
  result.handle = window;
  result.width = rect.right - rect.left;
  result.height = rect.bottom - rect.top;
  return result;
}

}  // namespace

bool CaptureRegion::validate(std::string& error) const {
  if (x < 0 || y < 0) {
    error = "capture region must start inside the window";
    return false;
  }
  if (width < 32 || height < 32) {
    error = "capture region is too small";
    return false;
  }
  return true;
}

bool RadarCapture::cs2ClientSize(int& width, int& height) {
  const ClientWindow window = findCs2Window();
  if (window.handle == nullptr) return false;
  width = window.width;
  height = window.height;
  return true;
}

std::optional<Image> RadarCapture::grab(const CaptureRegion& region, std::string& error) {
  std::string validation;
  if (!region.validate(validation)) {
    error = validation;
    return std::nullopt;
  }
  const ClientWindow window = findCs2Window();
  if (window.handle == nullptr) {
    error = "CS2 is not the foreground window";
    return std::nullopt;
  }
  if (region.x + region.width > window.width || region.y + region.height > window.height) {
    error = "capture region leaves the CS2 client area";
    return std::nullopt;
  }

  GdiSurface surface;
  HDC screen = surface.screen = GetDC(nullptr);
  if (screen == nullptr) {
    error = "GetDC failed";
    return std::nullopt;
  }
  HDC compatible = surface.compatible = CreateCompatibleDC(screen);
  HBITMAP bitmap = surface.bitmap = CreateCompatibleBitmap(screen, region.width, region.height);
  if (compatible == nullptr || bitmap == nullptr) {
    error = "GDI surface allocation failed";
    return std::nullopt;
  }
  HGDIOBJ previous = surface.previous = SelectObject(compatible, bitmap);
  if (!previous || previous == HGDI_ERROR) { error = "SelectObject failed"; return std::nullopt; }

  POINT origin{region.x,region.y};
  const BOOL located=ClientToScreen(window.handle,&origin);
  const BOOL ok = located&&BitBlt(compatible, 0, 0, region.width, region.height, screen, origin.x, origin.y,
                         SRCCOPY | CAPTUREBLT);

  Image frame;
  SelectObject(compatible,previous); // GetDIBits requires an unselected bitmap.
  if (ok) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = region.width;
    info.bmiHeader.biHeight = -region.height;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    std::vector<std::uint32_t> buffer(static_cast<std::size_t>(region.width) * static_cast<std::size_t>(region.height));
    if (GetDIBits(compatible, bitmap, 0, static_cast<UINT>(region.height), buffer.data(), &info, DIB_RGB_COLORS) == 0) {
      error = "GetDIBits failed";
    } else {
      frame = Image(region.width, region.height);
      for (std::size_t i = 0; i < buffer.size(); ++i) {
        const std::uint32_t pixel = buffer[i];
        frame.pixels()[i] = rgbFromDib(pixel);
      }
      // A fully black frame usually means an exclusive-fullscreen surface that
      // GDI cannot read. Treat it as a failure rather than feeding blackness to
      // the localizer.
      bool anyNonBlack = false;
      for (const Rgb& pixel : frame.pixels()) {
        if (pixel.r != 0 || pixel.g != 0 || pixel.b != 0) {
          anyNonBlack = true;
          break;
        }
      }
      if (!anyNonBlack) {
        error = "capture returned a black frame; use borderless windowed mode";
        frame = Image();
      }
    }
  } else {
    error = "BitBlt failed";
  }


  if (frame.empty()) return std::nullopt;
  return frame;
}

}  // namespace cs2n
