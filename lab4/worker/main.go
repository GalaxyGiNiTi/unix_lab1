package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"
	"unicode"

	amqp "github.com/rabbitmq/amqp091-go"
)

// Конфигурация RabbitMQ
var (
	rabbitmqHost = getEnv("RABBITMQ_HOST", "localhost")
	rabbitmqUser = getEnv("RABBITMQ_USER", "user")
	rabbitmqPass = getEnv("RABBITMQ_PASS", "password")
	workerID     = getEnv("HOSTNAME", fmt.Sprintf("worker-%d", time.Now().UnixNano()))
)

// Структуры для JSON
type TaskRequest struct {
	Text string `json:"text"`
}

type TaskResponse struct {
	WordCount   int            `json:"word_count"`
	CharCount   int            `json:"char_count"`
	Words       map[string]int `json:"words"`
	ProcessedBy string         `json:"processed_by"`
}

func getEnv(key, defaultValue string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return defaultValue
}

func getRabbitMQURL() string {
	return fmt.Sprintf("amqp://%s:%s@%s:5672/", rabbitmqUser, rabbitmqPass, rabbitmqHost)
}

// countWords анализирует текст и возвращает статистику
func countWords(text string) TaskResponse {
	// Разбиваем текст на слова
	words := strings.FieldsFunc(text, func(r rune) bool {
		return !unicode.IsLetter(r) && !unicode.IsNumber(r)
	})

	// Считаем частоту слов
	wordFreq := make(map[string]int)
	for _, word := range words {
		word = strings.ToLower(word)
		if word != "" {
			wordFreq[word]++
		}
	}

	// Считаем символы (без пробелов)
	charCount := 0
	for _, r := range text {
		if !unicode.IsSpace(r) {
			charCount++
		}
	}

	return TaskResponse{
		WordCount:   len(words),
		CharCount:   charCount,
		Words:       wordFreq,
		ProcessedBy: workerID,
	}
}

// connectWithRetry пытается подключиться к RabbitMQ с повторами
func connectWithRetry(url string, maxRetries int) (*amqp.Connection, error) {
	var conn *amqp.Connection
	var err error

	for i := 0; i < maxRetries; i++ {
		conn, err = amqp.Dial(url)
		if err == nil {
			return conn, nil
		}
		log.Printf("Попытка подключения к RabbitMQ %d/%d: %v", i+1, maxRetries, err)
		time.Sleep(2 * time.Second)
	}
	return nil, fmt.Errorf("не удалось подключиться к RabbitMQ после %d попыток: %w", maxRetries, err)
}

func main() {
	log.Printf("Worker %s запускается...", workerID)

	conn, err := connectWithRetry(getRabbitMQURL(), 30)
	if err != nil {
		log.Fatalf("Ошибка подключения: %v", err)
	}
	defer conn.Close()

	ch, err := conn.Channel()
	if err != nil {
		log.Fatalf("Ошибка создания канала: %v", err)
	}
	defer ch.Close()

	// Объявляем очередь задач
	q, err := ch.QueueDeclare(
		"word_count_tasks", // name
		true,               // durable
		false,              // delete when unused
		false,              // exclusive
		false,              // no-wait
		nil,                // arguments
	)
	if err != nil {
		log.Fatalf("Ошибка объявления очереди: %v", err)
	}

	// Устанавливаем prefetch = 1 для равномерного распределения
	err = ch.Qos(
		1,     // prefetch count
		0,     // prefetch size
		false, // global
	)
	if err != nil {
		log.Fatalf("Ошибка установки QoS: %v", err)
	}

	// Подписываемся на очередь
	msgs, err := ch.Consume(
		q.Name, // queue
		"",     // consumer
		false,  // auto-ack (отключено для manual ack)
		false,  // exclusive
		false,  // no-local
		false,  // no-wait
		nil,    // args
	)
	if err != nil {
		log.Fatalf("Ошибка подписки: %v", err)
	}

	// Канал для graceful shutdown
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	log.Printf("Worker %s готов к обработке задач", workerID)

	for {
		select {
		case msg := <-msgs:
			log.Printf("Получена задача: corrID=%s", msg.CorrelationId)

			// Парсим задачу
			var req TaskRequest
			if err := json.Unmarshal(msg.Body, &req); err != nil {
				log.Printf("Ошибка парсинга: %v", err)
				msg.Nack(false, false) // отклоняем без requeue
				continue
			}

			// Обрабатываем
			response := countWords(req.Text)
			log.Printf("Обработано: %d слов, %d символов", response.WordCount, response.CharCount)

			// Отправляем ответ
			respBody, err := json.Marshal(response)
			if err != nil {
				log.Printf("Ошибка сериализации: %v", err)
				msg.Nack(false, false)
				continue
			}

			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			err = ch.PublishWithContext(ctx,
				"",          // exchange
				msg.ReplyTo, // routing key (очередь ответа)
				false,       // mandatory
				false,       // immediate
				amqp.Publishing{
					ContentType:   "application/json",
					CorrelationId: msg.CorrelationId,
					Body:          respBody,
				})
			cancel()

			if err != nil {
				log.Printf("Ошибка отправки ответа: %v", err)
				msg.Nack(false, true) // requeue
				continue
			}

			msg.Ack(false)
			log.Printf("Задача выполнена: corrID=%s", msg.CorrelationId)

		case <-sigChan:
			log.Println("Получен сигнал завершения, выходим...")
			return
		}
	}
}
