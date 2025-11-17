FROM debian:stable-slim

# Устанавливаем утилиты:
# - util-linux  -> содержит команду `flock`
# - coreutils, procps и т.п. обычно уже есть, но stable-slim может быть "обрезанным"
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        util-linux \
        coreutils && \
    rm -rf /var/lib/apt/lists/*

# Рабочая директория
WORKDIR /app

# Копируем скрипт
COPY entrypoint.sh /app/entrypoint.sh

# Делаем скрипт исполняемым
RUN chmod +x /app/entrypoint.sh

# Общий том будет монтироваться в /shared
VOLUME ["/shared"]

# По умолчанию запускаем скрипт
CMD ["/app/entrypoint.sh"]
