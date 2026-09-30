"""Run actual configuration erase/restart functions with failing storage doubles."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'src/Config.cpp').read_text()
functions = source[source.index('bool eraseStoredConfiguration()'):source.index('void saveFallback')]
harness = r'''
#include <cassert>
struct Storage {
  bool openOk = true, clearOk = true;
  int clears = 0, closes = 0;
  bool begin(const char*, bool) { return openOk; }
  bool clear() { ++clears; return clearOk; }
  void end() { ++closes; }
} preferences;
struct Device { int restarts = 0; void restart() { ++restarts; } } ESP;
enum { INFO, WARN, ERROR };
void appLog(const char*, const char*, int = INFO) {}
void delay(int) {}
'''
harness += functions
harness += r'''
int main() {
  preferences.openOk = false;
  clearPreferences();
  assert(preferences.clears == 0 && ESP.restarts == 0);
  preferences.openOk = true;
  preferences.clearOk = false;
  clearPreferences();
  assert(preferences.clears == 1 && preferences.closes == 1 && ESP.restarts == 0);
  preferences.clearOk = true;
  clearPreferences();
  assert(preferences.clears == 2 && preferences.closes == 2 && ESP.restarts == 1);
}
'''
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder)
    (path / 'test.cpp').write_text(harness)
    subprocess.run(['c++', '-std=c++11', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('PASS: open failure, erase failure, successful erase/restart')
