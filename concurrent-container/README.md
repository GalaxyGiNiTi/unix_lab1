Практическая работа «Конкурентный контейнер»
# Практическая работа: Конкурентный контейнер (Docker + POSIX Shell)

Цель работы — создать Docker-контейнер, который конкурентно управляет файлами в общем разделеяемом томе, корректно работает при запуске 1, 10 и 50 экземпляров и исключает race condition при создании файлов.

---

# 1. Структура проекта



concurrent-container/
├── Dockerfile
└── entrypoint.sh


---

# 2. Dockerfile

```Dockerfile
FROM debian:stable-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends util-linux coreutils && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY entrypoint.sh /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh

VOLUME ["/shared"]

CMD ["/app/entrypoint.sh"]

3. Скрипт entrypoint.sh
#!/bin/sh

SHARED_DIR="${SHARED_DIR:-/shared}"
LOCKFILE="$SHARED_DIR/.lockfile"

mkdir -p "$SHARED_DIR"
exec 9>>"$LOCKFILE"

CONTAINER_ID="$(head /dev/urandom 2>/dev/null | tr -dc 'A-Za-z0-9' | head -c 8)"
[ -z "$CONTAINER_ID" ] && CONTAINER_ID="fallbackID"

echo "Container started, ID = $CONTAINER_ID"
echo "Shared dir: $SHARED_DIR"

FILE_SEQ=0

while :; do
    flock 9

    i=1
    while :; do
        FILE_NAME=$(printf '%03d' "$i")
        FILE_PATH="$SHARED_DIR/$FILE_NAME"

        if [ ! -e "$FILE_PATH" ]; then
            : > "$FILE_PATH"
            break
        fi

        i=$((i + 1))
    done

    flock -u 9

    FILE_SEQ=$((FILE_SEQ + 1))

    {
        printf 'CONTAINER_ID=%s\n' "$CONTAINER_ID"
        printf 'FILE_SEQ=%s\n' "$FILE_SEQ"
    } > "$FILE_PATH"

    sleep 1
    rm -f "$FILE_PATH"
    sleep 1
done

4. Создание общего тома
docker volume create shared-volume

5. Сборка образа

В каталоге проекта:

docker build -t concurrent-container .

6. Запуск одного контейнера

PowerShell:

docker run --rm -it -v shared-volume:/shared --name c1 concurrent-container

7. Проверка содержимого тома

Открыть отдельный контейнер:

docker run --rm -it -v shared-volume:/shared debian:stable-slim bash


Установить нужные утилиты:

apt-get update >/dev/null && apt-get install -y coreutils >/dev/null


Посмотреть содержимое каждые 0.5 сек:

while true; do clear; ls -l /shared; sleep 0.5; done

8. Запуск 10 контейнеров
for ($i = 1; $i -le 10; $i++) {
    docker run -d -v shared-volume:/shared --name "c$i" concurrent-container
}


Проверить:

docker ps

9. Запуск 50 контейнеров (стресс-тест)
for ($i = 1; $i -le 50; $i++) {
    docker run -d -v shared-volume:/shared --name "c$i" concurrent-container
}

10. Проверка отсутствия race condition

Внутри наблюдающего контейнера:

while true; do clear; ls -1 /shared; sleep 0.2; done


Ожидаемое:

в каталоге нет двух файлов с одинаковыми именами;

ошибки доступа или повреждённые файлы не появляются;

каждый созданный файл содержит данные конкретного контейнера.

11. Остановка всех тестовых контейнеров
docker rm -f (docker ps -q --filter "name=c")

12. Удаление тома (если нужно)
docker volume rm shared-volume

13. Вывод

В данной работе реализован конкурентный контейнер, обеспечивающий атомарное создание временных файлов в общем Docker-томе с использованием утилиты flock. Решение устойчиво работает при параллельном запуске 1, 10 и 50 экземпляров, исключая race conditions при доступе к общему ресурсу.