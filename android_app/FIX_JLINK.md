# КРИТИЧНО: Исправление ошибки jlink.exe

## Проблема

Ошибка при сборке:
```
Error while executing process ...\jlink.exe
Execution failed for JdkImageTransform
```

**Причина:** Антивирус блокирует выполнение `jlink.exe`, который необходим для Android SDK 34.

## Решение (ОБЯЗАТЕЛЬНО!)

### Шаг 1: Добавьте в исключения антивируса

**Добавьте эти файлы и папки в исключения:**

1. **`jlink.exe`** (критично!):
   ```
   C:\Program Files\Android\Android Studio\jbr\bin\jlink.exe
   ```
   Или весь каталог:
   ```
   C:\Program Files\Android\Android Studio\jbr\bin\
   ```

2. **Кеш Gradle:**
   ```
   C:\Users\Pavel\.gradle
   ```

3. **Проект Android:**
   ```
   C:\Users\Pavel\Yandex.Disk\Arduino\Projects\kotel_esp32\android_app
   ```

4. **Android SDK:**
   ```
   C:\Users\Pavel\AppData\Local\Android\Sdk
   ```

### Шаг 2: Как добавить в исключения

#### Windows Defender:
1. Откройте "Безопасность Windows"
2. Защита от вирусов и угроз → Управление настройками
3. Исключения → Добавить или удалить исключения
4. Добавьте файлы и папки (см. выше)

#### Kaspersky:
1. Настройки → Дополнительно → Угрозы и исключения → Исключения
2. Добавьте файлы и папки

#### Avast:
1. Настройки → Общие → Исключения
2. Добавьте файлы и папки

#### Norton:
1. Настройки → Антивирус → Сканирование и риски → Исключения
2. Добавьте файлы и папки

### Шаг 3: Очистка кеша

1. Закройте Android Studio **ПОЛНОСТЬЮ**
2. Запустите `clean_gradle_cache.bat`
3. Подождите 10 секунд

### Шаг 4: Перезапуск

1. Откройте Android Studio
2. File → Invalidate Caches / Restart → Invalidate and Restart
3. Дождитесь синхронизации (5-10 минут)

### Шаг 5: Сборка

1. Build → Clean Project
2. Build → Rebuild Project
3. Build → Build Bundle(s) / APK(s) → Build APK(s)

## Альтернативное решение

Если добавление в исключения не помогает:

1. **Временно отключите антивирус** на время сборки (10-15 минут)
2. Выполните очистку (`clean_gradle_cache.bat`)
3. Откройте Android Studio
4. Build → Clean Project
5. Build → Rebuild Project
6. Build → Build Bundle(s) / APK(s) → Build APK(s)
7. **Включите антивирус обратно** после успешной сборки

## Почему это происходит?

Android SDK 34 использует `jlink.exe` для создания JDK image. Антивирусы часто блокируют выполнение этого инструмента, так как он может создавать и модифицировать файлы, что похоже на поведение вредоносного ПО.

## Важно

**НЕ удаляйте файлы и папки из исключений** после успешной сборки - они нужны для всех будущих сборок!
