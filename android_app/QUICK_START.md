# Быстрый старт

## Шаг 1: Открыть проект в Android Studio

1. Запустите Android Studio
2. File → Open → выберите папку `android_app`
3. Дождитесь синхронизации Gradle (может занять несколько минут при первом запуске)

## Шаг 2: Настроить SDK

Если Android SDK не установлен:
1. Tools → SDK Manager
2. Установите:
   - Android SDK Platform 34
   - Android SDK Build-Tools
   - Android Emulator (опционально)

## Шаг 3: Запустить приложение

### Вариант A: На реальном устройстве
1. Включите "Отладка по USB" на Android устройстве
2. Подключите устройство к компьютеру
3. В Android Studio нажмите Run (Shift+F10) или зеленая кнопка ▶️
4. Выберите ваше устройство

### Вариант B: На эмуляторе
1. Tools → Device Manager
2. Create Device → выберите устройство (например, Pixel 5)
3. Download и установите системный образ (например, Android 13)
4. Запустите эмулятор
5. В Android Studio нажмите Run

## Шаг 4: Настроить MQTT

После первого запуска приложения:
1. Нажмите на три точки в правом верхнем углу
2. Выберите "Настройки"
3. Заполните параметры MQTT (значения по умолчанию уже заполнены)
4. Нажмите "Сохранить"
5. Вернитесь на главный экран - приложение автоматически подключится к MQTT

## Возможные проблемы

### Ошибка: "SDK location not found"
- File → Project Structure → SDK Location
- Укажите путь к Android SDK (обычно `C:\Users\YourName\AppData\Local\Android\Sdk`)

### Ошибка: "Gradle sync failed"
- File → Invalidate Caches → Invalidate and Restart
- Или: File → Sync Project with Gradle Files

### Приложение не подключается к MQTT
- Проверьте, что ESP32 устройство включено и подключено к WiFi
- Проверьте настройки MQTT в веб-интерфейсе ESP32
- Убедитесь, что префикс топиков совпадает

### Ошибка компиляции Kotlin
- Убедитесь, что установлен Kotlin plugin в Android Studio
- File → Settings → Plugins → установите "Kotlin" если его нет

## Следующие шаги

После успешного запуска можно:
- Изменить дизайн в `app/src/main/res/layout/activity_main.xml`
- Добавить новые функции в `MainActivity.kt`
- Настроить цвета в `app/src/main/res/values/colors.xml`
