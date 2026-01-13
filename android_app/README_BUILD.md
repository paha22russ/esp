# Инструкция по сборке APK

## Проблемы с кешем Gradle

**ВАЖНО:** Если возникают ошибки "Could not read workspace metadata", это обычно означает, что антивирус блокирует доступ к файлам Gradle.

### Способ 1: Автоматическая очистка + Антивирус

1. **Добавьте в исключения антивируса:**
   - `C:\Users\Pavel\.gradle` (весь кеш Gradle)
   - `C:\Users\Pavel\Yandex.Disk\Arduino\Projects\kotel_esp32\android_app` (весь проект)

2. **Закройте Android Studio ПОЛНОСТЬЮ**

3. **Запустите `clean_gradle_cache.bat`** в папке `android_app`
   - Дважды кликните на файл
   - Или в PowerShell: `cd android_app; .\clean_gradle_cache.bat`

4. **Подождите 10 секунд**

5. **Откройте Android Studio**

6. **File → Invalidate Caches / Restart → Invalidate and Restart**

7. **Дождитесь полной синхронизации Gradle** (может занять 5-10 минут)

8. **Build → Clean Project**

9. **Build → Rebuild Project**

### Способ 2: Ручная очистка

1. Закройте Android Studio
2. Удалите папку: `%USERPROFILE%\.gradle\caches\8.13`
3. Удалите папку: `android_app\.gradle`
4. Удалите папки: `android_app\build` и `android_app\app\build`
5. Откройте Android Studio
6. File → Invalidate Caches / Restart
7. Build → Clean Project
8. Build → Rebuild Project

### Способ 3: Если антивирус блокирует

1. Временно отключите антивирус
2. Выполните очистку (Способ 1 или 2)
3. Добавьте папку `%USERPROFILE%\.gradle` в исключения антивируса
4. Включите антивирус
5. Попробуйте собрать проект снова

## Сборка APK

### Debug APK

1. Build → Build Bundle(s) / APK(s) → Build APK(s)
2. После сборки появится уведомление
3. Нажмите "locate" в уведомлении
4. APK будет в: `android_app\app\build\outputs\apk\debug\app-debug.apk`

### Release APK

1. Build → Generate Signed Bundle / APK
2. Выберите "APK"
3. Создайте новый ключ (Create new...) или используйте существующий
4. Заполните данные ключа
5. Выберите "release" build variant
6. Нажмите "Finish"
7. APK будет в: `android_app\app\build\outputs\apk\release\app-release.apk`

## Установка APK на телефон

1. Скопируйте APK файл на телефон
2. Откройте файл на телефоне
3. Разрешите установку из неизвестных источников (если нужно)
4. Установите приложение

## Настройка MQTT в приложении

После установки приложения:

1. Откройте приложение
2. Нажмите меню (три точки)
3. Выберите "Настройки"
4. Заполните параметры MQTT:
   - Сервер: `m5.wqtt.ru` (или ваш брокер)
   - Порт: `5374`
   - Пользователь: ваш логин
   - Пароль: ваш пароль
   - Префикс: `kotel/device1` (или ваш префикс)
5. Нажмите "Сохранить"
