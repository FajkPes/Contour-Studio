#pragma once
namespace app {
// Returns -1 for a normal interactive launch. No WinUI or window in capture mode.
int captureCommand(int argc, wchar_t **argv);
} // namespace app
