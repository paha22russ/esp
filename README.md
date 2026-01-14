# ESP32 Project

Пустой проект для ESP32.

## Структура проекта

- `src/` - исходные файлы
- `include/` - заголовочные файлы
- `lib/` - локальные библиотеки
- `test/` - тесты

## Сборка

```bash
pio run -e esp32dev
```

## Загрузка через USB

```bash
pio run -e esp32dev -t upload
```

## Загрузка через OTA

```bash
pio run -e esp32dev_ota -t upload
```
