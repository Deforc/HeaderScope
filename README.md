# HeaderScope

HeaderScope — приложение для macOS / Windows на C++ и Qt, которое анализирует связи `#include` в проектах на C/C++. Оно показывает, какие файлы зависят от выбранного заголовка, находит циклы и помогает проверить, создаст ли новое подключение цикл.

## Быстрый старт

Для сборки приложения нужны компилятор с C++20, CMake 3.21+ и Qt 6.5+ с модулем Qt Widgets. Команды ниже рассчитаны на запуск из корня репозитория.

```sh
cmake -S . -B build
cmake --build build
```

Если CMake не находит Qt, добавьте при конфигурации `-DCMAKE_PREFIX_PATH=/путь/к/Qt`. Укажите каталог нужного комплекта Qt для вашего компилятора и архитектуры.

На macOS запустите `open build/app/window/HeaderScope.app` при сборке генератором по умолчанию. На Windows файл `HeaderScope.exe` находится в `build/app/window/` или в его подкаталоге конфигурации, например `Debug`.

Без Qt можно собирать бэкенд и общие демоданные отдельно:

```sh
cmake -S . -B build-core -DHEADERSCOPE_BUILD_APP=OFF
cmake --build build-core
```

Каркас запускает пустое окно HeaderScope. Сборщик и ядро графа пока можно проверять через консольный прототип.

## Демопроект

Соберите небольшой проект `main.cpp → a.h → b.h`. На macOS можно использовать стандартный генератор CMake. Если на Windows используется Visual Studio, добавьте к первой команде `-G Ninja` и запускайте её в окружении с настроенным компилятором.

```sh
cmake -S demo/include-chain -B build-demo
cmake --build build-demo
```

Файл `build-demo/compile_commands.json` можно передать сборщику. Чтобы проверить условное подключение `a.h → extra.h`, повторите конфигурацию с `-DHEADERSCOPE_DEMO_EXTRA=ON`.

## Консольный прототип анализа

Для сборщика нужны библиотека libclang и заголовки `clang-c`. Если CMake не находит их, передайте префикс LLVM через `-DCMAKE_PREFIX_PATH` или задайте явно `HEADERSCOPE_LIBCLANG_INCLUDE_DIR` и `HEADERSCOPE_LIBCLANG_LIBRARY`.

```sh
cmake -S . -B build-inspect -DHEADERSCOPE_BUILD_APP=OFF -DHEADERSCOPE_BUILD_SCANNER=ON
cmake --build build-inspect
build-inspect/demo/inspect/headerscope_inspect build-demo/compile_commands.json demo/include-chain/b.h
```

Прототип показывает связи с номерами строк, циклические группы и кратчайшие цепочки от зависимых файлов до выбранного заголовка. Для проверки настоящего цикла повторите конфигурацию демопроекта с `-DHEADERSCOPE_DEMO_CYCLE=ON` и запустите анализ повторно.

Границы модулей, формат результата и сценарии для проверки описаны в [архитектуре и плане старта](docs/architecture.md).
