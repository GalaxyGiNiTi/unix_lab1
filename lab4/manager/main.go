package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"time"

	"github.com/google/uuid"
	amqp "github.com/rabbitmq/amqp091-go"
)

// Конфигурация RabbitMQ
var (
	rabbitmqHost = getEnv("RABBITMQ_HOST", "localhost")
	rabbitmqUser = getEnv("RABBITMQ_USER", "user")
	rabbitmqPass = getEnv("RABBITMQ_PASS", "password")
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

type ErrorResponse struct {
	Error string `json:"error"`
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

// rpcCall отправляет задачу в очередь и ждёт ответа (RPC паттерн)
func rpcCall(text string) (*TaskResponse, error) {
	conn, err := connectWithRetry(getRabbitMQURL(), 10)
	if err != nil {
		return nil, err
	}
	defer conn.Close()

	ch, err := conn.Channel()
	if err != nil {
		return nil, fmt.Errorf("ошибка создания канала: %w", err)
	}
	defer ch.Close()

	// Объявляем очередь задач
	_, err = ch.QueueDeclare(
		"word_count_tasks", // name
		true,               // durable
		false,              // delete when unused
		false,              // exclusive
		false,              // no-wait
		nil,                // arguments
	)
	if err != nil {
		return nil, fmt.Errorf("ошибка объявления очереди: %w", err)
	}

	// Создаём временную очередь для ответа
	replyQueue, err := ch.QueueDeclare(
		"",    // name (пусто = автогенерация)
		false, // durable
		true,  // delete when unused
		true,  // exclusive
		false, // no-wait
		nil,   // arguments
	)
	if err != nil {
		return nil, fmt.Errorf("ошибка создания очереди ответа: %w", err)
	}

	// Подписываемся на ответы
	msgs, err := ch.Consume(
		replyQueue.Name, // queue
		"",              // consumer
		true,            // auto-ack
		false,           // exclusive
		false,           // no-local
		false,           // no-wait
		nil,             // args
	)
	if err != nil {
		return nil, fmt.Errorf("ошибка подписки на очередь: %w", err)
	}

	// Генерируем correlation ID
	corrID := uuid.New().String()

	// Формируем сообщение
	taskReq := TaskRequest{Text: text}
	body, err := json.Marshal(taskReq)
	if err != nil {
		return nil, fmt.Errorf("ошибка сериализации: %w", err)
	}

	// Отправляем задачу
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	err = ch.PublishWithContext(ctx,
		"",                 // exchange
		"word_count_tasks", // routing key
		false,              // mandatory
		false,              // immediate
		amqp.Publishing{
			ContentType:   "application/json",
			CorrelationId: corrID,
			ReplyTo:       replyQueue.Name,
			DeliveryMode:  amqp.Persistent,
			Body:          body,
		})
	if err != nil {
		return nil, fmt.Errorf("ошибка отправки сообщения: %w", err)
	}

	log.Printf("Отправлена задача с corrID=%s", corrID)

	// Ждём ответ
	for {
		select {
		case msg := <-msgs:
			if msg.CorrelationId == corrID {
				var response TaskResponse
				if err := json.Unmarshal(msg.Body, &response); err != nil {
					return nil, fmt.Errorf("ошибка десериализации ответа: %w", err)
				}
				log.Printf("Получен ответ от worker: %s", response.ProcessedBy)
				return &response, nil
			}
		case <-ctx.Done():
			return nil, fmt.Errorf("таймаут ожидания ответа")
		}
	}
}

// HTTP обработчики

func countHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.WriteHeader(http.StatusMethodNotAllowed)
		json.NewEncoder(w).Encode(ErrorResponse{Error: "Метод не поддерживается"})
		return
	}

	var req TaskRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		w.WriteHeader(http.StatusBadRequest)
		json.NewEncoder(w).Encode(ErrorResponse{Error: "Неверный формат JSON"})
		return
	}

	if req.Text == "" {
		w.WriteHeader(http.StatusBadRequest)
		json.NewEncoder(w).Encode(ErrorResponse{Error: "Поле 'text' обязательно"})
		return
	}

	response, err := rpcCall(req.Text)
	if err != nil {
		log.Printf("Ошибка RPC: %v", err)
		w.WriteHeader(http.StatusInternalServerError)
		json.NewEncoder(w).Encode(ErrorResponse{Error: fmt.Sprintf("Ошибка обработки: %v", err)})
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

func healthHandler(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{"status": "ok", "service": "manager"})
}

func main() {
	http.HandleFunc("/count", countHandler)
	http.HandleFunc("/health", healthHandler)

	port := getEnv("PORT", "8000")
	log.Printf("Manager запущен на порту %s", port)
	log.Fatal(http.ListenAndServe(":"+port, nil))
}
