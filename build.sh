#!/bin/sh
# Универсальный POSIX-совместимый скрипт сборки

# Проверка аргументов
if [ $# -ne 1 ]; then
    echo "Usage: $0 <source_file>" >&2
    exit 1
fi

SRC="$1"

# Проверка существования исходного файла
if [ ! -f "$SRC" ]; then
    echo "Error: source file '$SRC' not found" >&2
    exit 2
fi

# Извлечение имени выходного файла из комментария Output:
# Поддерживает // Output:, # Output: и % Output:
OUTPUT_NAME=$(grep -E "^[[:space:]]*(//|#|%)*[[:space:]]*Output:" "$SRC" | head -n 1 | sed -E 's/.*Output:[[:space:]]*//')

if [ -z "$OUTPUT_NAME" ]; then
    echo "Error: no 'Output:' comment found in source file" >&2
    exit 3
fi

# Создание временного каталога
TMPDIR=$(mktemp -d) || {
    echo "Error: cannot create temporary directory" >&2
    exit 4
}

# Очистка временного каталога при любом завершении
cleanup() { rm -rf "$TMPDIR"; }
trap cleanup EXIT INT TERM

# Определение типа файла и команды сборки
case "$SRC" in
    *.c)
        CMD="cc -o \"$TMPDIR/$OUTPUT_NAME\" \"$SRC\""
        ;;
    *.cpp|*.cc|*.cxx)
        CMD="c++ -o \"$TMPDIR/$OUTPUT_NAME\" \"$SRC\""
        ;;
    *.tex)
        CMD="pdflatex -output-directory \"$TMPDIR\" \"$SRC\" >/dev/null 2>&1"
        ;;
    *)
        echo "Error: unsupported file type" >&2
        exit 5
        ;;
esac

# Выполнение сборки
echo "Building '$SRC'..."
if ! sh -c "$CMD"; then
    echo "Error: build failed" >&2
    exit 6
fi

# Перемещение результата рядом с исходным файлом
if [ -f "$TMPDIR/$OUTPUT_NAME" ]; then
    mv "$TMPDIR/$OUTPUT_NAME" "$(dirname "$SRC")/"
elif [ -f "$TMPDIR/${OUTPUT_NAME}.pdf" ]; then
    mv "$TMPDIR/${OUTPUT_NAME}.pdf" "$(dirname "$SRC")/"
else
    echo "Error: output file not found in temporary directory" >&2
    exit 8
fi

echo "Build succeeded. Output: $OUTPUT_NAME"
exit 0