#!/bin/sh

# Каталог общего тома (куда будет примонтирован volume)
SHARED_DIR="${SHARED_DIR:-/shared}"

# Файл для синхронизации (на нём будем брать flock)
LOCKFILE="$SHARED_DIR/.lockfile"

# Убедимся, что каталог существует
mkdir -p "$SHARED_DIR"

# Открываем дескриптор 9 на LOCKFILE (создаст файл, если его нет)
exec 9>>"$LOCKFILE"

# Генерируем случайный идентификатор контейнера (один на весь запуск)
# Можно сделать простой: 8 случайных символов [A-Za-z0-9]
CONTAINER_ID="$(head /dev/urandom 2>/dev/null | tr -dc 'A-Za-z0-9' | head -c 8)"
[ -z "$CONTAINER_ID" ] && CONTAINER_ID="fallbackID"

echo "Container started, ID = $CONTAINER_ID"
echo "Shared dir: $SHARED_DIR"

# Счётчик файлов, созданных этим контейнером
FILE_SEQ=0

while :; do
    # ---- Критическая секция: выбор имени и создание файла ----
    # Берём эксклюзивную блокировку на дескриптор 9
    flock 9

    i=1
    while :; do
        FILE_NAME=$(printf '%03d' "$i")
        FILE_PATH="$SHARED_DIR/$FILE_NAME"

        if [ ! -e "$FILE_PATH" ]; then
            # Создаём файл (пустой). Важно сделать это внутри блокировки!
            : > "$FILE_PATH"
            break
        fi

        i=$((i + 1))
    done

    # Снимаем блокировку как можно раньше
    flock -u 9
    # ---- Конец критической секции ----

    # Увеличиваем локальный порядковый номер файла
    FILE_SEQ=$((FILE_SEQ + 1))

    # Записываем данные в файл (уже без блокировки: имя занято, другие его не возьмут)
    {
        printf 'CONTAINER_ID=%s\n' "$CONTAINER_ID"
        printf 'FILE_SEQ=%s\n' "$FILE_SEQ"
    } > "$FILE_PATH"

    # Задержка 1 секунда
    sleep 1

    # Удаляем файл
    rm -f "$FILE_PATH"

    # Ещё одна задержка 1 секунда
    sleep 1
done
