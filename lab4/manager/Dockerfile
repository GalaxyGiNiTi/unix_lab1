# Этап сборки
FROM golang:1.21-alpine AS builder

WORKDIR /app

# Копируем файлы модуля
COPY go.mod ./
RUN go mod download

# Копируем исходный код
COPY main.go ./

# Собираем бинарник
RUN CGO_ENABLED=0 GOOS=linux go build -o manager .

# Финальный образ
FROM alpine:latest

WORKDIR /app

# Копируем бинарник из этапа сборки
COPY --from=builder /app/manager .

EXPOSE 8000

CMD ["./manager"]
