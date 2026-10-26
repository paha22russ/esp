from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/main.cpp').read_text()
cases = [
    ('encoder_decoder_test', 'void IRAM_ATTR encoderISR()', '// Обновление дисплея OLED'),
    ('encoder_button_test', 'void handleEncoderButton()', '// Главная функция обработки энкодера'),
]
with tempfile.TemporaryDirectory() as folder:
    for name, start, end in cases:
        a = source.index(start)
        function = source[a:source.index(end, a)]
        harness = (root / 'tests' / (name + '.cpp.in')).read_text()
        cpp = Path(folder) / (name + '.cpp')
        executable = Path(folder) / name
        cpp.write_text(harness.replace('// PRODUCTION_FUNCTION', function))
        subprocess.run(['g++', '-std=c++14', str(cpp), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        print(name + ': PASS')
    subprocess.run(['g++', '-std=c++14', '-c', str(root / 'tests/supply_guard_compile_test.cpp'),
                    '-o', str(Path(folder) / 'supply_guard.o')], check=True)
    print('supply guard compile-time assertions: PASS')
with tempfile.TemporaryDirectory() as folder:
    exe = Path(folder) / 'ota_result_test'
    subprocess.run(['g++', '-std=c++14', str(root / 'tests/ota_result_test.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    print('OTA result/version policy: PASS')

with tempfile.TemporaryDirectory() as folder:
    a = source.index('bool saveAutoSettingsToEEPROM()')
    function = source[a:source.index('void saveMqttSettingsToEEPROM()', a)]
    harness = (root / 'tests/auto_settings_test.cpp.in').read_text()
    cpp = Path(folder) / 'auto_settings_test.cpp'
    cpp.write_text(harness.replace('// PRODUCTION_FUNCTION', function))
    exe = Path(folder) / 'auto_settings_test'
    subprocess.run(['g++', '-std=c++14', '-I' + str(root / '.pio/libdeps/esp32dev/ArduinoJson/src'), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    print('Auto settings: failed write/readback, retry, reboot, debounce, rollover: PASS')

with tempfile.TemporaryDirectory() as folder:
    exe = Path(folder) / 'settings_store_test'
    subprocess.run(['g++', '-std=c++14', '-I' + str(root / 'tests/storage_stubs'),
                    '-I' + str(root / '.pio/libdeps/esp32dev/ArduinoJson/src'),
                    str(root / 'tests/settings_store_test.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    print('Settings store: NVS unavailable, partial migration, torn/corrupt writes, reboot and recovery: PASS')
