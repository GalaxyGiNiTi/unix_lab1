#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <iomanip>
#include <sstream>
#include <atomic>
#include <ctime>

/**
 * @brief Структура события с несериализуемыми данными
 * 
 * Демонстрирует передачу сложных объектов между потоками через указатели.
 * В реальном приложении здесь могут быть любые данные: указатели на ресурсы,
 * файловые дескрипторы, сетевые соединения и т.д.
 */
struct Event {
    int id;
    std::chrono::system_clock::time_point timestamp;
    
    explicit Event(int id) 
        : id(id), timestamp(std::chrono::system_clock::now()) {}
};

/**
 * @brief Монитор для безопасного обмена событиями между потоками
 * 
 * Реализует паттерн "Монитор" с использованием:
 * - std::mutex для взаимного исключения
 * - std::condition_variable для эффективного ожидания без активной блокировки
 * - Семантика "один поставщик - один потребитель"
 */
class EventMonitor {
public:
    EventMonitor() : hasEvent_(false), isShutdown_(false) {}
    
    /**
     * @brief Отправка события от поставщика
     * @param ev Указатель на событие для передачи потребителю
     * @return true если событие отправлено, false если монитор завершает работу
     */
    bool sendEvent(std::shared_ptr<Event> ev) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // Ждём, пока предыдущее событие не будет обработано
        cond_.wait(lock, [this]() { return !hasEvent_ || isShutdown_; });
        
        if (isShutdown_) {
            return false;
        }

        // Кладём новое событие
        event_ = ev;
        hasEvent_ = true;

        printMessage("[Producer]", "Sent event", ev->id);

        // Будим потребителя
        cond_.notify_one();
        return true;
    }

    /**
     * @brief Получение события потребителем
     * @return Указатель на событие или nullptr при завершении работы
     */
    std::shared_ptr<Event> waitEvent() {
        std::unique_lock<std::mutex> lock(mutex_);

        // Ждём, пока событие не появится или не придёт сигнал завершения
        cond_.wait(lock, [this]() { return hasEvent_ || isShutdown_; });
        
        if (isShutdown_ && !hasEvent_) {
            return nullptr;
        }

        auto ev = event_;
        hasEvent_ = false;

        printMessage("[Consumer]", "Received event", ev->id);

        // Будим поставщика — можно отправлять следующее
        cond_.notify_one();

        return ev;
    }
    
    /**
     * @brief Корректное завершение работы монитора
     */
    void shutdown() {
        std::unique_lock<std::mutex> lock(mutex_);
        isShutdown_ = true;
        cond_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable cond_;
    bool hasEvent_;
    bool isShutdown_;
    std::shared_ptr<Event> event_;
    std::mutex coutMutex_; // Дополнительная защита для вывода в консоль
    
    /**
     * @brief Потокобезопасный вывод сообщений с временной меткой
     */
    void printMessage(const char* actor, const char* action, int eventId) {
        std::lock_guard<std::mutex> lock(coutMutex_);
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        time_t time_t_now = std::chrono::system_clock::to_time_t(now);
        
        // Кроссплатформенный вывод времени
        char time_buffer[32];
        struct tm tm_buf;
        
        #ifdef _WIN32
            localtime_s(&tm_buf, &time_t_now);
        #else
            localtime_r(&time_t_now, &tm_buf);
        #endif
        
        strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", &tm_buf);
        
        std::cout << "[" << time_buffer
                  << "." << std::setfill('0') << std::setw(3) << ms.count() << "] "
                  << std::setfill(' ') << std::left << std::setw(12) << actor << " "
                  << action << " id=" << eventId << std::endl;
    }
};

/**
 * @brief Главная функция - демонстрация работы монитора
 */
int main() {
    std::cout << "=== Monitor Demo: Producer-Consumer Pattern ===" << std::endl;
    std::cout << "Starting producer and consumer threads..." << std::endl;
    std::cout << std::endl;

    EventMonitor monitor;
    const int EVENT_COUNT = 10; // Количество событий для демонстрации
    
    std::atomic<bool> producerDone(false);

    // Поток-поставщик
    std::thread producer([&monitor, &producerDone]() {
        try {
            for (int i = 1; i <= EVENT_COUNT; ++i) {
                // Задержка 1 секунда между событиями
                std::this_thread::sleep_for(std::chrono::seconds(1));

                auto ev = std::make_shared<Event>(i);
                if (!monitor.sendEvent(ev)) {
                    std::cerr << "Producer: shutdown signal received" << std::endl;
                    break;
                }
            }
            producerDone = true;
        } catch (const std::exception& e) {
            std::cerr << "Producer error: " << e.what() << std::endl;
        }
    });

    // Поток-потребитель
    std::thread consumer([&monitor, &producerDone]() {
        try {
            while (true) {
                auto ev = monitor.waitEvent();
                
                if (!ev) {
                    // Получен сигнал завершения
                    break;
                }
                
                // Симуляция обработки события
                // В реальном приложении здесь была бы полезная работа
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                // Проверка, не завершился ли поставщик
                if (producerDone && ev->id == EVENT_COUNT) {
                    break;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Consumer error: " << e.what() << std::endl;
        }
    });

    // Ожидаем завершения поставщика
    producer.join();
    
    // Даём время потребителю обработать последнее событие
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Корректное завершение работы монитора
    monitor.shutdown();
    
    // Ожидаем завершения потребителя
    consumer.join();

    std::cout << std::endl;
    std::cout << "=== All events processed successfully ===" << std::endl;

    return 0;
}
